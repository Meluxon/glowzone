// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/helperprotocol.h"
#include "model/modelprofile.h"

#include <KLazyLocalizedString>
#include <optional>

namespace Glowzone::EcLights
{

// Modes of the EC-driven case lights (GameZone SetLED values).
struct ModeInfo {
    quint8 value;
    const char *id;
    KLazyLocalizedString name;
};

// Every SetLED mode Glowzone knows. A model offers a subset
// (ModelProfile::ecLightModes); use modesFor() for anything shown to the user.
const QList<ModeInfo> &modes();
QList<ModeInfo> modesFor(const ModelProfile &model);
const ModeInfo *modeInfo(quint8 value);
const ModeInfo *modeInfo(const QString &id);

inline constexpr quint8 ModeOff = 1;
// Brightness starts at 1; the top level is per model (ModelProfile::ecBrightnessLevels).
inline constexpr quint8 MinBrightness = 1;

// Blocking calls to the root helper, for the CLI (the window uses LightApplier).
// Errors are translated.
std::optional<QList<EcLightState>> status(QString *error);
std::optional<QList<EcLightState>> set(const QList<EcRequest> &requests, QString *error);
std::optional<int> layout(QString *error); // fan layout as the BIOS detects it now (see FanLayoutDef)

// "0x0b"
QString layoutCode(int code);
// Empty if the code is the model's normal one, else a translated explanation.
QString layoutWarning(const ModelProfile &model, int code);

} // namespace Glowzone::EcLights
