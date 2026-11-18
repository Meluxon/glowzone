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

using namespace Qt::StringLiterals;

namespace Glowzone::Settings
{

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/glowzone.conf"_L1;
}

// The file is opened per call: the window and the CLI may both change it.

std::optional<bool> linked(const QString &fanId)
{
    const QSettings s(filePath(), QSettings::IniFormat);
    const QVariant v = s.value("links/"_L1 + fanId);
    return v.isValid() ? std::optional<bool>(v.toBool()) : std::nullopt;
}

void setLinked(const QString &fanId, bool isLinked)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue("links/"_L1 + fanId, isLinked);
}

TemperatureRange temperatureRange(const CpuTemperatureDef &def)
{
    const QSettings s(filePath(), QSettings::IniFormat);
    bool okCoolest = false;
    bool okHottest = false;
    const int coolest = s.value("cpuTemperature/coolest"_L1).toInt(&okCoolest);
    const int hottest = s.value("cpuTemperature/hottest"_L1).toInt(&okHottest);
    return okCoolest && okHottest ? TemperatureRange{coolest, hottest} : def.defaultRange;
}

void setTemperatureRange(TemperatureRange range)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue("cpuTemperature/coolest"_L1, range.coolest);
    s.setValue("cpuTemperature/hottest"_L1, range.hottest);
}

QString currentProfile()
{
    const QSettings s(filePath(), QSettings::IniFormat);
    return s.value("profiles/current"_L1).toString();
}

void setCurrentProfile(const QString &name)
{
    QSettings s(filePath(), QSettings::IniFormat);
    if (name.isEmpty()) {
        s.remove("profiles/current"_L1);
    } else {
        s.setValue("profiles/current"_L1, name);
    }
}

QByteArray windowGeometry()
{
    const QSettings s(filePath(), QSettings::IniFormat);
    return s.value("window/geometry"_L1).toByteArray();
}

void setWindowGeometry(const QByteArray &geometry)
{
    QSettings s(filePath(), QSettings::IniFormat);
    if (s.value("window/geometry"_L1).toByteArray() != geometry) {
        s.setValue("window/geometry"_L1, geometry);
    }
}

bool keepRunning()
{
    const QSettings s(filePath(), QSettings::IniFormat);
    return s.value("background/keepRunning"_L1, false).toBool();
}

void setKeepRunning(bool on)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue("background/keepRunning"_L1, on);
}

QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/"_L1 + QLatin1String(GLOWZONE_APP_ID)
        + ".desktop"_L1;
}

bool startsAtLogin()
{
    return QFileInfo::exists(autostartPath());
}

// A value of type string in a desktop entry: \ and line breaks escaped.
static QString desktopString(QString value)
{
    value.replace(u'\\', "\\\\"_L1);
    value.replace(u'\n', "\\n"_L1);
    value.replace(u'\r', "\\r"_L1);
    value.replace(u'\t', "\\t"_L1);
    return value;
}

// The program as the first Exec argument (Desktop Entry spec, "The Exec key"):
// in double quotes with ", `, $ and \ escaped, % doubled (field codes), and the
// whole thing escaped once more as a string.
static QString execProgram(const QString &path)
{
    QString q = path;
    for (const QChar c : {u'\\', u'"', u'`', u'$'}) {
        q.replace(c, u'\\' + c);
    }
    q.replace(u'%', "%%"_L1);
    return desktopString(u'"' + q + u'"');
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
    return u"[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Glowzone\n"
           "Comment=%1\n"
           "TryExec=%2\n"
           "Exec=%3 --background\n"
           "Icon=%4\n"
           "Terminal=false\n"
           "NoDisplay=true\n"
           "X-GNOME-Autostart-enabled=true\n"_s
        .arg(desktopString(comment), desktopString(program), execProgram(program), QLatin1String(GLOWZONE_APP_ID));
}

} // namespace Glowzone::Settings
