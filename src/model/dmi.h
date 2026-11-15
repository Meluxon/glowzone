// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

namespace Glowzone
{

// The three DMI fields a model profile is matched on.
struct DmiInfo {
    QString vendor;          // /sys/class/dmi/id/sys_vendor
    QString productVersion;  // /sys/class/dmi/id/product_version (Lenovo puts the model name here)
    QString boardName;       // /sys/class/dmi/id/board_name

    // Reads the running machine's DMI data. These files are world-readable.
    static DmiInfo readSystem();
};

} // namespace Glowzone
