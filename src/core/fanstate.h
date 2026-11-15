// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ite8297.h"
#include "model/modelprofile.h"

#include <QJsonObject>

#include <QMap>

namespace Glowzone
{

// The fan controller can't be read back, so Glowzone remembers what it last
// sent, per header, in ~/.local/state/glowzone/fans.json.
namespace FanState
{

using Headers = QMap<quint8, Ite8297::HeaderSetting>;

QString filePath();
Headers load(const ModelProfile &model);         // only what fits the model; empty if nothing saved yet
bool save(const Headers &headers, QString *error);
bool merge(const QList<Ite8297::HeaderSetting> &changed, QString *error);

// {"0x13": {"mode": "rainbow", "speed": 1, "brightness": 2, "colour": "#158ddd"}, ...}
// When reading, anything the model doesn't offer (header, mode, level) is dropped.
QJsonObject headersToJson(const Headers &headers);
Headers headersFromJson(const QJsonObject &json, const ModelProfile &model);
bool fitsModel(const ModelProfile &model, const Ite8297::HeaderSetting &setting);

// Link toggle default when nothing is stored: linked unless the saved state
// shows the LEDs of a double fan differ.
bool defaultLinked(const FanDef &fan, const Headers &saved);

} // namespace FanState
} // namespace Glowzone
