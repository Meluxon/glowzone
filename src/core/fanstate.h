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

// Held while sending to the controller together with reading or saving the
// state, so a command and a running Glowzone take turns. Otherwise the running
// one could send a temperature colour, by the old state, to an LED the command
// has just set but not saved yet. It's flock() on a file that stays, so taking
// it writes nothing, and the kernel releases it if the holder dies. Advisory:
// after a short wait (a hung process) it goes ahead without it.
class Lock
{
public:
    Lock();
    ~Lock();
    Q_DISABLE_COPY_MOVE(Lock)

private:
    int m_fd = -1;
};

// Replaces the given headers in the saved state and keeps every other entry.
// Only while the lock is held, which the parameter shows.
bool merge(const Lock &lock, const QList<Ite8297::HeaderSetting> &changed, QString *error);

// {"0x13": {"mode": "rainbow", "speed": 1, "brightness": 2, "colour": "#158ddd"}, ...}
// When reading, anything the model doesn't offer (header, mode, level) is dropped.
QJsonObject headersToJson(const Headers &headers);
Headers headersFromJson(const QJsonObject &headers, const ModelProfile &model);
bool fitsModel(const ModelProfile &model, Ite8297::HeaderSetting setting);

// Are the LEDs of a double fan set together? As the user chose (glowzone.conf);
// never chosen: linked unless the saved state shows they differ.
bool linked(const FanDef &fan, const Headers &saved);

} // namespace FanState
} // namespace Glowzone
