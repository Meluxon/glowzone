// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acpibackend.h"

namespace Glowzone
{

// /proc/acpi/call from the acpi_call kernel module. Root only.
class ProcAcpiCall : public AcpiBackend
{
public:
    bool ensureReady(QString *detail) override;
    std::optional<QByteArray> call(const QByteArray &command, QString *detail) override;

    // acpi_call returns its whole result buffer: the reply, a NUL, then stale
    // bytes from earlier contents (e.g. "0x3\0called" left over from
    // "not called"). Only the text before the first NUL is the reply.
    static QByteArray cleanReply(QByteArray raw);
};

} // namespace Glowzone
