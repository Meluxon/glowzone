// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "appliers.h"

#include <QSignalBlocker>

#include <utility>

namespace Glowzone
{

static constexpr int FanDelayMs = 80;
static constexpr int LightDelayMs = 300; // each change is a root helper call and a BIOS SMI

FanApplier::FanApplier(const ModelProfile &model, QObject *parent, const QString &hwmonRoot)
    : QObject(parent)
    , m_model(model)
    , m_controller(model)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(FanDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &FanApplier::flushNow);
    m_rangeTimer.setSingleShot(true);
    m_rangeTimer.setInterval(FanDelayMs);
    connect(&m_rangeTimer, &QTimer::timeout, this, [this] {
        sendFollowers();
    });

    if (model.cpuTemperature) {
        m_temperature = new CpuTemperature(*model.cpuTemperature, this, hwmonRoot);
        connect(m_temperature, &CpuTemperature::colourChanged, this, [this] {
            sendFollowers();
        });
        connect(m_temperature, &CpuTemperature::updated, this, &FanApplier::temperatureUpdated);
        connect(m_temperature, &CpuTemperature::failed, this, &FanApplier::temperatureFailed);
        connect(m_temperature, &CpuTemperature::recovered, this, &FanApplier::temperatureRecovered);
        connect(m_temperature, &CpuTemperature::scaleChanged, this, &FanApplier::temperatureScaleChanged);
    }
}

bool FanApplier::open(QString *error)
{
    m_open = m_controller.open(error);
    updateTemperatureReading();
    return m_open;
}

void FanApplier::queue(const QList<Ite8297::HeaderSetting> &settings)
{
    for (const Ite8297::HeaderSetting &s : settings) {
        m_pending.insert(s.header, s);
    }
    m_timer.start();
}

void FanApplier::setCurrent(const FanState::Headers &headers)
{
    const auto before = m_followers;
    m_followers.clear();
    follow(headers.values());
    // E.g. set from the command line, with its own one-off reading. Sent the
    // way every temperature colour is: by the saved state, under the lock.
    QList<quint8> added = m_followers.keys();
    added.removeIf([&before](quint8 header) {
        return before.contains(header);
    });
    if (!added.isEmpty() && m_temperature && m_temperature->isRunning()) {
        sendFollowers(added);
        return;
    }
    updateTemperatureReading();
}

void FanApplier::setTemperatureRange(TemperatureRange range)
{
    if (!m_temperature) {
        return;
    }
    const Ite8297::Rgb before = m_temperature->colour();
    {
        const QSignalBlocker blocker(m_temperature); // colourChanged() would send at once
        m_temperature->setRange(range);
    }
    if (m_temperature->colour() != before || m_rangeTimer.isActive()) {
        m_rangeTimer.start();
    }
}

void FanApplier::follow(const QList<Ite8297::HeaderSetting> &settings)
{
    for (const Ite8297::HeaderSetting &s : settings) {
        if (Ite8297::followsTemperature(s.mode)) {
            m_followers.insert(s.header, s);
        } else {
            m_followers.remove(s.header);
        }
    }
}

// The sensor is only read while an LED needs it. Starting reads right away,
// and the first reading sends the colour to the LEDs (colourChanged).
void FanApplier::updateTemperatureReading()
{
    if (!m_temperature) {
        return;
    }
    const bool wanted = m_open && !m_followers.isEmpty();
    if (wanted && !m_temperature->isRunning()) {
        m_temperature->start();
    } else if (!wanted && m_temperature->isRunning()) {
        m_temperature->stop();
    }
}

QList<Ite8297::HeaderSetting> FanApplier::resolved(const QList<Ite8297::HeaderSetting> &settings) const
{
    const Ite8297::Rgb colour = m_temperature ? m_temperature->colour() : Ite8297::Rgb{};
    QList<Ite8297::HeaderSetting> out;
    for (const Ite8297::HeaderSetting &s : settings) {
        out << Ite8297::resolve(s, colour);
    }
    return out;
}

void FanApplier::flushNow()
{
    m_timer.stop();
    if (m_pending.isEmpty()) {
        return;
    }
    // A new range's colour is due: the following LEDs outside this batch get
    // it now, the batch goes out in it below – and none of them again later.
    if (m_rangeTimer.isActive()) {
        sendFollowers();
    }
    const QList<Ite8297::HeaderSetting> batch = m_pending.values();
    follow(batch);
    // The first LED to follow starts the reading, so the batch below goes out
    // with the current colour. Its LEDs are still pending here: the reading's
    // first colour leaves them out.
    updateTemperatureReading();
    m_pending.clear();

    QString error;
    bool sent = false;
    bool stored = false;
    {
        const FanState::Lock lock;
        sent = sendNow(batch, &error);
        if (sent) {
            for (const Ite8297::HeaderSetting &s : batch) {
                m_unsaved.insert(s.header, s);
            }
            stored = FanState::merge(lock, m_unsaved.values(), &error);
            if (stored) {
                m_unsaved.clear();
            }
        }
    }
    updateTemperatureReading();
    if (!sent) {
        Q_EMIT failed(error);
        return;
    }
    Q_EMIT applied();
    if (stored) {
        Q_EMIT saved();
    } else {
        Q_EMIT saveFailed(error);
    }
}

