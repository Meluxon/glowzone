// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "dmi.h"

#include <KLazyLocalizedString>
#include <QList>

namespace Glowzone
{

// One LED of a fan, on its own ITE 8297 board header.
struct LedDef {
    quint8 header;             // ITE 8297 zone ID
    const char *id;            // "upper", "rear", ... (nullptr for a single-LED fan)
    KLazyLocalizedString name; // where it sits, shown to the user
};

// One fan as the user sees it. A "double" fan has two LEDs on two board headers,
// and each can be coloured on its own.
struct FanDef {
    const char *id;            // stable key for the CLI and presets, e.g. "cpu"
    KLazyLocalizedString name; // shown to the user
    QList<LedDef> leds;

    bool isDouble() const { return leds.size() > 1; }
    QList<quint8> headers() const;
    // By id ("upper") or 1-based number ("1"); nullptr if no such LED.
    const LedDef *led(const QString &idOrNumber) const;
};

// A light driven by the EC through GameZone WMI SetLED.
struct EcLightDef {
    const char *id;            // "logo", "ambient"
    KLazyLocalizedString name;
    quint8 index;              // SetLED/GetLED light index
};

// A fan-layout code from GameZone IsSupportLightingFeature (0x0A). The BIOS
// derives it from the fans' speed signals, so a fan that isn't spinning at that
// moment counts as missing – the LEDs themselves may be fine.
struct FanLayoutDef {
    int code;
    QList<const char *> undetectedFans; // fan ids; empty = all detected
};

// Everything Glowzone needs to know about one PC model. A machine is supported
// only if vendor, product version and board name all match exactly.
struct ModelProfile {
    const char *vendor;
    const char *productVersion;
    const char *boardName;

    quint16 usbVendorId;       // ITE 8297 fan LED controller
    quint16 usbProductId;
    QList<FanDef> fans;

    const char *acpiGameZone;  // ACPI path of the GameZone WMI method
    int biosLedPresence;       // expected IsSupportBIOSLED (0x0E) result
    QList<EcLightDef> ecLights;

    int normalFanLayout;              // IsSupportLightingFeature (0x0A) with all fans present
    QList<FanLayoutDef> fanLayouts;   // known other codes

    // What this model offers, as Lenovo's own software shows it. The protocol
    // code knows every value it can encode (Ite8297::modes(), EcLights::modes());
    // a model uses only the ones listed here. Each list must contain "off".
    QList<quint8> fanModes;           // ITE 8297 mode values
    quint8 fanLevels;                 // fan speed and brightness: 1 to fanLevels
    QList<quint8> ecLightModes;       // SetLED mode values
    quint8 ecBrightnessLevels;        // SetLED brightness: 1 to ecBrightnessLevels
    quint8 ecDefaultBrightness;       // factory setting, used by "reset"

    bool matches(const DmiInfo &dmi) const;
    QString displayName() const;   // vendor and product version, as DMI reports them
    const FanDef *fan(const QString &id) const;
    const EcLightDef *ecLight(const QString &id) const;
    const EcLightDef *ecLightByIndex(quint8 index) const;
    bool hasFanMode(quint8 value) const { return fanModes.contains(value); }
    bool hasEcLightMode(quint8 value) const { return ecLightModes.contains(value); }
};

const QList<ModelProfile> &supportedModels();

// nullptr if the machine is not supported.
const ModelProfile *matchModel(const DmiInfo &dmi);

} // namespace Glowzone
