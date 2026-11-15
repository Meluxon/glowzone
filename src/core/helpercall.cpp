// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "helpercall.h"
#include "config-glowzone.h"

#include <KAuth/Action>
#include <KAuth/ActionReply>
#include <KAuth/ExecuteJob>
#include <KLocalizedString>
#include <QEventLoop>

namespace Glowzone
{

namespace HP = HelperProtocol;

static QString errorMessage(int code, const QString &detail)
{
    QString msg;
    switch (code) {
    case HP::UnsupportedModel:
        msg = i18n("The helper refused to run: this computer is not a supported model.");
        break;
    case HP::AcpiCallUnavailable:
        msg = i18n("The acpi_call kernel module is not available. Install acpi_call-dkms "
                   "(or acpi_call / acpi_call-lts for your kernel).");
        break;
    case HP::BiosLedMissing:
        msg = i18n("The BIOS does not report the case lights on this computer.");
        break;
    case HP::InvalidArguments:
        msg = i18n("The helper rejected the request as invalid.");
        break;
    case HP::AcpiCallFailed:
    case HP::UnexpectedReply:
        msg = i18n("The firmware call for the case lights failed.");
        break;
    case KAuth::ActionReply::AuthorizationDeniedError:
        msg = i18n("Not authorised to change the case lights.");
        break;
    case KAuth::ActionReply::UserCancelledError:
        msg = i18n("Authorisation was cancelled.");
        break;
    case KAuth::ActionReply::BackendError:
        msg = i18n("The authorisation system (polkit) reported an error.");
        break;
    case KAuth::ActionReply::NoResponderError:
    case KAuth::ActionReply::NoSuchActionError:
    case KAuth::ActionReply::DBusError:
        msg = i18n("Could not reach the Glowzone helper. Is Glowzone installed correctly?");
        break;
    default:
        msg = i18n("The Glowzone helper reported an error (code %1).", code);
        break;
    }
    if (!detail.isEmpty()) {
        msg += QLatin1Char('\n') + i18nc("@info technical details follow", "Details: %1", detail);
    }
    return msg;
}

namespace
{

class KAuthCall : public HelperCall
{
public:
    KAuthCall(Kind kind, const QList<EcRequest> &requests, QWindow *window, QObject *parent)
        : HelperCall(parent)
        , m_kind(kind)
        , m_requests(requests)
        , m_window(window)
    {
    }

    void start() override
    {
        const char *slot = m_kind == Status ? HP::ActionStatus : m_kind == Set ? HP::ActionSetLed : HP::ActionLayout;
        KAuth::Action action(QLatin1String(GLOWZONE_HELPER_ID ".") + QLatin1String(slot));
        action.setHelperId(QStringLiteral(GLOWZONE_HELPER_ID));
        if (m_kind == Set) {
            action.setArguments(HP::requestsToVariant(m_requests));
        }
        action.setTimeout(30000);
        if (m_window) {
            action.setParentWindow(m_window);
        }
        KAuth::ExecuteJob *job = action.execute();
        connect(job, &KJob::result, this, [this, job] {
            finish(read(job));
        });
        job->start();
    }

private:
    HelperResult read(KAuth::ExecuteJob *job) const
    {
        HelperResult r;
        if (job->error()) {
            r.error = errorMessage(job->error(), job->errorText());
        } else if (m_kind == Layout) {
            bool ok = false;
            r.layout = job->data().value(QLatin1String(HP::KeyLayout)).toInt(&ok);
            if (!ok) {
                r.error = errorMessage(HP::UnexpectedReply, {});
            }
        } else {
            r.lights = HP::statesFromVariant(job->data());
        }
        return r;
    }

    void finish(const HelperResult &result)
    {
        Q_EMIT finished(result);
        deleteLater();
    }

    Kind m_kind;
    QList<EcRequest> m_requests;
    QWindow *m_window;
};

} // namespace

HelperCall *KAuthHelperCalls::create(HelperCall::Kind kind, const QList<EcRequest> &requests, QObject *parent)
{
    return new KAuthCall(kind, requests, m_window, parent);
}

HelperResult runToEnd(HelperCalls &calls, HelperCall::Kind kind, const QList<EcRequest> &requests)
{
    HelperResult result;
    bool done = false;
    QEventLoop loop;
    HelperCall *call = calls.create(kind, requests, nullptr);
    QObject::connect(call, &HelperCall::finished, &loop, [&](const HelperResult &r) {
        result = r;
        done = true;
        loop.quit();
    });
    call->start();
    if (!done) {
        loop.exec();
    }
    return result;
}

} // namespace Glowzone
