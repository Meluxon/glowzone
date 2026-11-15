// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cputemperature.h"
#include "fancontroller.h"
#include "fanstate.h"
#include "helpercall.h"

#include <QMap>
#include <QObject>
#include <QTimer>

#include <optional>

namespace Glowzone
{

// Changes apply live, like in Vantage, but are collected for a short moment so
// dragging a slider or the colour picker doesn't flood the hardware. The last
// value for each LED wins.
//
// LEDs in the CPU-temperature mode are kept in their colour here: while any
// LED uses the mode, the temperature is read and those LEDs get a new colour
// whenever the step changes. The saved fan state keeps the mode itself, never
// the colour of the moment.
class FanApplier : public QObject
{
    Q_OBJECT

public:
    FanApplier(const ModelProfile &model, QObject *parent, const QString &hwmonRoot = QStringLiteral("/sys/class/hwmon"));

    bool open(QString *error);
    void queue(const QList<Ite8297::HeaderSetting> &settings);
    void flushNow();
    bool isBusy() const { return !m_pending.isEmpty(); }

    // What the LEDs are set to, e.g. after reading the saved state again: the
    // CPU-temperature LEDs among them are followed from now on. Sends nothing
    // else.
    void setCurrent(const FanState::Headers &headers);

    // Any LED in the CPU-temperature mode?
    bool followsTemperature() const { return !m_followers.isEmpty(); }
    // nullptr if the model doesn't offer the mode.
    const CpuTemperature *temperature() const { return m_temperature; }

Q_SIGNALS:
    void failed(const QString &error);
    void applied();
    void temperatureUpdated();
    void temperatureFailed(const QString &error);
    void temperatureRecovered();

private:
    void follow(const QList<Ite8297::HeaderSetting> &settings);
    void updateTemperatureReading();
    void sendFollowers();
    QList<Ite8297::HeaderSetting> resolved(const QList<Ite8297::HeaderSetting> &settings) const;

    FanController m_controller;
    bool m_open = false;
    QMap<quint8, Ite8297::HeaderSetting> m_pending;
    QMap<quint8, Ite8297::HeaderSetting> m_followers; // as saved: mode CPU temperature
    CpuTemperature *m_temperature = nullptr;
    QTimer m_timer;
};

// The case lights go through the root helper, asynchronously and one call at a
// time – including the fan-layout read, so the window never has two calls
// running (KAuth's helper serves one call at a time and doesn't queue them). State from the helper is reported only when nothing newer is queued,
// so it never overwrites what the user is doing.
// Each SetLED triggers a BIOS SMI that can freeze the PC for seconds, so
// requests that wouldn't change anything are dropped.
class LightApplier : public QObject
{
    Q_OBJECT

public:
    LightApplier(HelperCalls &calls, QObject *parent);
    void setDelay(int ms) { m_timer.setInterval(ms); } // for tests

    void refresh();
    void readLayout(); // answered by layoutRead(); failures are silent
    void queue(EcRequest request);
    bool isBusy() const;
    void finish(); // send anything queued right away; idle() follows

Q_SIGNALS:
    void stateChanged(const QList<Glowzone::EcLightState> &lights);
    void succeeded(); // a call went through (after an error: it's over)
    void failed(const QString &error);
    void layoutRead(int code);
    void idle();

private:
    void startNext();

    std::optional<EcLightState> expected(quint8 index) const;
    bool isNoOp(EcRequest request) const;

    QMap<quint8, EcRequest> m_pending;
    QMap<quint8, EcLightState> m_known;  // last state reported by the helper
    QMap<quint8, EcRequest> m_inFlight;  // sent, answer not back yet
    QTimer m_timer;
    HelperCalls &m_calls;
    HelperCall *m_call = nullptr;
    bool m_refreshWanted = false;
    bool m_layoutWanted = false;
};

} // namespace Glowzone
