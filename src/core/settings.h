// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/modelprofile.h"

#include <QMap>
#include <QString>
#include <optional>

// ~/.config/glowzone/glowzone.conf, shared by the window and the CLI. A value
// is written only when it changes.
namespace Glowzone::Settings
{

QString filePath();

// Linked / split state of a double fan; nullopt if never chosen.
std::optional<bool> linked(const QString &fanId);
void setLinked(const QString &fanId, bool isLinked);

// The range the CPU-temperature colours are spread over; the model's default
// if never set. Stored as given: check it with usableRange() before use.
TemperatureRange temperatureRange(const CpuTemperatureDef &def);
void setTemperatureRange(TemperatureRange range);

// The profile applied last (empty if none, or if it was deleted).
QString currentProfile();
void setCurrentProfile(const QString &name);

// What applying a profile records, in one write: the split choices, the range
// and – if given – the profile as the current one.
void setApplied(const QMap<QString, bool> &links, std::optional<TemperatureRange> range, const std::optional<QString> &profile = std::nullopt);

// QWidget::saveGeometry(), restored on the next start (empty if never stored).
QByteArray windowGeometry();
void setWindowGeometry(const QByteArray &geometry);

// Closing the window keeps Glowzone running in the system tray, so fans
// following the CPU temperature keep changing colour. Off by default.
bool keepRunning();
void setKeepRunning(bool on);

// Start in the tray at login: a .desktop file in ~/.config/autostart that
// runs "glowzone --background". The file itself is the setting.
QString autostartPath();
bool startsAtLogin();
// Errors are translated. The entry starts the program that is running now.
bool setStartAtLogin(bool on, QString *error);
// The entry's text for a program path (escaped as the Desktop Entry spec wants).
QString autostartEntry(const QString &program);

} // namespace Glowzone::Settings
