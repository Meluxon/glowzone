// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.h"

#include <QSettings>
#include <QStandardPaths>

namespace Glowzone::Settings
{

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/glowzone.conf");
}

std::optional<bool> linked(const QString &fanId)
{
    const QVariant v = QSettings(filePath(), QSettings::IniFormat).value(QLatin1String("links/") + fanId);
    return v.isValid() ? std::optional<bool>(v.toBool()) : std::nullopt;
}

void setLinked(const QString &fanId, bool linked)
{
    QSettings(filePath(), QSettings::IniFormat).setValue(QLatin1String("links/") + fanId, linked);
}

QString currentProfile()
{
    return QSettings(filePath(), QSettings::IniFormat).value(QLatin1String("profiles/current")).toString();
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
    return QSettings(filePath(), QSettings::IniFormat).value(QLatin1String("window/size")).toSize();
}

void setWindowSize(const QSize &size)
{
    QSettings(filePath(), QSettings::IniFormat).setValue(QLatin1String("window/size"), size);
}

} // namespace Glowzone::Settings
