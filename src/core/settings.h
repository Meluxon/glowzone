// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QSize>
#include <QString>
#include <optional>

// ~/.config/glowzone/glowzone.conf, shared by the window and the CLI.
namespace Glowzone::Settings
{

QString filePath();

// Linked / split state of a double fan; nullopt if never chosen.
std::optional<bool> linked(const QString &fanId);
void setLinked(const QString &fanId, bool isLinked);

// The profile applied last (empty if none, or if it was deleted).
QString currentProfile();
void setCurrentProfile(const QString &name);

// Window size, restored on the next start (empty if never stored).
QSize windowSize();
void setWindowSize(QSize size);

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
