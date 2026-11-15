// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "appliers.h"

#include "core/eclights.h"
#include "core/fanstate.h"

#include <KAuth/ExecuteJob>

namespace Glowzone::Gui
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

LightApplier::LightApplier(QObject *parent)
    : QObject(parent)
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
    return m_job || !m_pending.isEmpty();
}

void LightApplier::startNext()
{
    if (m_job) {
        return; // picked up when the running job finishes
    }
    if (!m_pending.isEmpty()) {
        m_job = EcLights::setJob(m_pending.values(), m_window);
        m_jobIsSet = true;
        m_pending.clear();
        m_refreshWanted = false; // a set reply carries the new state anyway
    } else if (m_refreshWanted) {
        m_job = EcLights::statusJob(m_window);
        m_jobIsSet = false;
        m_refreshWanted = false;
    } else {
        Q_EMIT idle();
        return;
    }

    connect(m_job, &KJob::result, this, [this] {
        QString error;
        const auto lights = EcLights::result(m_job, &error);
        m_job = nullptr; // auto-deleted by KJob
        if (!lights) {
            m_known.clear(); // unsure now – don't skip anything
            // A failed change may have set some lights and not others (each is
            // its own BIOS call): read back what they really show.
            m_refreshWanted = m_refreshWanted || m_jobIsSet;
            Q_EMIT failed(error);
        } else {
            for (const EcLightState &l : *lights) {
                m_known.insert(l.index, l);
            }
            if (m_pending.isEmpty() && !m_timer.isActive()) {
                Q_EMIT stateChanged(*lights);
            }
        }
        startNext();
    });
    m_job->start();
}

} // namespace Glowzone::Gui
