// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

// The window (and the tray) runs as a single instance registered on the
// session bus; the command line can ask whether it's there.
namespace Glowzone::Instance
{

// The name KDBusService registers: reversed organisation domain + application
// name, e.g. io.github.meluxon.glowzone. Needs KAboutData to be set.
QString serviceName();

// Is the window or the tray running for this user?
bool isRunning();

} // namespace Glowzone::Instance
