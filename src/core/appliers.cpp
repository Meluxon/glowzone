// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "appliers.h"

#include "fanstate.h"

namespace Glowzone
{

static constexpr int FanDelayMs = 80;
static constexpr int LightDelayMs = 300; // each change is a root helper call and a BIOS SMI

FanApplier::FanApplier(const ModelProfile &model, QObject *parent)
    : QObject(parent)
    , m_controller(model)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(FanDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &FanApplier::flushNow);
}

bool FanApplier::open(QString *error)
{
    return m_controller.open(error);
}

void FanApplier::queue(const QList<Ite8297::HeaderSetting> &settings)
{
    for (const Ite8297::HeaderSetting &s : settings) {
        m_pending.insert(s.header, s);
    }
    m_timer.start();
}

void FanApplier::flushNow()
{
    m_timer.stop();
    if (m_pending.isEmpty()) {
        return;
    }
    const QList<Ite8297::HeaderSetting> batch = m_pending.values();
    m_pending.clear();

    QString error;
    if (!m_controller.apply(batch, &error) || !FanState::merge(batch, &error)) {
        Q_EMIT failed(error);
        return;
    }
    Q_EMIT applied();
}

LightApplier::LightApplier(HelperCalls &calls, QObject *parent)
    : QObject(parent)
    , m_calls(calls)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(LightDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &LightApplier::startNext);
}

void LightApplier::refresh()
{
    m_refreshWanted = true;
    startNext();
}

void LightApplier::queue(EcRequest request)
{
    if (isNoOp(request)) {
        m_pending.remove(request.index); // back to where it was: nothing to send
        if (m_pending.isEmpty()) {
            m_timer.stop();
        }
        return;
    }
    m_pending.insert(request.index, request);
    m_timer.start();
}

bool LightApplier::isNoOp(EcRequest request) const
{
    const auto it = m_known.constFind(request.index);
    if (it == m_known.cend()) {
        return false;
    }
    return (request.mode == 0 || request.mode == it->mode) && (request.brightness == 0 || request.brightness == it->brightness);
}

void LightApplier::finish()
{
    m_timer.stop();
    if (!isBusy()) {
        Q_EMIT idle();
        return;
    }
    startNext();
}

bool LightApplier::isBusy() const
{
    return m_call || !m_pending.isEmpty();
}

void LightApplier::startNext()
{
    if (m_call) {
        return; // picked up when the running call finishes
    }
    HelperCall::Kind kind = HelperCall::Status;
    QList<EcRequest> requests;
    if (!m_pending.isEmpty()) {
        kind = HelperCall::Set;
        requests = m_pending.values();
        m_pending.clear();
        m_refreshWanted = false; // a set reply carries the new state anyway
    } else if (m_refreshWanted) {
        m_refreshWanted = false;
    } else {
        Q_EMIT idle();
        return;
    }

    m_call = m_calls.create(kind, requests, this);
    connect(m_call, &HelperCall::finished, this, [this, kind](const HelperResult &r) {
        m_call = nullptr;
        if (!r.ok()) {
            m_known.clear(); // unsure now – don't skip anything
            // A failed change may have set some lights and not others (each is
            // its own BIOS call): read back what they really show.
            m_refreshWanted = m_refreshWanted || kind == HelperCall::Set;
            Q_EMIT failed(r.error);
        } else {
            for (const EcLightState &l : r.lights) {
                m_known.insert(l.index, l);
            }
            if (m_pending.isEmpty() && !m_timer.isActive()) {
                Q_EMIT stateChanged(r.lights);
            }
        }
        startNext();
    });
    m_call->start();
}

} // namespace Glowzone
