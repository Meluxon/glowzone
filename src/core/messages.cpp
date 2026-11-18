// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "messages.h"
#include "model/modelprofile.h"

#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace Glowzone
{

QString unsupportedModelMessage(const DmiInfo &dmi)
{
    auto orUnknown = [](const QString &s) {
        return s.isEmpty() ? i18nc("@info unknown DMI value", "unknown") : s;
    };
    QStringList supported;
    for (const ModelProfile &m : supportedModels()) {
        supported << i18nc("@item model, board", "%1 (board %2)", m.displayName(), QLatin1String(m.boardName));
    }
    return i18n("This computer is not supported (vendor: %1, model: %2, board: %3).\nGlowzone only runs on: %4",
                orUnknown(dmi.vendor),
                orUnknown(dmi.productVersion),
                orUnknown(dmi.boardName),
                supported.join(", "_L1));
}

} // namespace Glowzone
