// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace Glowzone
{

// Raw access to ACPI methods. Production uses /proc/acpi/call; tests use a fake.
// Only EcService builds the command strings – callers never pass their own.
class AcpiBackend
{
public:
    virtual ~AcpiBackend() = default;

    // Makes sure calls are possible (e.g. loads the acpi_call module).
    virtual bool ensureReady(QString *detail) = 0;

    // Runs one call and returns acpi_call's reply text, e.g. "0x3" or "{0x01, 0x02, ...}".
    virtual std::optional<QByteArray> call(const QByteArray &command, QString *detail) = 0;
};

} // namespace Glowzone
