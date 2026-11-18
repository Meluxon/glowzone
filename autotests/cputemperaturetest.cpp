// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The CPU-temperature mode: colour steps against Lenovo Vantage's captured
// behaviour (the default range), other ranges and the fades outside them, the
// imitated EC smoothing, and reading hwmon (a fake tree).

#include "core/cputemperature.h"
#include "testmodel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

using namespace Glowzone;
using Ite8297::Rgb;

static const CpuTemperatureDef &def()
{
    return legionModel().cpuTemperature.value();
}

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

class CpuTemperatureTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Steady temperatures and the colours Vantage sent for them (runs 0 and A,
    // 2026-11-15; the temperature is the EC's, which the smoothing imitates).
    void stepsMatchVantage_data()
    {
        QTest::addColumn<double>("celsius");
        QTest::addColumn<quint32>("colour");
        QTest::newRow("idle") << 35.0 << 0x0000ffu;
        QTest::newRow("40") << 40.0 << 0x0050ffu;
        QTest::newRow("44") << 44.0 << 0x0050ffu;
        QTest::newRow("47") << 47.0 << 0x00a0ffu;
        QTest::newRow("52") << 52.0 << 0x00ffffu;
        QTest::newRow("59") << 59.0 << 0x00ffa0u;
        QTest::newRow("1 core: 60") << 60.0 << 0x00ff50u;
        QTest::newRow("1 core: 62") << 62.0 << 0x00ff50u;
        QTest::newRow("2 cores: 69") << 69.0 << 0x00ff00u;
        QTest::newRow("2 cores: 70") << 70.0 << 0x50ff00u;
        QTest::newRow("76") << 76.0 << 0xa0ff00u;
        QTest::newRow("full load: 80") << 80.0 << 0xffff00u;
        QTest::newRow("hot: 100 (not captured)") << 100.0 << 0xff0000u;
    }
    void stepsMatchVantage()
    {
        QFETCH(double, celsius);
        QFETCH(quint32, colour);
        const TemperatureScale scale(def(), def().defaultRange, 100);
        QCOMPARE(scale.colourAt(celsius), Ite8297::rgbFromValue(colour));
    }

    // Going down needs a clear drop; going up doesn't wait.
    void hysteresis()
    {
        const TemperatureScale s(def(), def().defaultRange, 100);
        const int at65 = s.step(65.0);
        const int at70 = s.step(70.0);
        QCOMPARE(at70, at65 + 1);
        QCOMPARE(s.step(70.0, at65), at70);    // up at once
        QCOMPARE(s.step(69.5, at70), at70);    // just below 70: stays
        QCOMPARE(s.step(69.0, at70), at70);
        QCOMPARE(s.step(68.9, at70), at65);    // clearly below: down
        QCOMPARE(s.step(50.0, at70), s.step(50.0)); // a big drop goes all the way
        const int at40 = s.step(40.0);
        QCOMPARE(s.step(39.5, at40), at40);
        QCOMPARE(s.colour(s.step(38.5, at40)), Ite8297::rgbFromValue(0x0000ff));
    }

    // The colours spread evenly over another range; outside it they fade.
    void range()
    {
        const TemperatureScale s(def(), {35, 80}, 100); // 45 °C / 13 colours ≈ 3.46 °C each
        QCOMPARE(s.colourAt(35.0), Ite8297::rgbFromValue(0x0000ff));
        QCOMPARE(s.colourAt(38.4), Ite8297::rgbFromValue(0x0000ff));
        QCOMPARE(s.colourAt(38.5), Ite8297::rgbFromValue(0x0050ff));
        QCOMPARE(s.colourAt(62.0), Ite8297::rgbFromValue(0x50ff00)); // 8th colour from 59.2 °C
        QCOMPARE(s.colourAt(76.6), Ite8297::rgbFromValue(0xff0000));
        QCOMPARE(s.colourAt(79.9), Ite8297::rgbFromValue(0xff0000));
        // Above: towards white, one step per °C, white at the top.
        QCOMPARE(s.colourAt(80.0), Ite8297::rgbFromValue(0xff0000));
        QCOMPARE(s.colourAt(80.9), Ite8297::rgbFromValue(0xff0000));
        QCOMPARE(s.colourAt(81.0), (Rgb{255, 13, 13}));   // 1/20 of the way
        QCOMPARE(s.colourAt(90.5), (Rgb{255, 128, 128})); // half
        QCOMPARE(s.colourAt(99.0), (Rgb{255, 242, 242})); // 19/20
        QCOMPARE(s.colourAt(100.0), (Rgb{255, 255, 255}));
        QCOMPARE(s.colourAt(105.0), (Rgb{255, 255, 255}));
        // Below: the first colour, dimmer per °C as the eye sees it, darkest at 20 °C.
        QCOMPARE(s.colourAt(34.0), (Rgb{0, 0, 228}));
        QCOMPARE(s.colourAt(20.0), (Rgb{0, 0, 12}));
        QCOMPARE(s.colourAt(5.0), (Rgb{0, 0, 12}));
        quint8 last = 0;
        for (int t = 20; t <= 35; ++t) {
            const quint8 b = s.colourAt(t).b;
            QVERIFY2(b > last || t == 20, qPrintable(QString::number(t)));
            last = b;
        }
        // No fade where the range reaches the end.
        const TemperatureScale full(def(), {20, 100}, 100);
        QCOMPARE(full.colourAt(10.0), Ite8297::rgbFromValue(0x0000ff));
        QCOMPARE(full.colourAt(120.0), Ite8297::rgbFromValue(0xff0000));
    }

    void usableRanges_data()
    {
        QTest::addColumn<int>("coolest");
        QTest::addColumn<int>("hottest");
        QTest::addColumn<int>("top");
        QTest::addColumn<int>("usableCoolest");
        QTest::addColumn<int>("usableHottest");
        QTest::newRow("fine") << 35 << 80 << 100 << 35 << 80;
        QTest::newRow("too narrow") << 30 << 35 << 100 << 22 << 35;
        QTest::newRow("too narrow at the bottom") << 20 << 25 << 100 << 20 << 33;
        QTest::newRow("over the top") << 50 << 120 << 100 << 50 << 100;
        QTest::newRow("lower top") << 50 << 100 << 90 << 50 << 90;
        QTest::newRow("too cool") << 10 << 40 << 100 << 20 << 40;
        QTest::newRow("upside down") << 80 << 40 << 100 << 27 << 40;
        QTest::newRow("top below the narrowest range") << 35 << 80 << 30 << 20 << 33;
    }
    void usableRanges()
    {
        QFETCH(int, coolest);
        QFETCH(int, hottest);
        QFETCH(int, top);
        QFETCH(int, usableCoolest);
        QFETCH(int, usableHottest);
        QCOMPARE(usableRange(def(), {coolest, hottest}, top), (TemperatureRange{usableCoolest, usableHottest}));
        const TemperatureScale s(def(), {coolest, hottest}, top);
        QCOMPARE(s.range(), (TemperatureRange{usableCoolest, usableHottest}));
    }

    void smoothing()
    {
        TemperatureSmoother s(def());
        QCOMPARE(s.add(34.0, 0), 35.0); // first reading as it is (+1 °C offset)
        // A load starts: the package jumps to 79 °C, the shown value ramps at 4 °C/s.
        QCOMPARE(s.add(79.0, 2.0), 43.0);
        QCOMPARE(s.add(79.0, 2.0), 51.0);
        QCOMPARE(s.add(79.0, 2.0), 59.0);
        double v = 0;
        for (int i = 0; i < 10; ++i) {
            v = s.add(79.0, 2.0);
        }
        QCOMPARE(v, 80.0); // steady: package + 1, like the EC
        // The load stops: down at the same rate.
        QCOMPARE(s.add(45.0, 2.0), 72.0);
        // A single spike is averaged out.
        TemperatureSmoother t(def());
        t.add(40.0, 0);
        t.add(40.0, 2.0);
        QCOMPARE(t.add(70.0, 2.0), 49.0); // average 50 + 1 = 51, limited to +8
    }

    void sensor()
    {
        QTemporaryDir root;
        writeFile(root.filePath(u"hwmon0/name"_s), "acpitz\n");
        writeFile(root.filePath(u"hwmon0/temp1_input"_s), "27800\n");
        writeFile(root.filePath(u"hwmon4/name"_s), "coretemp\n");
        writeFile(root.filePath(u"hwmon4/temp2_label"_s), "Core 0\n");
        writeFile(root.filePath(u"hwmon4/temp2_input"_s), "61000\n");
        writeFile(root.filePath(u"hwmon4/temp1_label"_s), "Package id 0\n");
        writeFile(root.filePath(u"hwmon4/temp1_input"_s), "52000\n");

        TemperatureSensor sensor(def(), root.path());
        QString error;
        QCOMPARE(sensor.read(&error), std::optional<double>(52.0));
        QCOMPARE(sensor.limit(), std::nullopt); // no tempN_crit

        writeFile(root.filePath(u"hwmon4/temp1_crit"_s), "105000\n");
        writeFile(root.filePath(u"hwmon4/temp1_input"_s), "x\n");
        QVERIFY(!sensor.read(&error)); // searched again on the next read
        writeFile(root.filePath(u"hwmon4/temp1_input"_s), "52000\n");
        QVERIFY(sensor.read(&error));
        QCOMPARE(sensor.limit(), std::optional<int>(105));

        writeFile(root.filePath(u"hwmon4/temp1_input"_s), "garbage\n");
        QVERIFY(!sensor.read(&error));
        QVERIFY(!error.isEmpty());
        writeFile(root.filePath(u"hwmon4/temp1_input"_s), "48500\n");
        QCOMPARE(sensor.read(&error), std::optional<double>(48.5)); // found again
    }

    void sensorMissing()
    {
        QTemporaryDir root;
        writeFile(root.filePath(u"hwmon0/name"_s), "acpitz\n");
        TemperatureSensor sensor(def(), root.path());
        QString error;
        QVERIFY(!sensor.read(&error));
        QVERIFY(error.contains("coretemp"_L1));

        writeFile(root.filePath(u"hwmon1/name"_s), "coretemp\n");
        writeFile(root.filePath(u"hwmon1/temp2_label"_s), "Core 0\n");
        QVERIFY(!sensor.read(&error));
        QVERIFY(error.contains("Package id 0"_L1));
    }

    // Reading right away on start, then a colour only when the step changes.
    void following()
    {
        QTemporaryDir root;
        const QString input = root.filePath(u"hwmon2/temp1_input"_s);
        writeFile(root.filePath(u"hwmon2/name"_s), "coretemp\n");
        writeFile(root.filePath(u"hwmon2/temp1_label"_s), "Package id 0\n");
        writeFile(input, "60000\n");

        CpuTemperature t(def(), nullptr, root.path());
        QSignalSpy colours(&t, &CpuTemperature::colourChanged);
        QSignalSpy updates(&t, &CpuTemperature::updated);
        t.setInterval(20);
        t.start();
        QCOMPARE(colours.size(), 1);
        QCOMPARE(colours.at(0).at(0).value<Rgb>(), Ite8297::rgbFromValue(0x00ff50)); // 61 °C
        QCOMPARE(t.temperature(), std::optional<double>(61.0));

        QVERIFY(QTest::qWaitFor([&] { return updates.size() >= 4; }));
        QCOMPARE(colours.size(), 1); // same step: nothing new

        writeFile(input, "35000\n"); // cooling down – the smoothing takes its time
        QVERIFY(QTest::qWaitFor([&] { return t.colour() == Ite8297::rgbFromValue(0x0000ff); }, 15000));
        QVERIFY(colours.size() > 2); // walked through the steps in between

        t.stop();
        QVERIFY(!t.isRunning());
    }

    // A new range recolours at once, while running; the same colour sends nothing.
    void rangeChange()
    {
        QTemporaryDir root;
        writeFile(root.filePath(u"hwmon2/name"_s), "coretemp\n");
        writeFile(root.filePath(u"hwmon2/temp1_label"_s), "Package id 0\n");
        writeFile(root.filePath(u"hwmon2/temp1_input"_s), "60000\n");
        writeFile(root.filePath(u"hwmon2/temp1_crit"_s), "100000\n");

        CpuTemperature t(def(), nullptr, root.path());
        QSignalSpy colours(&t, &CpuTemperature::colourChanged);
        t.setRange({36, 100});
        QCOMPARE(colours.size(), 0); // not running
        t.setInterval(60000);
        t.start();
        QCOMPARE(colours.size(), 1);
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0x00ff50)); // 61 °C

        t.setRange({35, 60}); // 61 °C is above it now
        QCOMPARE(colours.size(), 2);
        QCOMPARE(t.colour(), (Rgb{255, 6, 6})); // 1/40 of the way to white
        t.setRange({35, 61});
        QCOMPARE(colours.size(), 3);
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0xff0000)); // the last colour, up to hottest
        t.setRange({34, 61}); // still the same colour
        QCOMPARE(colours.size(), 3);
        QCOMPARE(t.scale().top(), 100);
    }

    // Setting the same range again keeps the step that the hysteresis holds.
    void sameRangeKeepsHysteresis()
    {
        QTemporaryDir root;
        const QString input = root.filePath(u"hwmon2/temp1_input"_s);
        writeFile(root.filePath(u"hwmon2/name"_s), "coretemp\n");
        writeFile(root.filePath(u"hwmon2/temp1_label"_s), "Package id 0\n");
        writeFile(input, "69000\n"); // 70 °C with the offset: the step from 70

        CpuTemperature t(def(), nullptr, root.path());
        t.setRange(def().defaultRange);
        QSignalSpy updates(&t, &CpuTemperature::updated);
        t.setInterval(20);
        t.start();
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0x50ff00));
        writeFile(input, "68800\n"); // a little below 70, within the hysteresis
        QVERIFY(QTest::qWaitFor([&] { return updates.size() >= 4; }));
        QVERIFY(*t.temperature() < 70.0);
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0x50ff00)); // held
        t.setRange(def().defaultRange);
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0x50ff00)); // still held
        t.setRange({35, 120}); // other numbers, the same range once limited to the top
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0x50ff00));
    }

    // A sensor found again with another limit changes the colours, and says so.
    void limitChange()
    {
        QTemporaryDir root;
        const QString input = root.filePath(u"hwmon2/temp1_input"_s);
        writeFile(root.filePath(u"hwmon2/name"_s), "coretemp\n");
        writeFile(root.filePath(u"hwmon2/temp1_label"_s), "Package id 0\n");
        writeFile(input, "60000\n");

        CpuTemperature t(def(), nullptr, root.path());
        QSignalSpy scales(&t, &CpuTemperature::scaleChanged);
        QSignalSpy updates(&t, &CpuTemperature::updated);
        t.setInterval(20);
        t.start();
        QCOMPARE(t.scale().top(), def().hottestLimit); // no tempN_crit
        QCOMPARE(scales.size(), 0);

        writeFile(input, "x\n"); // fails: searched again on the next reading
        writeFile(root.filePath(u"hwmon2/temp1_crit"_s), "105000\n");
        QVERIFY(QTest::qWaitFor([&] { return updates.size() >= 3; }));
        writeFile(input, "60000\n");
        QVERIFY(QTest::qWaitFor([&] { return scales.size() == 1; }));
        QCOMPARE(t.scale().top(), 105);
    }

    void failingSensorReportsOnce()
    {
        QTemporaryDir root;
        CpuTemperature t(def(), nullptr, root.path());
        QSignalSpy failed(&t, &CpuTemperature::failed);
        QSignalSpy updates(&t, &CpuTemperature::updated);
        t.setInterval(10);
        t.start();
        QVERIFY(QTest::qWaitFor([&] { return updates.size() >= 3; }));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(t.colour(), Ite8297::rgbFromValue(0x0000ff)); // the coolest while nothing is known
        QVERIFY(!t.temperature());
    }
};

QTEST_GUILESS_MAIN(CpuTemperatureTest)
#include "cputemperaturetest.moc"
