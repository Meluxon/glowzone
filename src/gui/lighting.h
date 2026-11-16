// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/helpercall.h"
#include "core/profiles.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <memory>

class QWindow;

namespace Glowzone
{
class FanApplier;
class TemperatureScale;
class LightApplier;
}

namespace Glowzone::Gui
{

// The lighting as Glowzone knows it, and the single place that changes it. It
// lives as long as Glowzone runs – also in the tray, without a window. Pages
// and the sidebar read from here and call the setters; everything they show is
// refreshed from changed().
class Lighting : public QObject
{
    Q_OBJECT

public:
    Lighting(const ModelProfile &model, QObject *parent);

    // Opens the fan controller – from then on LEDs in the CPU-temperature mode
    // follow it, window or not. If that failed, each new window tries again.
    // With a window, the case lights are read too, and the window is the
    // parent for a polkit dialog, should one ever be needed. Without one
    // (started in the background) the root helper isn't used at all.
    void start(QWindow *window);

    // What is missing right now (a window opened later shows it too); each
    // problem goes away once its part works.
    QStringList problems() const;
    QString fanProblem() const { return m_fanProblem; } // empty while the controller works

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

    // --- CPU temperature
    // Does any LED follow it right now?
    bool followsCpuTemperature() const;
    // The smoothed temperature the colours follow (nullopt: not read yet,
    // or no LED follows it).
    std::optional<double> cpuTemperature() const;
    // Why it can't be read (empty while it can).
    QString cpuTemperatureError() const { return m_temperatureError; }
    // The colour the following LEDs show.
    Ite8297::Rgb cpuTemperatureColour() const;
    // The colours for every temperature, with the current range (nullptr if
    // the model doesn't offer the mode).
    const TemperatureScale *temperatureScale() const;
    // Shared by all following LEDs; part of a profile. Takes effect at once.
    void setTemperatureRange(TemperatureRange range);

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
    // The profile applied last, as glowzone.conf records it; the window sets
    // it through here, so a change made elsewhere can be told apart.
    void setCurrentProfile(const QString &name);
    Profile current() const;
    // Does the lighting match what the profile contains?
    bool matches(const Profile &profile) const;
    void apply(const Profile &profile);

    void turnAllOff();

    // Reads again what the CLI may have changed meanwhile: the fan state, the
    // split choices, the temperature range and the case lights. Skipped while
    // a change is being sent.
    void reloadFromDisk();

    // Send anything still pending; idle() follows when done.
    bool isBusy() const;
    void finish();

Q_SIGNALS:
    void changed();
    void savedChanged();               // saved state, settings or profiles changed (e.g. by the CLI)
    void temperatureChanged();         // a new reading (every couple of seconds while followed)
    void problemsChanged();
    void error(const QString &text);   // a change failed
    void errorCleared();
    void idle();

private:
    void checkFanLayout();
    void loadFans();
    void loadTemperatureRange();
    void saveTemperatureRange(); // now, instead of when m_rangeSave runs out
    void reloadSaved();
    void setProblem(QString &problem, const QString &text);
    void setError(const QString &text, int source);
    void clearError(int source);

    const ModelProfile &m_model;
    FanState::Headers m_fans;
    QMap<QString, bool> m_links;
    std::optional<TemperatureRange> m_temperatureRange; // as stored (nullopt: no such mode)
    QTimer m_rangeSave;
    QMap<quint8, EcLightState> m_lights;
    QString m_currentProfile;
    bool m_fansAvailable = true; // until opening the controller fails
    bool m_lightsLoaded = false;
    bool m_lightsFailed = false;
    bool m_lightsStale = false; // a profile was applied elsewhere: being read again
    int m_fanLayout = -1;
    bool m_layoutChecked = false;
    QString m_fanProblem;
    QString m_lightProblem;
    QString m_layoutProblem;
    QString m_temperatureError;
    int m_errorSource = 0;  // which queue the shown error came from (0: none)
    QDateTime m_stateStamp; // fans.json as last read or saved by us
    bool m_settingsChanged = false; // glowzone.conf or the profiles, since the last reload
    QTimer m_savedReload;
    std::unique_ptr<KAuthHelperCalls> m_helper;
    FanApplier *m_fanApplier;
    LightApplier *m_lightApplier;
};

} // namespace Glowzone::Gui
