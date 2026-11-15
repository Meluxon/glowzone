// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/helperprotocol.h"
#include "model/modelprofile.h"

#include <KLazyLocalizedString>
#include <optional>

class QWindow;

namespace KAuth
{
class ExecuteJob;
}

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

// Both calls go through the root helper (KAuth). Errors are translated.
// Blocking versions, for the CLI:
std::optional<QList<EcLightState>> status(QString *error);
std::optional<QList<EcLightState>> set(const QList<EcRequest> &requests, QString *error);

// Asynchronous versions, for the GUI: start() the job, then read the outcome
// with result() in its KJob::result handler.
// parent: the window a polkit dialog would belong to (nullptr for the CLI).
KAuth::ExecuteJob *statusJob(QWindow *parent = nullptr);
KAuth::ExecuteJob *setJob(const QList<EcRequest> &requests, QWindow *parent = nullptr);
std::optional<QList<EcLightState>> result(KAuth::ExecuteJob *job, QString *error);

// Fan layout as the BIOS detects it right now (see FanLayoutDef).
KAuth::ExecuteJob *layoutJob(QWindow *parent = nullptr);
std::optional<int> layoutResult(KAuth::ExecuteJob *job, QString *error);
std::optional<int> layout(QString *error); // blocking, for the CLI

// "0x0b"
QString layoutCode(int code);
// Empty if the code is the model's normal one, else a translated explanation.
QString layoutWarning(const ModelProfile &model, int code);

} // namespace Glowzone::EcLights
