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

#include <utility>

using namespace Qt::StringLiterals;

namespace Glowzone::Settings
{

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/glowzone.conf"_L1;
}

// The file is opened per call: the window and the CLI may both change it.
static QSettings file()
{
    return QSettings(filePath(), QSettings::IniFormat);
}

// Written only when a value changes: each write makes a running Glowzone read
// the settings again. Values that belong together go in one write, so nobody
// reads half of them. (The file stores text: compare as the value's type.)
static void store(const QList<std::pair<QString, QVariant>> &values)
{
    QSettings s = file();
    for (const auto &[key, value] : values) {
        QVariant stored = s.value(key);
        if (!stored.isValid() || !stored.convert(value.metaType()) || stored != value) {
            s.setValue(key, value);
        }
    }
}

static constexpr auto LinksGroup = "links/"_L1;
static constexpr auto CoolestKey = "cpuTemperature/coolest"_L1;
static constexpr auto HottestKey = "cpuTemperature/hottest"_L1;
static constexpr auto CurrentProfileKey = "profiles/current"_L1;
static constexpr auto WindowGeometryKey = "window/geometry"_L1;
static constexpr auto KeepRunningKey = "background/keepRunning"_L1;

std::optional<bool> linked(const QString &fanId)
{
    const QVariant v = file().value(LinksGroup + fanId);
    return v.isValid() ? std::optional(v.toBool()) : std::nullopt;
}

void setLinked(const QString &fanId, bool isLinked)
{
    store({{LinksGroup + fanId, isLinked}});
}

TemperatureRange temperatureRange(const CpuTemperatureDef &def)
{
    const QSettings s = file();
    bool okCoolest = false;
    bool okHottest = false;
    const int coolest = s.value(CoolestKey).toInt(&okCoolest);
    const int hottest = s.value(HottestKey).toInt(&okHottest);
    return okCoolest && okHottest ? TemperatureRange{coolest, hottest} : def.defaultRange;
}

void setTemperatureRange(TemperatureRange range)
{
    store({{CoolestKey, range.coolest}, {HottestKey, range.hottest}});
}

void setApplied(const QMap<QString, bool> &links, std::optional<TemperatureRange> range, const std::optional<QString> &profile)
{
    QList<std::pair<QString, QVariant>> values;
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        values.append({LinksGroup + it.key(), it.value()});
    }
    if (range) {
        values.append({CoolestKey, range->coolest});
        values.append({HottestKey, range->hottest});
    }
    if (profile) {
        values.append({CurrentProfileKey, *profile});
    }
    store(values);
}

QString currentProfile()
{
    return file().value(CurrentProfileKey).toString();
}

void setCurrentProfile(const QString &name)
{
    if (name.isEmpty()) {
        file().remove(CurrentProfileKey);
    } else {
        store({{CurrentProfileKey, name}});
    }
}

QByteArray windowGeometry()
{
    return file().value(WindowGeometryKey).toByteArray();
}

void setWindowGeometry(const QByteArray &geometry)
{
    store({{WindowGeometryKey, geometry}});
}

bool keepRunning()
{
    return file().value(KeepRunningKey, false).toBool();
}

void setKeepRunning(bool on)
{
    store({{KeepRunningKey, on}});
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
