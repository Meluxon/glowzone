// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The CPU-temperature mode: colour steps against Lenovo Vantage's captured
// behaviour, the imitated EC smoothing, and reading hwmon (a fake tree).

#include "core/cputemperature.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Glowzone;
using Ite8297::Rgb;

static const CpuTemperatureDef &def()
{
    const ModelProfile *m = matchModel({QStringLiteral("LENOVO"), QStringLiteral("Legion T5 26IOB6"), QStringLiteral("3733")});
    Q_ASSERT(m && m->cpuTemperature);
    return *m->cpuTemperature;
}

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

static Rgb rgb(quint32 c)
{
    return {quint8(c >> 16), quint8(c >> 8), quint8(c)};
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
        QCOMPARE(TemperatureColours::colour(def(), TemperatureColours::step(def(), celsius)), rgb(colour));
    }

    // Going down needs a clear drop; going up doesn't wait.
    void hysteresis()
    {
        using TemperatureColours::step;
        QCOMPARE(step(def(), 70.0, 6), 7);   // up at once
        QCOMPARE(step(def(), 69.5, 7), 7);   // just below 70: stays
        QCOMPARE(step(def(), 69.0, 7), 7);
        QCOMPARE(step(def(), 68.9, 7), 6);   // clearly below: down
        QCOMPARE(step(def(), 50.0, 7), 3);   // a big drop goes all the way
        QCOMPARE(step(def(), 39.5, 1), 1);
        QCOMPARE(step(def(), 38.5, 1), 0);
        QCOMPARE(step(def(), 20.0, 0), 0);
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
        writeFile(root.filePath(QStringLiteral("hwmon0/name")), "acpitz\n");
        writeFile(root.filePath(QStringLiteral("hwmon0/temp1_input")), "27800\n");
        writeFile(root.filePath(QStringLiteral("hwmon4/name")), "coretemp\n");
        writeFile(root.filePath(QStringLiteral("hwmon4/temp2_label")), "Core 0\n");
        writeFile(root.filePath(QStringLiteral("hwmon4/temp2_input")), "61000\n");
        writeFile(root.filePath(QStringLiteral("hwmon4/temp1_label")), "Package id 0\n");
        writeFile(root.filePath(QStringLiteral("hwmon4/temp1_input")), "52000\n");

        TemperatureSensor sensor(def(), root.path());
        QString error;
        QCOMPARE(sensor.read(&error), std::optional<double>(52.0));

        writeFile(root.filePath(QStringLiteral("hwmon4/temp1_input")), "garbage\n");
        QVERIFY(!sensor.read(&error));
        QVERIFY(!error.isEmpty());
        writeFile(root.filePath(QStringLiteral("hwmon4/temp1_input")), "48500\n");
        QCOMPARE(sensor.read(&error), std::optional<double>(48.5)); // found again
    }

    void sensorMissing()
    {
        QTemporaryDir root;
        writeFile(root.filePath(QStringLiteral("hwmon0/name")), "acpitz\n");
        TemperatureSensor sensor(def(), root.path());
        QString error;
        QVERIFY(!sensor.read(&error));
        QVERIFY(error.contains(QLatin1String("coretemp")));

        writeFile(root.filePath(QStringLiteral("hwmon1/name")), "coretemp\n");
        writeFile(root.filePath(QStringLiteral("hwmon1/temp2_label")), "Core 0\n");
        QVERIFY(!sensor.read(&error));
        QVERIFY(error.contains(QLatin1String("Package id 0")));
    }

    // Reading right away on start, then a colour only when the step changes.
    void following()
    {
        QTemporaryDir root;
        const QString input = root.filePath(QStringLiteral("hwmon2/temp1_input"));
        writeFile(root.filePath(QStringLiteral("hwmon2/name")), "coretemp\n");
        writeFile(root.filePath(QStringLiteral("hwmon2/temp1_label")), "Package id 0\n");
        writeFile(input, "60000\n");

        CpuTemperature t(def(), nullptr, root.path());
        QSignalSpy colours(&t, &CpuTemperature::colourChanged);
        QSignalSpy updates(&t, &CpuTemperature::updated);
        t.setInterval(20);
        t.start();
        QCOMPARE(colours.size(), 1);
        QCOMPARE(colours.at(0).at(0).value<Rgb>(), rgb(0x00ff50)); // 61 °C
        QCOMPARE(t.temperature(), std::optional<double>(61.0));

        QVERIFY(QTest::qWaitFor([&] { return updates.size() >= 4; }));
        QCOMPARE(colours.size(), 1); // same step: nothing new

        writeFile(input, "35000\n"); // cooling down – the smoothing takes its time
        QVERIFY(QTest::qWaitFor([&] { return t.colour() == rgb(0x0000ff); }, 15000));
        QVERIFY(colours.size() > 2); // walked through the steps in between

        t.stop();
        QVERIFY(!t.isRunning());
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
        QCOMPARE(t.colour(), rgb(0x0000ff)); // the coolest while nothing is known
        QVERIFY(!t.temperature());
    }
};

QTEST_GUILESS_MAIN(CpuTemperatureTest)
#include "cputemperaturetest.moc"
