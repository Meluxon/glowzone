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

// Exec= quoting (Desktop Entry spec): the program path in double quotes, with
// ", `, $ and \ escaped.
static QString quotedForExec(const QString &path)
{
    QString q = path;
    for (const QChar c : {QLatin1Char('\\'), QLatin1Char('"'), QLatin1Char('`'), QLatin1Char('$')}) {
        q.replace(c, QLatin1Char('\\') + c);
    }
    return QLatin1Char('"') + q + QLatin1Char('"');
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
    const QString entry = QStringLiteral(
                              "[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=Glowzone\n"
                              "Comment=%1\n"
                              "Exec=%2 --background\n"
                              "Icon=%3\n"
                              "Terminal=false\n"
                              "NoDisplay=true\n"
                              "X-GNOME-Autostart-enabled=true\n")
                              .arg(i18nc("@info autostart entry", "Keeps the fan lighting running in the background"),
                                   quotedForExec(QCoreApplication::applicationFilePath()),
                                   QLatin1String(GLOWZONE_APP_ID));
    const QByteArray data = entry.toUtf8();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = i18n("Could not write %1: %2", path, f.errorString());
        return false;
    }
    return true;
}

} // namespace Glowzone::Settings
