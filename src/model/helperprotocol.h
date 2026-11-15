// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Contract between the app and the root helper. The helper returns the error
// codes below; the app turns them into translated messages (the helper runs
// without the user's locale).

#include <QList>
#include <QVariantMap>

namespace Glowzone
{

// Values as GameZone GetLED/SetLED use them (see EcLights::modes()).
struct EcLightState {
    quint8 index = 0;      // EcLightDef::index
    quint8 mode = 0;
    quint8 brightness = 0;
    bool operator==(const EcLightState &) const = default;
};

struct EcRequest {
    quint8 index = 0;
    quint8 mode = 0;       // 0 = leave unchanged
    quint8 brightness = 0; // 0 = leave unchanged
};

namespace HelperProtocol
{

// Action names: <helper id>.<slot name>
inline constexpr const char *ActionStatus = "status";
inline constexpr const char *ActionSetLed = "setled";
inline constexpr const char *ActionLayout = "layout"; // fan layout as the BIOS detects it

// Argument / reply keys
inline constexpr const char *KeyLights = "lights";         // QVariantList of QVariantMap
inline constexpr const char *KeyIndex = "index";           // EcLightDef::index
inline constexpr const char *KeyMode = "mode";
inline constexpr const char *KeyBrightness = "brightness";
inline constexpr const char *KeyDetail = "detail";         // English detail on errors
inline constexpr const char *KeyLayout = "layout";         // IsSupportLightingFeature result

// Kept clear of KAuth's and KJob's own error codes.
enum Error {
    UnsupportedModel = 1000, // DMI does not match any profile
    AcpiCallUnavailable,     // module missing or could not be loaded
    BiosLedMissing,          // IsSupportBIOSLED did not report the expected lights
    InvalidArguments,        // request outside the whitelist
    AcpiCallFailed,          // acpi_call returned an error
    UnexpectedReply,         // reply could not be parsed or did not match
};

QVariantMap statesToVariant(const QList<EcLightState> &lights);
QList<EcLightState> statesFromVariant(const QVariantMap &data);
QVariantMap requestsToVariant(const QList<EcRequest> &requests);

} // namespace HelperProtocol
} // namespace Glowzone
