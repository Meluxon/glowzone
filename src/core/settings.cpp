// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.h"

#include "config-glowzone.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace Glowzone::Settings
{

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/glowzone.conf");
}

// The file is opened per call: the window and the CLI may both change it.

std::optional<bool> linked(const QString &fanId)
{
    const QSettings s(filePath(), QSettings::IniFormat);
    const QVariant v = s.value(QLatin1String("links/") + fanId);
    return v.isValid() ? std::optional<bool>(v.toBool()) : std::nullopt;
}

void setLinked(const QString &fanId, bool isLinked)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QLatin1String("links/") + fanId, isLinked);
}

TemperatureRange temperatureRange(const CpuTemperatureDef &def)
{
    const QSettings s(filePath(), QSettings::IniFormat);
    bool okCoolest = false;
    bool okHottest = false;
    const int coolest = s.value(QLatin1String("cpuTemperature/coolest")).toInt(&okCoolest);
    const int hottest = s.value(QLatin1String("cpuTemperature/hottest")).toInt(&okHottest);
    return okCoolest && okHottest ? TemperatureRange{coolest, hottest} : def.defaultRange;
}

void setTemperatureRange(TemperatureRange range)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QLatin1String("cpuTemperature/coolest"), range.coolest);
    s.setValue(QLatin1String("cpuTemperature/hottest"), range.hottest);
}

QString currentProfile()
{
    const QSettings s(filePath(), QSettings::IniFormat);
    return s.value(QLatin1String("profiles/current")).toString();
}

void setCurrentProfile(const QString &name)
{
    QSettings s(filePath(), QSettings::IniFormat);
    if (name.isEmpty()) {
        s.remove(QLatin1String("profiles/current"));
    } else {
        s.setValue(QLatin1String("profiles/current"), name);
    }
}

QSize windowSize()
{
    const QSettings s(filePath(), QSettings::IniFormat);
    return s.value(QLatin1String("window/size")).toSize();
}

void setWindowSize(QSize size)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QLatin1String("window/size"), size);
}

bool keepRunning()
{
    const QSettings s(filePath(), QSettings::IniFormat);
    return s.value(QLatin1String("background/keepRunning"), false).toBool();
}

void setKeepRunning(bool on)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QLatin1String("background/keepRunning"), on);
}

QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/autostart/") + QLatin1String(GLOWZONE_APP_ID)
        + QLatin1String(".desktop");
}

bool startsAtLogin()
{
    return QFileInfo::exists(autostartPath());
}

// A value of type string in a desktop entry: \ and line breaks escaped.
static QString desktopString(QString value)
{
    value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    value.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    value.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    value.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    return value;
}

// The program as the first Exec argument (Desktop Entry spec, "The Exec key"):
// in double quotes with ", `, $ and \ escaped, % doubled (field codes), and the
// whole thing escaped once more as a string.
static QString execProgram(const QString &path)
{
    QString q = path;
    for (const QChar c : {QLatin1Char('\\'), QLatin1Char('"'), QLatin1Char('`'), QLatin1Char('$')}) {
        q.replace(c, QLatin1Char('\\') + c);
    }
    q.replace(QLatin1Char('%'), QLatin1String("%%"));
    return desktopString(QLatin1Char('"') + q + QLatin1Char('"'));
}

bool setStartAtLogin(bool on, QString *error)
{
    const QString path = autostartPath();
    if (!on) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            *error = i18n("Could not remove %1.", path);
            return false;
        }
        return true;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = i18n("Could not create the directory for %1.", path);
        return false;
    }
    const QByteArray data = autostartEntry(QCoreApplication::applicationFilePath()).toUtf8();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = i18n("Could not write %1: %2", path, f.errorString());
        return false;
    }
    return true;
}

// TryExec: once the program is uninstalled, the session skips the entry
// instead of failing at login.
QString autostartEntry(const QString &program)
{
    const QString comment = i18nc("@info autostart entry", "Keeps the fan lighting running in the background");
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=Glowzone\n"
               "Comment=%1\n"
               "TryExec=%2\n"
               "Exec=%3 --background\n"
               "Icon=%4\n"
               "Terminal=false\n"
               "NoDisplay=true\n"
               "X-GNOME-Autostart-enabled=true\n")
        .arg(desktopString(comment), desktopString(program), execProgram(program), QLatin1String(GLOWZONE_APP_ID));
}

} // namespace Glowzone::Settings
