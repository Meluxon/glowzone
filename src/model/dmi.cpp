// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "dmi.h"

#include <QFile>

namespace Glowzone
{

static QString readField(const char *name)
{
    QFile f(QLatin1String("/sys/class/dmi/id/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.read(256)).trimmed();
}

DmiInfo DmiInfo::readSystem()
{
    return {readField("sys_vendor"), readField("product_version"), readField("board_name")};
}

} // namespace Glowzone
