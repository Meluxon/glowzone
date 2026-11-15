// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/dmi.h"

namespace Glowzone
{

// Shown by the CLI and the window when the PC matches no model profile.
QString unsupportedModelMessage(const DmiInfo &dmi);

} // namespace Glowzone
