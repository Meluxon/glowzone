// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Glowzone::Gui
{

// Shows the main window (or a message if the PC isn't supported) and runs the
// event loop. Needs a QApplication. In the background (login start): no
// window, only the tray – if running in the background is switched on.
int run(bool background);

} // namespace Glowzone::Gui
