// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "eclights.h"
#include "config-glowzone.h"

#include <KAuth/Action>
#include <KAuth/ActionReply>
#include <KAuth/ExecuteJob>
#include <KLocalizedString>

namespace Glowzone::EcLights
{

namespace HP = HelperProtocol;

const QList<ModeInfo> &modes()
{
    static const QList<ModeInfo> list = {
        {1, "off", kli18nc("@item light mode", "Off")},
        {2, "on", kli18nc("@item light mode", "On")},
        {3, "blink-fast", kli18nc("@item light mode", "Blink fast")},
        {4, "blink-slow", kli18nc("@item light mode", "Blink slow")},
        {5, "breathe", kli18nc("@item light mode", "Breathing")},
    };
    return list;
}

QList<ModeInfo> modesFor(const ModelProfile &model)
{
    QList<ModeInfo> list;
    for (const ModeInfo &m : modes()) {
        if (model.hasEcLightMode(m.value)) {
            list << m;
        }
    }
    return list;
}

const ModeInfo *modeInfo(quint8 value)
{
    for (const ModeInfo &m : modes()) {
        if (m.value == value) {
            return &m;
        }
    }
    return nullptr;
}

const ModeInfo *modeInfo(const QString &id)
{
    for (const ModeInfo &m : modes()) {
        if (id == QLatin1String(m.id)) {
            return &m;
        }
    }
    return nullptr;
}

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

static KAuth::ExecuteJob *makeJob(const char *slot, const QVariantMap &args, QWindow *parent)
{
    KAuth::Action action(QLatin1String(GLOWZONE_HELPER_ID ".") + QLatin1String(slot));
    action.setHelperId(QStringLiteral(GLOWZONE_HELPER_ID));
    action.setArguments(args);
    action.setTimeout(30000);
    if (parent) {
        action.setParentWindow(parent);
    }
    return action.execute();
}

KAuth::ExecuteJob *statusJob(QWindow *parent)
{
    return makeJob(HP::ActionStatus, {}, parent);
}

KAuth::ExecuteJob *setJob(const QList<EcRequest> &requests, QWindow *parent)
{
    return makeJob(HP::ActionSetLed, HP::requestsToVariant(requests), parent);
}

std::optional<QList<EcLightState>> result(KAuth::ExecuteJob *job, QString *error)
{
    if (job->error()) {
        *error = errorMessage(job->error(), job->errorText());
        return std::nullopt;
    }
    return HP::statesFromVariant(job->data());
}

static std::optional<QList<EcLightState>> runBlocking(KAuth::ExecuteJob *job, QString *error)
{
    job->setAutoDelete(false);
    job->exec();
    const auto r = result(job, error);
    delete job;
    return r;
}

std::optional<QList<EcLightState>> status(QString *error)
{
    return runBlocking(statusJob(), error);
}

std::optional<QList<EcLightState>> set(const QList<EcRequest> &requests, QString *error)
{
    return runBlocking(setJob(requests), error);
}

KAuth::ExecuteJob *layoutJob(QWindow *parent)
{
    return makeJob(HP::ActionLayout, {}, parent);
}

std::optional<int> layoutResult(KAuth::ExecuteJob *job, QString *error)
{
    if (job->error()) {
        *error = errorMessage(job->error(), job->errorText());
        return std::nullopt;
    }
    bool ok = false;
    const int value = job->data().value(QLatin1String(HP::KeyLayout)).toInt(&ok);
    if (!ok) {
        *error = errorMessage(HP::UnexpectedReply, {});
        return std::nullopt;
    }
    return value;
}

std::optional<int> layout(QString *error)
{
    KAuth::ExecuteJob *job = layoutJob();
    job->setAutoDelete(false);
    job->exec();
    const auto r = layoutResult(job, error);
    delete job;
    return r;
}

QString layoutWarning(const ModelProfile &model, int layout)
{
    if (layout == model.normalFanLayout) {
        return {};
    }
    const QString codes = QStringLiteral("0x%1 / 0x%2").arg(layout, 2, 16, QLatin1Char('0')).arg(model.normalFanLayout, 2, 16, QLatin1Char('0'));
    QStringList missing;
    for (const FanLayoutDef &def : model.fanLayouts) {
        if (def.code == layout) {
            for (const char *id : def.undetectedFans) {
                if (const FanDef *fan = model.fan(QLatin1String(id))) {
                    missing << fan->name.toString();
                }
            }
        }
    }
    const QString why = i18n("This is a BIOS detection glitch that can happen after a cold start, when a fan isn’t spinning yet. "
                             "The fan LEDs usually still work, and your settings are unchanged. A reboot normally clears it.");
    if (!missing.isEmpty()) {
        return i18nc("%1 fan names, %2 layout codes, %3 explanation",
                     "The BIOS doesn’t detect these fans right now: %1 (fan layout %2).\n%3",
                     missing.join(QLatin1String(", ")),
                     codes,
                     why);
    }
    return i18nc("%1 layout codes, %2 explanation", "The BIOS reports an unusual fan layout right now (%1); a fan may not be detected.\n%2", codes, why);
}

} // namespace Glowzone::EcLights
