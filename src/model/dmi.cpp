// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "dmi.h"

#include <QFile>

using namespace Qt::StringLiterals;

namespace Glowzone
{

static QString readField(QLatin1StringView name)
{
    QFile f("/sys/class/dmi/id/"_L1 + name);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.read(256)).trimmed();
}

DmiInfo DmiInfo::readSystem()
{
    return {readField("sys_vendor"_L1), readField("product_version"_L1), readField("board_name"_L1)};
}

} // namespace Glowzone
