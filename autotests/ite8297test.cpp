// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks report building against bytes captured from Lenovo Vantage.

#include "core/ite8297.h"

#include <QTest>

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

    void reportsAre64Bytes()
    {
        QCOMPARE(settingsReport({0x11, Mode::Off, 1, 2, Rgb{}}).size(), 64);
        QCOMPARE(commitReport(Group::B).size(), 64);
    }

    void colours()
    {
        QCOMPARE(parseRgb(QStringLiteral("ff3d9c")), std::optional<Rgb>(Rgb{0xff, 0x3d, 0x9c}));
        QCOMPARE(parseRgb(QStringLiteral("#158DDD")), std::optional<Rgb>(Rgb{0x15, 0x8d, 0xdd}));
        QVERIFY(!parseRgb(QStringLiteral("fff")));
        QVERIFY(!parseRgb(QStringLiteral("gg0000")));
        QCOMPARE(rgbToString(Rgb{0x15, 0x8d, 0xdd}), QStringLiteral("#158ddd"));
    }
};

QTEST_GUILESS_MAIN(Ite8297Test)
#include "ite8297test.moc"
