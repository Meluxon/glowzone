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
            {KeyIndex, uint(l.index)},
            {KeyMode, uint(l.mode)},
            {KeyBrightness, uint(l.brightness)},
        });
    }
    return {{KeyLights, list}};
}

QList<EcLightState> lightsFromVariant(const QVariantMap &data)
{
    QList<EcLightState> lights;
    const QVariantList list = data.value(KeyLights).toList();
    for (const QVariant &item : list) {
        const QVariantMap m = item.toMap();
        lights.append({quint8(m.value(KeyIndex).toUInt()),
                       quint8(m.value(KeyMode).toUInt()),
                       quint8(m.value(KeyBrightness).toUInt())});
    }
    return lights;
}

} // namespace Glowzone::HelperProtocol
