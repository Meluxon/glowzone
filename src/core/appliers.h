// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "fancontroller.h"
#include "helpercall.h"

#include <QMap>
#include <QObject>
#include <QTimer>

namespace Glowzone
{

// Changes apply live, like in Vantage, but are collected for a short moment so
// dragging a slider or the colour picker doesn't flood the hardware. The last
// value for each LED wins.

class FanApplier : public QObject
{
    Q_OBJECT

public:
    FanApplier(const ModelProfile &model, QObject *parent);

    bool open(QString *error);
    void queue(const QList<Ite8297::HeaderSetting> &settings);
    void flushNow();

Q_SIGNALS:
    void failed(const QString &error);
    void applied();

private:
    FanController m_controller;
    QMap<quint8, Ite8297::HeaderSetting> m_pending;
    QTimer m_timer;
};

// The case lights go through the root helper, asynchronously and one job at a
// time. State from the helper is reported only when nothing newer is queued,
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
    void queue(EcRequest request);
    bool isBusy() const;
    void finish(); // send anything queued right away; idle() follows

Q_SIGNALS:
    void stateChanged(const QList<Glowzone::EcLightState> &lights);
    void failed(const QString &error);
    void idle();

private:
    void startNext();

    bool isNoOp(EcRequest request) const;

    QMap<quint8, EcRequest> m_pending;
    QMap<quint8, EcLightState> m_known; // last state reported by the helper
    QTimer m_timer;
    HelperCalls &m_calls;
    HelperCall *m_call = nullptr;
    bool m_refreshWanted = false;
};

} // namespace Glowzone
