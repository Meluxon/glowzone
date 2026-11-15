// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// Root helper: started by D-Bus on demand, authorised by polkit through KAuth.
// It does exactly two things – read and set the two EC lights – and re-checks
// the PC model itself on every call instead of trusting the app.

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
    KAuth::ActionReply status(const QVariantMap &args)
    {
        Q_UNUSED(args)
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

    KAuth::ActionReply layout(const QVariantMap &args)
    {
        Q_UNUSED(args)
        ProcAcpiCall acpi;
        EcService service(DmiInfo::readSystem(), acpi);
        const EcService::LayoutResult result = service.layout();
        if (result.error) {
            ActionReply reply = ActionReply::HelperErrorReply(result.error);
            reply.setErrorDescription(result.detail);
            return reply;
        }
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData({{QLatin1String(HelperProtocol::KeyLayout), result.layout}});
        return reply;
    }

private:
    static ActionReply toReply(const EcService::Result &result)
    {
        if (result.error) {
            ActionReply reply = ActionReply::HelperErrorReply(result.error);
            reply.setErrorDescription(result.detail);
            reply.setData({{QLatin1String(HelperProtocol::KeyDetail), result.detail}});
            return reply;
        }
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData(HelperProtocol::statesToVariant(result.lights));
        return reply;
    }
};

KAUTH_HELPER_MAIN(GLOWZONE_HELPER_ID, LedHelper)

#include "ledhelper.moc"
