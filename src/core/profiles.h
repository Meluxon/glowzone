// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "fanstate.h"
#include "model/helperprotocol.h"
#include "model/modelprofile.h"

#include <QMap>
#include <optional>

namespace Glowzone
{

// A complete lighting setup: every fan LED, the link state of double fans, the
// CPU-temperature range and the case lights. LEDs or lights that weren't known
// when it was saved are simply absent and left alone when it is applied.
struct Profile {
    QString name;
    FanState::Headers fans;
    QMap<QString, bool> links;          // fan id -> linked
    // nullopt: the model has no CPU-temperature mode
    std::optional<TemperatureRange> temperatureRange;
    QMap<quint8, EcLightState> lights;  // light index -> state
};

// One JSON file per profile in ~/.config/glowzone/profiles/, so they're easy to
// back up or share. Names are unique, compared case-insensitively.
namespace Profiles
{

inline constexpr int MaxNameLength = 64;

QString directory();

// Profiles saved for this model, sorted by name.
QList<Profile> loadAll(const ModelProfile &model);
// Not found: nullopt, and error (if given) says so.
std::optional<Profile> find(const ModelProfile &model, const QString &name, QString *error = nullptr);

// Replaces a profile of the same name. Errors are translated.
bool save(const ModelProfile &model, const Profile &profile, QString *error);
bool remove(const ModelProfile &model, const QString &name, QString *error);
// Fails if the new name is taken by another profile (case changes are fine).
bool rename(const ModelProfile &model, const QString &oldName, const QString &newName, QString *error);

// Trimmed name, or empty if unusable.
QString cleanName(const QString &name);
// cleanName(), or nullopt and why it can't be used.
std::optional<QString> checkedName(const QString &name, QString *error);

} // namespace Profiles
} // namespace Glowzone
