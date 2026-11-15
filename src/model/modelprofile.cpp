// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "modelprofile.h"

namespace Glowzone
{

const QList<ModelProfile> &supportedModels()
{
    // To add a model: capture Vantage on Windows, map every header and mode,
    // and add a profile here. There is deliberately no way to run on an unknown model.
    static const QList<ModelProfile> models = {
        {
            .vendor = "LENOVO",
            .productVersion = "Legion T5 26IOB6",
            .boardName = "3733",
            .usbVendorId = 0x17ef,
            .usbProductId = 0xc955,
            .fans = {
                {"cpu", kli18nc("@item fan", "CPU fan"), {{0x13, nullptr, {}}}},
                {"rear", kli18nc("@item fan", "Rear fan"), {{0x11, nullptr, {}}}},
                // Positions identified on the machine (red/blue test, 2026-11-15).
                {"front",
                 kli18nc("@item fan", "Front fans"),
                 {{0x3a, "upper", kli18nc("@item fan position", "upper")},
                  {0x38, "lower", kli18nc("@item fan position", "lower")}}},
                {"top",
                 kli18nc("@item fan", "Top fans"),
                 {{0x39, "front", kli18nc("@item fan position", "front")},
                  {0x12, "rear", kli18nc("@item fan position", "rear")}}},
            },
            .acpiGameZone = "\\WMI2.WMG0",
            .biosLedPresence = 0x3,
            .ecLights = {
                {"logo", kli18nc("@item light", "Front logo"), 1},
                {"ambient", kli18nc("@item light", "Internal light"), 2},
            },
            // Decoded from the DSDT (WMG0, method 0x0A) for this cooler type
            // (COL2/COL3); 0x0B observed with all fans present.
            .normalFanLayout = 0x0B,
            .fanLayouts = {
                {0x0C, {"top"}},
                {0x0D, {"front", "top"}},
            },
            // Vantage on this model: every mode below confirmed on the machine
            // (off, static, sparkle, breathing, wave, specter, rainbow, random),
            // three speed and brightness levels.
            .fanModes = {0x00, 0x01, 0x02, 0x03, 0x04, 0x06, 0x09, 0x0A},
            .fanLevels = 3,
            // Off, on, blink fast, blink slow, breathing; brightness 1–4,
            // found at 3 (both lights off) on the machine.
            .ecLightModes = {1, 2, 3, 4, 5},
            .ecBrightnessLevels = 4,
            .ecDefaultBrightness = 3,
        },
    };
    return models;
}

QList<quint8> FanDef::headers() const
{
    QList<quint8> list;
    for (const LedDef &l : leds) {
        list.append(l.header);
    }
    return list;
}

const LedDef *FanDef::led(const QString &idOrNumber) const
{
    bool isNumber = false;
    const int n = idOrNumber.toInt(&isNumber);
    if (isNumber) {
        return (n >= 1 && n <= leds.size()) ? &leds.at(n - 1) : nullptr;
    }
    for (const LedDef &l : leds) {
        if (l.id && idOrNumber == QLatin1String(l.id)) {
            return &l;
        }
    }
    return nullptr;
}

bool ModelProfile::matches(const DmiInfo &dmi) const
{
    return dmi.vendor == QLatin1String(vendor) && dmi.productVersion == QLatin1String(productVersion)
        && dmi.boardName == QLatin1String(boardName);
}

QString ModelProfile::displayName() const
{
    return QLatin1String(vendor) + QLatin1Char(' ') + QLatin1String(productVersion);
}

const FanDef *ModelProfile::fan(const QString &id) const
{
    for (const FanDef &f : fans) {
        if (id == QLatin1String(f.id)) {
            return &f;
        }
    }
    return nullptr;
}

const EcLightDef *ModelProfile::ecLight(const QString &id) const
{
    for (const EcLightDef &l : ecLights) {
        if (id == QLatin1String(l.id)) {
            return &l;
        }
    }
    return nullptr;
}

const EcLightDef *ModelProfile::ecLightByIndex(quint8 index) const
{
    for (const EcLightDef &l : ecLights) {
        if (l.index == index) {
            return &l;
        }
    }
    return nullptr;
}

const ModelProfile *matchModel(const DmiInfo &dmi)
{
    for (const ModelProfile &m : supportedModels()) {
        if (m.matches(dmi)) {
            return &m;
        }
    }
    return nullptr;
}

} // namespace Glowzone
