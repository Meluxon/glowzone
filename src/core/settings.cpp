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

// The file is opened per call: the window and the CLI may both change it.

std::optional<bool> linked(const QString &fanId)
{
    QSettings s(filePath(), QSettings::IniFormat);
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
    QSettings s(filePath(), QSettings::IniFormat);
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
    QSettings s(filePath(), QSettings::IniFormat);
    return s.value(QLatin1String("window/size")).toSize();
}

void setWindowSize(QSize size)
{
    QSettings s(filePath(), QSettings::IniFormat);
    s.setValue(QLatin1String("window/size"), size);
}

} // namespace Glowzone::Settings
