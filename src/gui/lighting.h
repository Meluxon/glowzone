// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/helpercall.h"
#include "core/profiles.h"

#include <QObject>

#include <memory>

class QWindow;

namespace Glowzone
{
class FanApplier;
class LightApplier;
}

namespace Glowzone::Gui
{

// The lighting as the window shows it, and the single place that changes it.
// Pages and the sidebar read from here and call the setters; everything they
// show is refreshed from changed().
class Lighting : public QObject
{
    Q_OBJECT

public:
    Lighting(const ModelProfile &model, QObject *parent);

    // Opens the fan controller and reads the case lights. The window is used
    // as parent for a polkit dialog, should one ever be needed.
    void start(QWindow *window);

    const ModelProfile &model() const { return m_model; }

    // --- fans
    bool fansAvailable() const { return m_fansAvailable; }
    bool isKnown(quint8 header) const { return m_fans.contains(header); }
    Ite8297::HeaderSetting led(quint8 header) const; // a default if unknown
    bool isLinked(const FanDef &fan) const;
    // Linked double fan: the values go to all its LEDs.
    void setLed(const FanDef &fan, quint8 header, Ite8297::HeaderSetting values);
    // Linking copies the first LED's settings to the others.
    void setLinked(const FanDef &fan, bool linked);

    // --- case lights
    bool lightsAvailable() const { return m_lightsLoaded; }
    bool lightsFailed() const { return m_lightsFailed; }

    // Fan layout as the BIOS detects it (checked once at start); -1 = unknown.
    int fanLayout() const { return m_fanLayout; }
    // Fans the BIOS doesn't see right now (only informative).
    bool isUndetected(const FanDef &fan) const;
    std::optional<EcLightState> light(quint8 index) const;
    void setLight(quint8 index, quint8 mode, quint8 brightness);

    // --- profiles
    Profile current() const;
    // Does the lighting match what the profile contains?
    bool matches(const Profile &profile) const;
    void apply(const Profile &profile);

    void turnAllOff();

    // Send anything still pending; idle() follows when done.
    bool isBusy() const;
    void finish();

Q_SIGNALS:
    void changed();
    void problem(const QString &text);  // something is missing (lasting)
    void error(const QString &text);    // a change failed
    void errorCleared();
    void idle();

private:
    const ModelProfile &m_model;
    FanState::Headers m_fans;
    QMap<QString, bool> m_links;
    QMap<quint8, EcLightState> m_lights;
    bool m_fansAvailable = true;
    bool m_lightsLoaded = false;
    bool m_lightsFailed = false;
    int m_fanLayout = -1;
    bool m_layoutChecked = false;

    void checkFanLayout();
    std::unique_ptr<KAuthHelperCalls> m_helper;
    FanApplier *m_fanApplier;
    LightApplier *m_lightApplier;
};

} // namespace Glowzone::Gui
