// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "helperprotocol.h"

namespace Glowzone::HelperProtocol
{

QVariantMap lightsToVariant(const QList<EcLightState> &lights)
{
    QVariantList list;
    for (const EcLightState &l : lights) {
        list.append(QVariantMap{
            {QLatin1String(KeyIndex), uint(l.index)},
            {QLatin1String(KeyMode), uint(l.mode)},
            {QLatin1String(KeyBrightness), uint(l.brightness)},
        });
    }
    return {{QLatin1String(KeyLights), list}};
}

QList<EcLightState> lightsFromVariant(const QVariantMap &data)
{
    QList<EcLightState> lights;
    const QVariantList list = data.value(QLatin1String(KeyLights)).toList();
    for (const QVariant &item : list) {
        const QVariantMap m = item.toMap();
        lights.append({quint8(m.value(QLatin1String(KeyIndex)).toUInt()),
                       quint8(m.value(QLatin1String(KeyMode)).toUInt()),
                       quint8(m.value(QLatin1String(KeyBrightness)).toUInt())});
    }
    return lights;
}

} // namespace Glowzone::HelperProtocol
