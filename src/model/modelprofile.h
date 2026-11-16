// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "dmi.h"

#include <KLazyLocalizedString>
#include <QList>

#include <optional>

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

// A light driven by the EC through GameZone WMI SetLED. Its colour is fixed
// in hardware; the sidebar draws it with these two (0xRRGGBB).
struct EcLightDef {
    const char *id;            // stable key for the CLI and profiles
    KLazyLocalizedString name;
    quint8 index;              // SetLED/GetLED light index
    quint32 colour;            // the light itself
    quint32 glow;              // the glow around it
};

// A fan-layout code from GameZone IsSupportLightingFeature (0x0A). The BIOS
// derives it from the fans' speed signals, so a fan that isn't spinning at that
// moment counts as missing – the LEDs themselves may be fine.
struct FanLayoutDef {
    int code;
    QList<const char *> undetectedFans; // fan ids; empty = all detected
};

// The temperatures the colour list is spread over (whole °C): the first colour
// from coolest, the last one up to hottest. Below it the first colour fades
// towards dark, above it the last one towards white. One per profile, shared
// by every LED that follows the temperature.
struct TemperatureRange {
    int coolest;
    int hottest;
    bool operator==(const TemperatureRange &) const = default;
};

// Fan LEDs that follow the CPU temperature, like Lenovo Vantage's "CPU
// temperature" lighting – but as a mode of its own for each fan LED.
//
// Every 2 s Vantage reads the EC's CPU temperature and picks a colour from a
// list, one per temperature step. The EC's value trails the CPU package
// temperature, moving a few °C per second. Reading the EC needs root, so
// Glowzone reads the CPU's own sensor instead (hwmon, readable by any user)
// and imitates the EC: the average of the last few readings plus an offset,
// moving by at most a few °C per second.
struct CpuTemperatureDef {
    const char *hwmonName;       // hwmon driver, e.g. "coretemp"
    const char *sensorLabel;     // tempN_label of the sensor to use, e.g. "Package id 0"
    double offset;               // °C added to the reading (the EC reads a little higher)
    int averageSamples;          // readings averaged
    double maxChangePerSecond;   // °C the shown temperature may move per second
    int intervalMs;              // how often the sensor is read
    double hysteresis;           // °C below a step's start before going back down (Glowzone's own)
    QList<quint32> colours;      // 0xRRGGBB, coolest first
    TemperatureRange defaultRange;
    int darkAt;                  // °C where the fade below the range is darkest (the room, roughly)
    double darkest;              // brightness there, as seen (0–1)
    int hottestLimit;            // °C where the fade above the range is white, if the sensor gives no limit
};
// Mode value Glowzone uses for "follows the CPU temperature". It is not a value
// of the fan controller and is never sent to it: on the wire the LED is set
// to static with the colour for the current temperature.
inline constexpr quint8 CpuTemperatureMode = 0xF0;

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

    // Fan LEDs following the CPU temperature (nullopt: the model doesn't offer it).
    std::optional<CpuTemperatureDef> cpuTemperature;

    bool matches(const DmiInfo &dmi) const;
    QString displayName() const;   // vendor and product version, as DMI reports them
    const FanDef *fan(const QString &id) const;
    const EcLightDef *ecLight(const QString &id) const;
    const EcLightDef *ecLightByIndex(quint8 index) const;
    // Fans the BIOS misses with this fan-layout code (empty if none or unknown).
    QList<const FanDef *> undetectedFans(int layout) const;
    bool hasFanMode(quint8 value) const
    {
        return value == CpuTemperatureMode ? cpuTemperature.has_value() : fanModes.contains(value);
    }
    bool hasEcLightMode(quint8 value) const { return ecLightModes.contains(value); }
};

const QList<ModelProfile> &supportedModels();

// nullptr if the machine is not supported.
const ModelProfile *matchModel(const DmiInfo &dmi);

} // namespace Glowzone
