// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/cputemperature.h"
#include "core/eclights.h"
#include "core/fanstate.h"
#include "core/ite8297.h"
#include "model/modelprofile.h"
#include "testmodel.h"

#include <QFile>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

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
        const ModelProfile &m = legionModel();
        QCOMPARE(m.fan(u"top"_s)->headers(), (QList<quint8>{0x39, 0x12}));
        QCOMPARE(m.fan(u"front"_s)->headers(), (QList<quint8>{0x3a, 0x38}));
        QVERIFY(!m.fan(u"cpu"_s)->isDouble());
        QVERIFY(m.fan(u"top"_s)->isDouble());
        // Physical positions, confirmed on the hardware
        QCOMPARE(m.fan(u"front"_s)->led(u"upper"_s)->header, quint8(0x3a));
        QCOMPARE(m.fan(u"front"_s)->led(u"lower"_s)->header, quint8(0x38));
        QCOMPARE(m.fan(u"top"_s)->led(u"front"_s)->header, quint8(0x39));
        QCOMPARE(m.fan(u"top"_s)->led(u"rear"_s)->header, quint8(0x12));
        QCOMPARE(m.fan(u"top"_s)->led(u"2"_s)->header, quint8(0x12));
        QVERIFY(!m.fan(u"top"_s)->led(u"3"_s));
        QVERIFY(!m.fan(u"top"_s)->led(u"upper"_s));
        QVERIFY(!m.fan(u"cpu"_s)->led(u"front"_s));
        QCOMPARE(m.ecLight(u"ambient"_s)->index, quint8(2));
        QVERIFY(!m.fan(u"gpu"_s));
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
                QVERIFY2(!t.hwmonName.isEmpty() && !t.sensorLabel.isEmpty(), name.constData());
                QVERIFY2(t.averageSamples >= 1 && t.maxChangePerSecond > 0 && t.intervalMs >= 500, name.constData());
                // A few °C at most: with 1 °C steps (the fades, the narrowest range) the colour
                // going down trails by that much.
                QVERIFY2(t.hysteresis >= 0 && t.hysteresis <= 2.0, name.constData());
                QVERIFY2(t.colours.size() >= 2, name.constData());
                QVERIFY2(t.darkest > 0 && t.darkest <= 1 && t.darkAt > 0, name.constData());
                QVERIFY2(usableRange(t, t.defaultRange, t.hottestLimit) == t.defaultRange, name.constData());
            }
            QVERIFY2(m.fanLevels >= Ite8297::MinLevel, name.constData());
            QSet<quint8> headers;
            QSet<QString> fanIds;
            for (const FanDef &f : m.fans) {
                QVERIFY2(!f.id.isEmpty() && !fanIds.contains(f.id), name.constData());
                fanIds.insert(f.id);
                QSet<QString> ledIds;
                for (const LedDef &led : f.leds) {
                    QVERIFY2(Ite8297::groupOf(led.header), name.constData());
                    QVERIFY2(FanState::fitsModel(m, {.header = led.header}), name.constData()); // e.g. its brightness
                    QVERIFY2(!headers.contains(led.header), name.constData()); // one LED per header
                    headers.insert(led.header);
                    // A double fan names its LEDs (the CLI's --led), a single one doesn't.
                    QVERIFY2(f.isDouble() == !led.id.isEmpty() && !ledIds.contains(led.id), name.constData());
                    ledIds.insert(led.id);
                }
            }
            for (const FanLayoutDef &layout : m.fanLayouts) {
                QVERIFY2(m.undetectedFans(layout.code).size() == layout.undetectedFans.size(), name.constData()); // no typo
            }

            QSet<QString> lightIds;
            QSet<quint8> lightIndices;
            for (const EcLightDef &l : m.ecLights) {
                QVERIFY2(!l.id.isEmpty() && !lightIds.contains(l.id) && !lightIndices.contains(l.index), name.constData());
                lightIds.insert(l.id);
                lightIndices.insert(l.index);
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
            const QString line = u"ATTRS{idVendor}==\"%1\", ATTRS{idProduct}==\"%2\""_s
                                     .arg(m.usbVendorId, 4, 16, u'0')
                                     .arg(m.usbProductId, 4, 16, u'0');
            QVERIFY2(rules.contains(line), qPrintable(m.displayName()));
        }
    }
};

QTEST_GUILESS_MAIN(ModelTest)
#include "modeltest.moc"
