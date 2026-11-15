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

} // namespace Glowzone::Settings
