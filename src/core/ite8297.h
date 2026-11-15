// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// ITE 8297 fan LED controller as Lenovo uses it (USB ID per model, see ModelProfile).
// HID feature report 0xCC, 64 bytes. One settings report per header, then one
// commit per header group. The controller cannot be read back.

#include "model/modelprofile.h"

#include <KLazyLocalizedString>
#include <QByteArray>
#include <QList>
#include <optional>

namespace Glowzone::Ite8297
{

inline constexpr quint8 ReportId = 0xCC;
inline constexpr int ReportSize = 64;

// The HID interface that takes these reports (the device may have others).
inline constexpr quint16 UsagePage = 0xFF89;
inline constexpr quint16 Usage = 0xCC;

// Mode byte of the settings report. Assumption: the value means the same effect
// on every Lenovo model with this controller (true for every model known so
// far); a model only offers a subset. A model whose firmware numbers its effects
// differently would need a per-model mapping instead (see ModelProfile).
enum class Mode : quint8 {
    Off = 0x00,
    Static = 0x01,
    Sparkle = 0x02,
    Breathing = 0x03,
    Wave = 0x04,
    Specter = 0x06,
    Rainbow = 0x09,
    Random = 0x0A,
    // Glowzone's own: follows the CPU temperature. Never sent to the
    // controller – resolve() turns it into Static with the matching colour.
    CpuTemperature = CpuTemperatureMode,
};

struct ModeInfo {
    Mode mode;                 // value sent to the controller
    const char *id;            // stable key for the CLI and saved state/profiles – never change it
    KLazyLocalizedString name; // shown to the user (context "@item fan LED mode")
    bool usesColour;           // false: the colour bytes are ignored (e.g. rainbow)
    bool usesSpeed;            // false: the speed byte is ignored (e.g. static)
    bool followsTemperature;   // true: the colour comes from the CPU temperature
};

// Every mode the controller is known to accept. A model offers a subset
// (ModelProfile::fanModes); use modesFor() for anything shown to the user.
const QList<ModeInfo> &modes();
QList<ModeInfo> modesFor(const ModelProfile &model);
const ModeInfo *modeInfo(Mode mode);
const ModeInfo *modeInfo(const QString &id);

// False for Glowzone's own modes (CPU temperature), which the controller doesn't know.
bool isControllerMode(Mode mode);

// Speed and brightness start at 1; the top level is per model (ModelProfile::fanLevels).
inline constexpr quint8 MinLevel = 1;

struct Rgb {
    quint8 r = 0, g = 0, b = 0;
    bool operator==(const Rgb &) const = default;
};

// "#rrggbb" or "rrggbb"
std::optional<Rgb> parseRgb(const QString &text);
QString rgbToString(Rgb c); // "#rrggbb"

struct HeaderSetting {
    quint8 header = 0;
    Mode mode = Mode::Off;
    quint8 speed = 1;
    quint8 brightness = 2;
    Rgb colour;
    bool operator==(const HeaderSetting &) const = default;
};

// What goes to the controller for a setting: a CPU-temperature LED becomes
// static in the given colour (at the lowest speed, as Vantage sends it);
// anything else is returned as it is.
HeaderSetting resolve(HeaderSetting setting, Rgb temperatureColour);

// The values used for a header Glowzone hasn't set yet: off, but white and
// at the lowest speed, so choosing a lit mode shows something sensible.
HeaderSetting initialSetting(quint8 header);

enum class Group { A, B };

std::optional<Group> groupOf(quint8 header);
bool isKnownHeader(quint8 header);

QByteArray settingsReport(HeaderSetting s);
QByteArray commitReport(Group g);

// The full sequence for a batch: settings reports, then one commit per group
// touched. Settings for unknown headers or in a mode the controller doesn't
// know (resolve() them first) are left out.
QList<QByteArray> reportsFor(const QList<HeaderSetting> &settings);

} // namespace Glowzone::Ite8297
