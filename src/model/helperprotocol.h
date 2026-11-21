// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Contract between the app and the root helper. The helper returns the error
// codes below; the app turns them into translated messages (the helper runs
// without the user's locale).

#include <QLatin1StringView>
#include <QList>
#include <QVariantMap>

namespace Glowzone
{

// Values as GameZone GetLED/SetLED use them (see EcLights::modes()).
struct EcLightState {
    quint8 index = 0;      // EcLightDef::index
    quint8 mode = 0;       // in a request: 0 = leave unchanged
    quint8 brightness = 0; // in a request: 0 = leave unchanged
    bool operator==(const EcLightState &) const = default;
};
using EcRequest = EcLightState; // what a light should become

namespace HelperProtocol
{

// Action names: <helper id>.<slot name>
inline constexpr QLatin1StringView ActionStatus{"status"};
inline constexpr QLatin1StringView ActionSetLed{"setled"};
inline constexpr QLatin1StringView ActionLayout{"layout"}; // fan layout as the BIOS detects it

// Argument / reply keys
inline constexpr QLatin1StringView KeyLights{"lights"};         // QVariantList of QVariantMap
inline constexpr QLatin1StringView KeyIndex{"index"};           // EcLightDef::index
inline constexpr QLatin1StringView KeyMode{"mode"};
inline constexpr QLatin1StringView KeyBrightness{"brightness"};
inline constexpr QLatin1StringView KeyLayout{"layout"};         // IsSupportLightingFeature result

// Kept clear of KAuth's and KJob's own error codes.
enum Error {
    UnsupportedModel = 1000, // DMI does not match any profile
    AcpiCallUnavailable,     // module missing or could not be loaded
    BiosLedMissing,          // IsSupportBIOSLED did not report the expected lights
    InvalidArguments,        // request outside the whitelist
    AcpiCallFailed,          // acpi_call returned an error
    UnexpectedReply,         // reply could not be parsed or did not match
};

// Lights or requests: {"lights": [{"index", "mode", "brightness"}, ...]}.
// An error reply carries an English detail as its error description.
QVariantMap lightsToVariant(const QList<EcLightState> &lights);
QList<EcLightState> lightsFromVariant(const QVariantMap &data);

} // namespace HelperProtocol
} // namespace Glowzone
