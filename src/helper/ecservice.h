// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acpibackend.h"
#include "model/helperprotocol.h"
#include "model/modelprofile.h"

#include <QList>
#include <QVariantMap>

namespace Glowzone
{

// The EC light logic of the root helper. Every request goes through the same
// gate: DMI must match a profile, a request must be valid for that model,
// acpi_call must be ready, and the BIOS must report the expected lights. Only
// the four GameZone methods named in ecservice.cpp are ever called.
class EcService
{
public:
    struct Result {
        int error = 0;              // 0 or HelperProtocol::Error
        QString detail;             // English, for logs and bug reports
        QList<EcLightState> lights; // state after the operation (none on an error)
    };

    EcService(const DmiInfo &dmi, AcpiBackend &acpi);

    Result status();
    // Calls SetLED only for requests that differ from what GetLED reports.
    Result set(const QVariantMap &args);
    // Fan layout code as the BIOS reports it (0x0A). Read-only, no SMI.
    struct LayoutResult {
        int error = 0;
        QString detail;
        int layout = -1;
    };
    LayoutResult layout();

private:
    int checkModel(QString *detail) const;
    int prepareBios(QString *detail);
    bool readAll(Result *r);
    std::optional<QList<EcRequest>> parseRequest(const QVariantMap &args, QString *detail) const;
    std::optional<EcLightState> getLed(quint8 index, int *error, QString *detail);
    QByteArray gameZoneCall(quint8 method, const QByteArray &arg) const;

    const ModelProfile *m_model;
    AcpiBackend &m_acpi;
    QString m_unsupportedDmi;
};

} // namespace Glowzone
