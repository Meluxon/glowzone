// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "helpercall.h"
#include "config-glowzone.h"

#include <KAuth/Action>
#include <KAuth/ActionReply>
#include <KAuth/ExecuteJob>
#include <KLocalizedString>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>

#include <chrono>
#include <memory>
#include <utility>

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

using namespace std::chrono_literals;

// KAuth's helper serves one call at a time. A call that arrives meanwhile is
// answered "busy", which KAuth's client ignores; it then waits for a completion
// that is broadcast to every client and matched by action name only – so it
// either never finishes or takes another caller's result. Glowzone's calls
// therefore take turns: one lock file for all its processes (window and CLI),
// and a time limit in case a call never finishes all the same.
constexpr auto LockWait = 30s; // another Glowzone may be in the middle of a slow SetLED
constexpr auto CallLimit = 60s;

QString lockPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return dir + QLatin1String("/glowzone-helper.lock");
}

class KAuthCall : public HelperCall
{
public:
    KAuthCall(Kind kind, const QList<EcRequest> &requests, QWindow *window, QObject *parent)
        : HelperCall(parent)
        , m_kind(kind)
        , m_requests(requests)
        , m_window(window)
        , m_lock(std::make_unique<QLockFile>(lockPath()))
    {
        m_lock->setStaleLockTime(2 * CallLimit);
        m_watchdog.setSingleShot(true);
        connect(&m_watchdog, &QTimer::timeout, this, [this] {
            if (m_job) {
                m_job->kill(KJob::Quietly);
                m_job = nullptr;
            }
            finish({i18n("The Glowzone helper didn’t answer. Try again in a moment."), {}, -1});
        });
    }

    void start() override
    {
        m_waiting.start();
        tryStart();
    }

private:
    void tryStart()
    {
        if (!m_lock->tryLock(0)) {
            if (m_waiting.durationElapsed() > LockWait) {
                finish({i18n("Another Glowzone window or command is using the case lights. Try again in a moment."), {}, -1});
            } else {
                QTimer::singleShot(100ms, this, &KAuthCall::tryStart);
            }
            return;
        }

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
        m_job = action.execute();
        connect(m_job, &KJob::result, this, [this] {
            KAuth::ExecuteJob *job = std::exchange(m_job, nullptr);
            finish(read(job));
        });
        m_watchdog.start(CallLimit);
        m_job->start();
    }

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
        if (m_done) {
            return;
        }
        m_done = true;
        m_watchdog.stop();
        m_lock->unlock();
        Q_EMIT finished(result);
        deleteLater();
    }

    Kind m_kind;
    QList<EcRequest> m_requests;
    QWindow *m_window;
    std::unique_ptr<QLockFile> m_lock;
    QElapsedTimer m_waiting;
    QTimer m_watchdog;
    KAuth::ExecuteJob *m_job = nullptr;
    bool m_done = false;
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
