// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The login start entry: whatever the program path, it must stay one Exec line
// that runs exactly that program (Desktop Entry spec quoting and escaping).

#include "core/settings.h"

#include <QFile>
#include <QStandardPaths>
#include <QTest>

using namespace Glowzone;

static QString line(const QString &entry, const QString &key)
{
    for (const QString &l : entry.split(QLatin1Char('\n'))) {
        if (l.startsWith(key + QLatin1Char('='))) {
            return l.mid(key.size() + 1);
        }
    }
    return {};
}

class SettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void autostartEntry_data()
    {
        QTest::addColumn<QString>("program");
        QTest::addColumn<QString>("exec");
        QTest::addColumn<QString>("tryExec");
        QTest::newRow("plain") << QStringLiteral("/usr/bin/glowzone") << QStringLiteral("\"/usr/bin/glowzone\" --background")
                               << QStringLiteral("/usr/bin/glowzone");
        QTest::newRow("space") << QStringLiteral("/home/me/My Apps/glowzone") << QStringLiteral("\"/home/me/My Apps/glowzone\" --background")
                               << QStringLiteral("/home/me/My Apps/glowzone");
        // Quoting first (\" \` \$ \\), then string escaping doubles every \.
        QTest::newRow("specials") << QStringLiteral("/opt/a\"b`c$d\\e/glowzone")
                                  << QStringLiteral("\"/opt/a\\\\\"b\\\\`c\\\\$d\\\\\\\\e/glowzone\" --background")
                                  << QStringLiteral("/opt/a\"b`c$d\\\\e/glowzone");
        QTest::newRow("percent") << QStringLiteral("/opt/100%/glowzone") << QStringLiteral("\"/opt/100%%/glowzone\" --background")
                                 << QStringLiteral("/opt/100%/glowzone");
        // A line break must not start a new key.
        QTest::newRow("newline") << QStringLiteral("/tmp/x\nExec=evil") << QStringLiteral("\"/tmp/x\\nExec=evil\" --background")
                                 << QStringLiteral("/tmp/x\\nExec=evil");
    }
    void autostartEntry()
    {
        QFETCH(QString, program);
        QFETCH(QString, exec);
        QFETCH(QString, tryExec);
        const QString entry = Settings::autostartEntry(program);
        QCOMPARE(line(entry, QStringLiteral("Exec")), exec);
        QCOMPARE(line(entry, QStringLiteral("TryExec")), tryExec);
        QCOMPARE(entry.count(QLatin1String("\nExec=")), 1);
        QVERIFY(entry.startsWith(QLatin1String("[Desktop Entry]\n")));
    }

    void temperatureRange()
    {
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(Settings::filePath());
        const ModelProfile *m = matchModel({QStringLiteral("LENOVO"), QStringLiteral("Legion T5 26IOB6"), QStringLiteral("3733")});
        QVERIFY(m && m->cpuTemperature);
        QCOMPARE(Settings::temperatureRange(*m->cpuTemperature), m->cpuTemperature->defaultRange); // never set
        Settings::setTemperatureRange({40, 85});
        QCOMPARE(Settings::temperatureRange(*m->cpuTemperature), (TemperatureRange{40, 85}));
    }

    void startAtLogin()
    {
        QStandardPaths::setTestModeEnabled(true);
        QString error;
        QVERIFY2(Settings::setStartAtLogin(true, &error), qPrintable(error));
        QVERIFY(Settings::startsAtLogin());
        QVERIFY(Settings::autostartPath().endsWith(QLatin1String("/autostart/io.github.meluxon.Glowzone.desktop")));
        QVERIFY2(Settings::setStartAtLogin(false, &error), qPrintable(error));
        QVERIFY(!Settings::startsAtLogin());
        QVERIFY(Settings::setStartAtLogin(false, &error)); // already off: fine
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "settingstest.moc"
