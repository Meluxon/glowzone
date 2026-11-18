// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks report building against bytes captured from Lenovo Vantage.

#include "core/ite8297.h"

#include <QTest>

using namespace Qt::StringLiterals;

using namespace Glowzone::Ite8297;

static QByteArray report(const QByteArray &hexPrefix)
{
    QByteArray r = QByteArray::fromHex(hexPrefix);
    r.append(QByteArray(ReportSize - r.size(), '\0'));
    return r;
}

class Ite8297Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cpuRainbow()
    {
        // Capture: CPU fan -> rainbow: cc 13 09 02 01 15 8d dd -> cc 28 0f
        const HeaderSetting s{0x13, Mode::Rainbow, 2, 1, Rgb{0x15, 0x8d, 0xdd}};
        const QList<QByteArray> reports = reportsFor({s});
        QCOMPARE(reports.size(), 2);
        QCOMPARE(reports.at(0), report("cc13090201158ddd"));
        QCOMPARE(reports.at(1), report("cc280f"));
    }

    void topStaticPink()
    {
        // Capture: top fans -> static pink; headers in both groups, so two commits.
        const Rgb pink{0xff, 0x3d, 0x9c};
        const QList<QByteArray> reports = reportsFor({{0x39, Mode::Static, 1, 2, pink}, {0x12, Mode::Static, 1, 2, pink}});
        QCOMPARE(reports.size(), 4);
        QCOMPARE(reports.at(0), report("cc39010102ff3d9c"));
        QCOMPARE(reports.at(1), report("cc12010102ff3d9c"));
        QCOMPARE(reports.at(2), report("cc280f"));
        QCOMPARE(reports.at(3), report("cc2d07"));
    }

    void frontRandomCommitsGroupBOnly()
    {
        const Rgb c{0x15, 0x8d, 0xdd};
        const QList<QByteArray> reports = reportsFor({{0x3a, Mode::Random, 1, 2, c}, {0x38, Mode::Random, 1, 2, c}});
        QCOMPARE(reports.size(), 3);
        QCOMPARE(reports.at(0), report("cc3a0a0102158ddd"));
        QCOMPARE(reports.at(2), report("cc2d07"));
    }

    void unknownHeaderIsNeverSent()
    {
        QVERIFY(reportsFor({{0x20, Mode::Static, 1, 2, Rgb{}}}).isEmpty());
        QVERIFY(!isKnownHeader(0x14));
        QVERIFY(!isKnownHeader(0x37));
        QVERIFY(isKnownHeader(0x10));
        QVERIFY(isKnownHeader(0x3a));
    }

    // Glowzone's own mode value must never reach the controller...
    void cpuTemperatureModeIsNeverSent()
    {
        QVERIFY(reportsFor({{0x13, Mode::CpuTemperature, 1, 2, Rgb{}}}).isEmpty());
        // ...only what resolve() makes of it. Capture: Vantage's CPU temperature
        // at idle, CPU fan: cc 13 01 01 02 00 00 ff -> cc 28 0f
        const HeaderSetting resolved = resolve({0x13, Mode::CpuTemperature, 3, 2, Rgb{0x15, 0x8d, 0xdd}}, Rgb{0x00, 0x00, 0xff});
        const QList<QByteArray> reports = reportsFor({resolved});
        QCOMPARE(reports.size(), 2);
        QCOMPARE(reports.at(0), report("cc130101020000ff"));
        QCOMPARE(reports.at(1), report("cc280f"));
        // Other modes pass through unchanged.
        const HeaderSetting rainbow{0x11, Mode::Rainbow, 2, 3, Rgb{1, 2, 3}};
        QCOMPARE(resolve(rainbow, Rgb{0, 0, 0xff}), rainbow);
        QCOMPARE(modeInfo(u"cpu-temperature"_s)->mode, Mode::CpuTemperature);
        QVERIFY(followsTemperature(Mode::CpuTemperature));
        QVERIFY(!followsTemperature(Mode::Static));
        QVERIFY(!modeInfo(Mode::CpuTemperature)->usesColour);
    }

    void reportsAre64Bytes()
    {
        QCOMPARE(settingsReport({0x11, Mode::Off, 1, 2, Rgb{}}).size(), 64);
        QCOMPARE(commitReport(Group::B).size(), 64);
    }

    void colours()
    {
        QCOMPARE(parseRgb(u"ff3d9c"_s), std::optional<Rgb>(Rgb{0xff, 0x3d, 0x9c}));
        QCOMPARE(parseRgb(u"#158DDD"_s), std::optional<Rgb>(Rgb{0x15, 0x8d, 0xdd}));
        QVERIFY(!parseRgb(u"fff"_s));
        QVERIFY(!parseRgb(u"gg0000"_s));
        QVERIFY(!parseRgb(u"0x1234"_s));
        QVERIFY(!parseRgb(u"+12345"_s));
        QVERIFY(!parseRgb(u"12 345"_s));
        QVERIFY(!parseRgb(QString::fromUtf16(u"\u0661\u0662\u0663\u0664\u0665\u0666"))); // Arabic-Indic digits
        QCOMPARE(rgbToString(Rgb{0x15, 0x8d, 0xdd}), u"#158ddd"_s);
    }
};

QTEST_GUILESS_MAIN(Ite8297Test)
#include "ite8297test.moc"