// The temperature colour changed: only the following LEDs get it (or those of
// them in only).
void FanApplier::sendFollowers(const QList<quint8> &only)
{
    m_rangeTimer.stop();
    // The command line may have changed an LED a moment ago, before Glowzone
    // read the saved state again: go by the saved state, so that change isn't
    // overwritten. The lock keeps a command from changing an LED between the
    // two. Our own changes are saved as they are sent, not saved yet, or still
    // pending.
    {
        const FanState::Lock lock;
        m_followers.clear();
        follow(FanState::load(m_model).values());
        follow(m_unsaved.values());
        follow(m_pending.values()); // changes on their way win
        QList<Ite8297::HeaderSetting> list = m_followers.values();
        if (!only.isEmpty()) {
            list.removeIf([&only](const Ite8297::HeaderSetting &s) {
                return !only.contains(s.header);
            });
        }
        send(list);
    }
    updateTemperatureReading();
}

// Nothing is saved – the state keeps the mode, not the colour of the moment.
void FanApplier::send(const QList<Ite8297::HeaderSetting> &followers)
{
    if (!m_open) {
        return;
    }
    QList<Ite8297::HeaderSetting> list;
    for (const Ite8297::HeaderSetting &s : followers) {
        if (!m_pending.contains(s.header)) { // a queued change sends its own colour
            list << s;
        }
    }
    if (list.isEmpty()) {
        return;
    }
    QString error;
    if (sendNow(list, &error)) {
        Q_EMIT applied();
    } else {
        Q_EMIT failed(error);
    }
}

// Once more after a failure: apply() then opens the controller again, which
// may have been reset or re-enumerated meanwhile.
bool FanApplier::sendNow(const QList<Ite8297::HeaderSetting> &settings, QString *error)
{
    const QList<Ite8297::HeaderSetting> reports = resolved(settings);
    const bool sent = m_controller.apply(reports, error) || m_controller.apply(reports, error);
    m_open = m_open || sent; // the controller may have come back
    return sent;
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

void LightApplier::readLayout()
{
    m_layoutWanted = true;
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

// What a light will show once the change already sent has arrived – not just
// what the helper last reported, or a quick On → Off would lose the Off.
std::optional<EcLightState> LightApplier::expected(quint8 index) const
{
    std::optional<EcLightState> state;
    if (const auto known = m_known.constFind(index); known != m_known.cend()) {
        state = *known;
    }
    if (const auto sent = m_inFlight.constFind(index); sent != m_inFlight.cend()) {
        if (!state) {
            if (sent->mode == 0 || sent->brightness == 0) {
                return std::nullopt; // part of it is unknown
            }
            state = EcLightState{index, sent->mode, sent->brightness};
        } else {
            state->mode = sent->mode ? sent->mode : state->mode;
            state->brightness = sent->brightness ? sent->brightness : state->brightness;
        }
    }
    return state;
}

void LightApplier::setKnown(const QList<EcLightState> &lights)
{
    for (const EcLightState &l : lights) {
        m_known.insert(l.index, l);
    }
}

void LightApplier::forget()
{
    m_known.clear();
}

bool LightApplier::isNoOp(EcRequest request) const
{
    const auto state = expected(request.index);
    return state && (request.mode == 0 || request.mode == state->mode) && (request.brightness == 0 || request.brightness == state->brightness);
}

void LightApplier::finish()
{
    m_timer.stop();
    m_finishing = true;
    m_refreshWanted = false; // nobody would see what they read
    m_layoutWanted = false;
    if (!isBusy()) {
        reportIdle();
        return;
    }
    startNext();
}

void LightApplier::reportIdle()
{
    m_finishing = false;
    Q_EMIT idle();
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
        m_inFlight = std::exchange(m_pending, {});
        m_refreshWanted = false; // a set reply carries the new state anyway
    } else if (m_refreshWanted) {
        m_refreshWanted = false;
    } else if (m_layoutWanted) {
        kind = HelperCall::Layout;
        m_layoutWanted = false;
    } else {
        reportIdle();
        return;
    }

    m_call = m_calls.create(kind, requests, this);
    connect(m_call, &HelperCall::finished, this, [this, kind](const HelperResult &r) {
        m_call = nullptr;
        m_inFlight.clear();
        if (kind == HelperCall::Layout) {
            if (r.ok()) {
                Q_EMIT layoutRead(r.layout);
            }
        } else if (!r.ok()) {
            m_known.clear(); // unsure now – don't skip anything
            // A failed change may have set some lights and not others (each is
            // its own BIOS call): read back what they really show.
            m_refreshWanted = m_refreshWanted || (kind == HelperCall::Set && !m_finishing);
            Q_EMIT failed(r.error);
        } else {
            for (const EcLightState &l : r.lights) {
                m_known.insert(l.index, l);
            }
            Q_EMIT succeeded();
            if (m_pending.isEmpty() && !m_timer.isActive()) {
                Q_EMIT stateChanged(r.lights);
            }
        }
        startNext();
    });
    m_call->start();
}

} // namespace Glowzone
