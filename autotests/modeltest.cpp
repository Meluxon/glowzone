// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/cputemperature.h"
#include "core/eclights.h"
#include "core/fanstate.h"
#include "core/ite8297.h"
#include "model/modelprofile.h"

#include <QFile>
#include <QTest>

using namespace Glowzone;

class ModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void matchesOnlyTheExactModel_data()
    {
        QTest::addColumn<QString>("vendor");
        QTest::addColumn<QString>("product");
        QTest::addColumn<QString>("board");
        QTest::addColumn<bool>("supported");
        QTest::newRow("legion t5") << "LENOVO" << "Legion T5 26IOB6" << "3733" << true;
        QTest::newRow("other board") << "LENOVO" << "Legion T5 26IOB6" << "3734" << false;
        QTest::newRow("other model, same board") << "LENOVO" << "Legion T5 26AMR5" << "3733" << false;
        QTest::newRow("vendor case") << "Lenovo" << "Legion T5 26IOB6" << "3733" << false;
        QTest::newRow("empty (VM)") << "" << "" << "" << false;
    }

    void matchesOnlyTheExactModel()
    {
        QFETCH(QString, vendor);
        QFETCH(QString, product);
        QFETCH(QString, board);
        QFETCH(bool, supported);
        QCOMPARE(matchModel({vendor, product, board}) != nullptr, supported);
    }

    void legionLayout()
    {
        const ModelProfile *m = matchModel({QStringLiteral("LENOVO"), QStringLiteral("Legion T5 26IOB6"), QStringLiteral("3733")});
        QVERIFY(m);
        QCOMPARE(m->fan(QStringLiteral("top"))->headers(), (QList<quint8>{0x39, 0x12}));
        QCOMPARE(m->fan(QStringLiteral("front"))->headers(), (QList<quint8>{0x3a, 0x38}));
        QVERIFY(!m->fan(QStringLiteral("cpu"))->isDouble());
        QVERIFY(m->fan(QStringLiteral("top"))->isDouble());
        // Physical positions, confirmed on the hardware
        QCOMPARE(m->fan(QStringLiteral("front"))->led(QStringLiteral("upper"))->header, quint8(0x3a));
        QCOMPARE(m->fan(QStringLiteral("front"))->led(QStringLiteral("lower"))->header, quint8(0x38));
        QCOMPARE(m->fan(QStringLiteral("top"))->led(QStringLiteral("front"))->header, quint8(0x39));
        QCOMPARE(m->fan(QStringLiteral("top"))->led(QStringLiteral("rear"))->header, quint8(0x12));
        QCOMPARE(m->fan(QStringLiteral("top"))->led(QStringLiteral("2"))->header, quint8(0x12));
        QVERIFY(!m->fan(QStringLiteral("top"))->led(QStringLiteral("3")));
        QVERIFY(!m->fan(QStringLiteral("top"))->led(QStringLiteral("upper")));
        QVERIFY(!m->fan(QStringLiteral("cpu"))->led(QStringLiteral("front")));
        QCOMPARE(m->ecLight(QStringLiteral("ambient"))->index, quint8(2));
        QVERIFY(!m->fan(QStringLiteral("gpu")));
    }

    // Every profile may only offer what the protocol code can encode, must
    // offer "off" (reset, Turn All Off) and sane level ranges.
    void profilesAreConsistent()
    {
        for (const ModelProfile &m : supportedModels()) {
            const QByteArray name = m.displayName().toUtf8();
            QVERIFY2(m.hasFanMode(quint8(Ite8297::Mode::Off)), name.constData());
            for (quint8 v : m.fanModes) {
                QVERIFY2(Ite8297::modeInfo(Ite8297::Mode(v)), name.constData());
            }
            // fanModes are controller values only; the CPU-temperature mode
            // comes with cpuTemperature.
            QVERIFY2(!m.fanModes.contains(CpuTemperatureMode), name.constData());
            QCOMPARE(Ite8297::modesFor(m).size(), m.fanModes.size() + (m.cpuTemperature ? 1 : 0));
            if (m.cpuTemperature) {
                const CpuTemperatureDef &t = *m.cpuTemperature;
                QVERIFY2(t.hwmonName && *t.hwmonName && t.sensorLabel && *t.sensorLabel, name.constData());
                QVERIFY2(t.averageSamples >= 1 && t.maxChangePerSecond > 0 && t.intervalMs >= 500, name.constData());
                // A few °C at most: with 1 °C steps (the fades, the narrowest range) the colour
                // going down trails by that much.
                QVERIFY2(t.hysteresis >= 0 && t.hysteresis <= 2.0, name.constData());
                QVERIFY2(t.colours.size() >= 2, name.constData());
                QVERIFY2(t.darkest > 0 && t.darkest <= 1 && t.darkAt > 0, name.constData());
                QVERIFY2(usableRange(t, t.defaultRange, t.hottestLimit) == t.defaultRange, name.constData());
            }
            QVERIFY2(m.fanLevels >= Ite8297::MinLevel, name.constData());
            for (const FanDef &f : m.fans) {
                for (quint8 h : f.headers()) {
                    QVERIFY2(Ite8297::isKnownHeader(h), name.constData());
                    QVERIFY2(FanState::fitsModel(m, Ite8297::initialSetting(h)), name.constData()); // e.g. its brightness
                }
            }

            QVERIFY2(m.hasEcLightMode(EcLights::ModeOff), name.constData());
            for (quint8 v : m.ecLightModes) {
                QVERIFY2(EcLights::modeInfo(v), name.constData());
            }
            QCOMPARE(EcLights::modesFor(m).size(), m.ecLightModes.size());
            QVERIFY2(m.ecDefaultBrightness >= EcLights::MinBrightness && m.ecDefaultBrightness <= m.ecBrightnessLevels, name.constData());
        }
    }

    // The udev rule must let the user open every supported model's controller.
    void udevRuleCoversEveryModel()
    {
        QFile f(QStringLiteral(GLOWZONE_UDEV_RULES));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString rules = QString::fromUtf8(f.readAll());
        for (const ModelProfile &m : supportedModels()) {
            const QString line = QStringLiteral("ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\"")
                                     .arg(m.usbVendorId, 4, 16, QLatin1Char('0'))
                                     .arg(m.usbProductId, 4, 16, QLatin1Char('0'));
            QVERIFY2(rules.contains(line), qPrintable(m.displayName()));
        }
    }
};

QTEST_GUILESS_MAIN(ModelTest)
#include "modeltest.moc"
