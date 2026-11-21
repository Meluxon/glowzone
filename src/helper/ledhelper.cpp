// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// Root helper: started by D-Bus on demand, authorised by polkit through KAuth.
// Three actions – read the case lights, set them, read the BIOS fan layout –
// all checked by EcService, which also re-checks the PC model on every call
// instead of trusting the app.

#include "config-glowzone.h"
#include "ecservice.h"
#include "procacpicall.h"

#include <KAuth/ActionReply>
#include <KAuth/HelperSupport>

using namespace KAuth;
using namespace Glowzone;

class LedHelper : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    KAuth::ActionReply status(const QVariantMap &)
    {
        ProcAcpiCall acpi;
        EcService service(DmiInfo::readSystem(), acpi);
        return toReply(service.status());
    }

    KAuth::ActionReply setled(const QVariantMap &args)
    {
        ProcAcpiCall acpi;
        EcService service(DmiInfo::readSystem(), acpi);
        return toReply(service.set(args));
    }

    KAuth::ActionReply layout(const QVariantMap &)
    {
        ProcAcpiCall acpi;
        EcService service(DmiInfo::readSystem(), acpi);
        const EcService::LayoutResult result = service.layout();
        if (result.error) {
            return errorReply(result.error, result.detail);
        }
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData({{HelperProtocol::KeyLayout, result.layout}});
        return reply;
    }

private:
    static ActionReply errorReply(int error, const QString &detail)
    {
        ActionReply reply = ActionReply::HelperErrorReply(error);
        reply.setErrorDescription(detail);
        return reply;
    }

    static ActionReply toReply(const EcService::Result &result)
    {
        if (result.error) {
            return errorReply(result.error, result.detail);
        }
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData(HelperProtocol::lightsToVariant(result.lights));
        return reply;
    }
};

KAUTH_HELPER_MAIN(GLOWZONE_HELPER_ID, LedHelper)

#include "ledhelper.moc"
