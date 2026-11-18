// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The login start entry: whatever the program path, it must stay one Exec line
// that runs exactly that program (Desktop Entry spec quoting and escaping).

#include "core/settings.h"
#include "testmodel.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

using namespace Glowzone;

static QString line(const QString &entry, const QString &key)
{
    for (const QString &l : entry.split(u'\n')) {
        if (l.startsWith(key + u'=')) {
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
        QTest::newRow("plain") << u"/usr/bin/glowzone"_s << u"\"/usr/bin/glowzone\" --background"_s
                               << u"/usr/bin/glowzone"_s;
        QTest::newRow("space") << u"/home/me/My Apps/glowzone"_s << u"\"/home/me/My Apps/glowzone\" --background"_s
                               << u"/home/me/My Apps/glowzone"_s;
        // Quoting first (\" \` \$ \\), then string escaping doubles every \.
        QTest::newRow("specials") << u"/opt/a\"b`c$d\\e/glowzone"_s
                                  << u"\"/opt/a\\\\\"b\\\\`c\\\\$d\\\\\\\\e/glowzone\" --background"_s
                                  << u"/opt/a\"b`c$d\\\\e/glowzone"_s;
        QTest::newRow("percent") << u"/opt/100%/glowzone"_s << u"\"/opt/100%%/glowzone\" --background"_s
                                 << u"/opt/100%/glowzone"_s;
        // A line break must not start a new key.
        QTest::newRow("newline") << u"/tmp/x\nExec=evil"_s << u"\"/tmp/x\\nExec=evil\" --background"_s
                                 << u"/tmp/x\\nExec=evil"_s;
    }
    void autostartEntry()
    {
        QFETCH(QString, program);
        QFETCH(QString, exec);
        QFETCH(QString, tryExec);
        const QString entry = Settings::autostartEntry(program);
        QCOMPARE(line(entry, u"Exec"_s), exec);
        QCOMPARE(line(entry, u"TryExec"_s), tryExec);
        QCOMPARE(entry.count("\nExec="_L1), 1);
        QVERIFY(entry.startsWith("[Desktop Entry]\n"_L1));
    }

    void temperatureRange()
    {
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(Settings::filePath());
        const CpuTemperatureDef &def = legionModel().cpuTemperature.value();
        QCOMPARE(Settings::temperatureRange(def), def.defaultRange); // never set
        Settings::setTemperatureRange({40, 85});
        QCOMPARE(Settings::temperatureRange(def), (TemperatureRange{40, 85}));

        // Hand-edited nonsense, or only half of it: the default.
        QSettings(Settings::filePath(), QSettings::IniFormat).setValue(u"cpuTemperature/hottest"_s, u"hot"_s);
        QCOMPARE(Settings::temperatureRange(def), def.defaultRange);
        QSettings(Settings::filePath(), QSettings::IniFormat).remove(u"cpuTemperature/hottest"_s);
        QCOMPARE(Settings::temperatureRange(def), def.defaultRange);
    }

    void startAtLogin()
    {
        QStandardPaths::setTestModeEnabled(true);
        QString error;
        QVERIFY2(Settings::setStartAtLogin(true, &error), qPrintable(error));
        QVERIFY(Settings::startsAtLogin());
        QVERIFY(Settings::autostartPath().endsWith("/autostart/io.github.meluxon.Glowzone.desktop"_L1));
        QVERIFY2(Settings::setStartAtLogin(false, &error), qPrintable(error));
        QVERIFY(!Settings::startsAtLogin());
        QVERIFY(Settings::setStartAtLogin(false, &error)); // already off: fine
    }

    // Setting what is already stored doesn't touch the file: a running
    // Glowzone reads it again on every change.
    void sameValueIsNotWritten()
    {
        Settings::setKeepRunning(true);
        Settings::setTemperatureRange({30, 90});
        Settings::setLinked(u"front"_s, false);
        const QDateTime written = QFileInfo(Settings::filePath()).lastModified();
        QTest::qWait(20);
        Settings::setKeepRunning(true);
        Settings::setTemperatureRange({30, 90});
        Settings::setLinked(u"front"_s, false);
        QCOMPARE(QFileInfo(Settings::filePath()).lastModified(), written);
        Settings::setKeepRunning(false);
        QVERIFY(QFileInfo(Settings::filePath()).lastModified() != written);
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "settingstest.moc"
