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
// whenever theirs changes – with the temperature or the range. The saved fan state keeps the mode itself, never
// the colour of the moment.
class FanApplier : public QObject
{
    Q_OBJECT

public:
    FanApplier(const ModelProfile &model, QObject *parent, const QString &hwmonRoot = HwmonRoot);

    bool open(QString *error);
    bool isOpen() const { return m_open; }
    void queue(const QList<Ite8297::HeaderSetting> &settings);
    void flushNow();
    bool isBusy() const { return !m_pending.isEmpty(); }

    // What the LEDs are set to, e.g. after reading the saved state again: the
    // CPU-temperature LEDs among them are followed from now on, and one that
    // wasn't followed before gets the current colour.
    void setCurrent(const FanState::Headers &headers);

    // Sent, but not saved yet (saving failed): what these LEDs really show.
    // Saved together with the next change.
    const FanState::Headers &unsaved() const { return m_unsaved; }

    // Any LED in the CPU-temperature mode?
    bool followsTemperature() const { return !m_followers.isEmpty(); }
    // nullptr if the model doesn't offer the mode.
    const CpuTemperature *temperature() const { return m_temperature; }
    // The range the colours are spread over. Following LEDs get a new colour a
    // moment later if it changes theirs, so dragging a slider doesn't flood the
    // controller.
    void setTemperatureRange(TemperatureRange range);
    void setTemperatureInterval(int ms) { m_temperature->setInterval(ms); } // for tests

Q_SIGNALS:
    void failed(const QString &error);     // sending failed: the LEDs show what was sent before
    void applied();                        // something was sent: the controller works
    void saveFailed(const QString &error); // sent, but the state could not be saved
    void saved();                          // the state of every LED sent so far is saved
    void temperatureUpdated();
    void temperatureFailed(const QString &error);
    void temperatureRecovered();
    void temperatureScaleChanged(); // the colours for each temperature, e.g. a new sensor limit

private:
    void follow(const QList<Ite8297::HeaderSetting> &settings);
    void updateTemperatureReading();
    void sendFollowers(const QList<quint8> &only = {});
    void send(const QList<Ite8297::HeaderSetting> &followers);
    bool sendNow(const QList<Ite8297::HeaderSetting> &settings, QString *error);
    QList<Ite8297::HeaderSetting> resolved(const QList<Ite8297::HeaderSetting> &settings) const;

    const ModelProfile &m_model;
    FanController m_controller;
    bool m_open = false;
    FanState::Headers m_pending;
    FanState::Headers m_unsaved;
    FanState::Headers m_followers; // as saved: mode CPU temperature
    CpuTemperature *m_temperature = nullptr;
    QTimer m_timer;
    QTimer m_rangeTimer; // a new range's colour goes out when it fires
};

// The case lights go through the root helper, asynchronously and one call at a
// time, the fan-layout read included: KAuth's helper serves one call at a time
// and doesn't queue them. State from the helper is reported only when nothing
// newer is queued, so it never overwrites what the user is doing.
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
    // The lights were set elsewhere (the command line): skip requests by this state.
    void setKnown(const QList<EcLightState> &lights);
    void forget(); // nothing is known about the lights any more
    bool isBusy() const;
    // Send anything queued right away, without reading anything back; idle() follows.
    void finish();

Q_SIGNALS:
    void stateChanged(const QList<Glowzone::EcLightState> &lights);
    void succeeded(); // a call went through (after an error: it's over)
    void failed(const QString &error);
    void layoutRead(int code);
    void idle();

private:
    void startNext();
    void reportIdle();

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
    bool m_finishing = false;
};

} // namespace Glowzone
