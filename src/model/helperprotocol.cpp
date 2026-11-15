// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "helperprotocol.h"

namespace Glowzone::HelperProtocol
{

static QVariantMap entry(quint8 index, quint8 mode, quint8 brightness)
{
    return {
        {QLatin1String(KeyIndex), uint(index)},
        {QLatin1String(KeyMode), uint(mode)},
        {QLatin1String(KeyBrightness), uint(brightness)},
    };
}

QVariantMap statesToVariant(const QList<EcLightState> &lights)
{
    QVariantList list;
    for (const EcLightState &s : lights) {
        list.append(entry(s.index, s.mode, s.brightness));
    }
    return {{QLatin1String(KeyLights), list}};
}

QList<EcLightState> statesFromVariant(const QVariantMap &data)
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

QVariantMap requestsToVariant(const QList<EcRequest> &requests)
{
    QVariantList list;
    for (const EcRequest &r : requests) {
        list.append(entry(r.index, r.mode, r.brightness));
    }
    return {{QLatin1String(KeyLights), list}};
}

} // namespace Glowzone::HelperProtocol
