// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "dmi.h"

#include <QFile>

using namespace Qt::StringLiterals;

namespace Glowzone
{

static QString readField(const char *name)
{
    QFile f("/sys/class/dmi/id/"_L1 + QLatin1String(name));
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
