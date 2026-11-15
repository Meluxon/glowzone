// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The saved fan state is the only record of what the fans show – it must
// round-trip exactly and ignore anything it doesn't recognise.

#include "core/fanstate.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

using namespace Glowzone;
using namespace Glowzone::Ite8297;

static QJsonObject entry(const char *mode, int speed)
{
    return {{QStringLiteral("mode"), QLatin1String(mode)},
            {QStringLiteral("speed"), speed},
            {QStringLiteral("brightness"), 2},
            {QStringLiteral("colour"), QStringLiteral("#ff0000")}};
}

static const ModelProfile &model()
{
    return supportedModels().constFirst();
}

static void writeState(int version, const QJsonObject &headers)
{
    QFile f(FanState::filePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), version}, {QStringLiteral("headers"), headers}}).toJson());
}

class FanStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        QFile::remove(FanState::filePath());
    }

    void emptyWhenNothingSaved()
    {
        QVERIFY(FanState::load(model()).isEmpty());
    }

    void roundTrip()
    {
        QString error;
        const HeaderSetting cpu{0x13, Mode::Rainbow, 1, 2, Rgb{0x15, 0x8d, 0xdd}};
        const HeaderSetting front{0x3a, Mode::Static, 1, 3, Rgb{0xff, 0x3d, 0x9c}};
        QVERIFY2(FanState::merge({cpu, front}, &error), qPrintable(error));
        const FanState::Headers loaded = FanState::load(model());
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.value(0x13), cpu);
        QCOMPARE(loaded.value(0x3a), front);
    }

    void mergeKeepsOtherHeaders()
    {
        QString error;
        QVERIFY(FanState::merge({{0x13, Mode::Rainbow, 1, 2, Rgb{}}}, &error));
        QVERIFY(FanState::merge({{0x11, Mode::Off, 1, 2, Rgb{}}}, &error));
        QCOMPARE(FanState::load(model()).size(), 2);
    }

    void ignoresJunk()
    {
        writeState(1,
                   {{QStringLiteral("0x13"), entry("static", 1)},   // fine
                    {QStringLiteral("0x20"), entry("static", 1)},   // unknown header
                    {QStringLiteral("0x11"), entry("laser", 1)},    // unknown mode
                    {QStringLiteral("0x12"), entry("static", 9)}}); // speed out of range
        const FanState::Headers loaded = FanState::load(model());
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded.contains(0x13));
    }

    // A model offers a subset of what the controller can do: anything else in
    // the file is not shown or applied – but stays in the file.
    void dropsWhatTheModelDoesNotOffer()
    {
        ModelProfile small = model();
        small.fans = {small.fans.constFirst()}; // the CPU fan (0x13) only
        small.fanModes = {0x00, 0x01, 0x09};    // off, static, rainbow
        small.fanLevels = 2;
        writeState(1,
                   {{QStringLiteral("0x13"), entry("static", 2)},   // fine
                    {QStringLiteral("0x11"), entry("static", 1)}}); // not this model's fan
        QCOMPARE(FanState::load(small).size(), 1);

        writeState(1, {{QStringLiteral("0x13"), entry("random", 1)}}); // mode not offered
        QVERIFY(FanState::load(small).isEmpty());
        writeState(1, {{QStringLiteral("0x13"), entry("static", 3)}}); // level not offered
        QVERIFY(FanState::load(small).isEmpty());
        QCOMPARE(FanState::load(model()).size(), 1);                   // fine for the full model

        QString error;
        QVERIFY(FanState::merge({{0x13, Mode::Off, 1, 1, Rgb{}}, {0x11, Mode::Off, 1, 1, Rgb{}}}, &error));
        QVERIFY(FanState::merge({{0x13, Mode::Static, 1, 1, Rgb{}}}, &error));
        QCOMPARE(FanState::load(model()).size(), 2); // 0x11 kept by merge
    }

    void ignoresOtherVersions()
    {
        writeState(2, {{QStringLiteral("0x13"), entry("static", 1)}});
        QVERIFY(FanState::load(model()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(FanStateTest)
#include "fanstatetest.moc"
