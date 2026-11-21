// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lighting.h"

#include "core/appliers.h"
#include "core/eclights.h"
#include "core/profiles.h"
#include "core/settings.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>

#include <utility>

namespace Glowzone::Gui
{

using namespace Ite8297;

// Dragging a range slider: the setting is written once it stops for this long.
static constexpr int RangeSaveDelayMs = 500;
// A command writes several files: read them once it is done.
static constexpr int SavedReloadDelayMs = 300;

Lighting::Lighting(const ModelProfile &model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_fanApplier(model, nullptr)
    , m_lightApplier(m_helper, nullptr)
{
    loadTemperatureRange();
    loadFans();
    m_currentProfile = Settings::currentProfile();
    if (const auto recorded = EcLights::loadState(model)) {
        m_lightsSeen = recorded->written; // older than any window
    }
    m_rangeSave.setSingleShot(true);
    m_rangeSave.setInterval(RangeSaveDelayMs);
    connect(&m_rangeSave, &QTimer::timeout, this, &Lighting::saveTemperatureRange);

    // The command line may change things while Glowzone runs – maybe only in
    // the tray, with LEDs following the CPU temperature: the fan state and the
    // case lights it set (the state directory), the split choices, temperature
    // range and current profile (glowzone.conf), the profiles. They are read
    // again when they change. Files are replaced, not written in place, so the
    // directories are watched.
    m_savedReload.setSingleShot(true);
    m_savedReload.setInterval(SavedReloadDelayMs);
    connect(&m_savedReload, &QTimer::timeout, this, &Lighting::reloadSaved);
    const QString stateDir = QFileInfo(FanState::filePath()).absolutePath();
    QStringList dirs{stateDir, QFileInfo(Settings::filePath()).absolutePath(), Profiles::directory()};
    dirs.removeIf([](const QString &dir) {
        return !QDir().mkpath(dir);
    });
    auto *watcher = new QFileSystemWatcher(dirs, this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this, watcher, stateDir](const QString &dir) {
        // A deleted directory is no longer watched: back it comes, empty.
        if (!watcher->directories().contains(dir) && QDir().mkpath(dir)) {
            watcher->addPath(dir);
        }
        m_settingsChanged = m_settingsChanged || dir != stateDir;
        m_savedReload.start();
    });

    // Not a lasting problem: the sensor may come back. The fan page and the
    // tray show it while it lasts.
    connect(&m_fanApplier, &FanApplier::temperatureUpdated, this, &Lighting::temperatureChanged);
    connect(&m_fanApplier, &FanApplier::temperatureScaleChanged, this, &Lighting::changed);
    connect(&m_fanApplier, &FanApplier::temperatureFailed, this, [this](const QString &text) {
        m_temperatureError = text;
        Q_EMIT temperatureChanged();
    });
    connect(&m_fanApplier, &FanApplier::temperatureRecovered, this, [this] {
        m_temperatureError.clear();
        Q_EMIT temperatureChanged();
    });

    connect(&m_fanApplier, &FanApplier::failed, this, [this](const QString &text) {
        // Show what was last sent successfully, not values that never arrived.
        loadFans();
        setError(text, FanErrors);
        Q_EMIT changed();
    });
    connect(&m_fanApplier, &FanApplier::applied, this, [this] {
        clearError(FanErrors);
        if (!m_fanProblem.isEmpty()) { // the controller came back
            setProblem(m_fanProblem, {});
            Q_EMIT changed();
        }
    });
    // The LEDs show the change, so the window keeps showing it too.
    connect(&m_fanApplier, &FanApplier::saveFailed, this, [this](const QString &text) {
        setError(text, SaveErrors);
    });
    connect(&m_fanApplier, &FanApplier::saved, this, [this] {
        clearError(SaveErrors);
    });
    connect(&m_lightApplier, &LightApplier::succeeded, this, [this] {
        clearError(LightErrors);
    });
    connect(&m_lightApplier, &LightApplier::stateChanged, this, [this](const QList<EcLightState> &lights) {
        if (!m_windowOpen) {
            return; // a change made just before the window closed
        }
        for (const EcLightState &l : lights) {
            m_lights.insert(l.index, l);
        }
        m_lightsLoaded = true;
        m_lightsFailed = false;
        setProblem(m_lightProblem, {});
        Q_EMIT changed();
        checkFanLayout();
    });
    connect(&m_lightApplier, &LightApplier::failed, this, [this](const QString &text) {
        if (!m_windowOpen) {
            return;
        }
        if (m_lightsLoaded) {
            setError(text, LightErrors);
        } else {
            m_lightsFailed = true;
            setProblem(m_lightProblem, text);
            Q_EMIT changed();
        }
    });
    connect(&m_lightApplier, &LightApplier::idle, this, &Lighting::idle);
    connect(&m_lightApplier, &LightApplier::layoutRead, this, [this](int code) {
        m_fanLayout = code;
        setProblem(m_layoutProblem, EcLights::layoutWarning(m_model, code));
        Q_EMIT changed(); // mark undetected fans in the sidebar
    });
}

void Lighting::start(QWindow *window)
{
    if (!m_fanApplier.isOpen()) { // the first time, or the controller was missing
        QString error;
        m_fanApplier.open(&error);
        setProblem(m_fanProblem, error);
        Q_EMIT changed();
    }
    m_windowOpen = m_windowOpen || window;
    m_helper.setWindow(window);
    if (window && !m_model.ecLights.isEmpty() && !m_lightsLoaded) {
        m_lightsFailed = false; // try again with this window
        m_lightApplier.refresh();
    }
}

void Lighting::windowClosed()
{
    m_windowOpen = false;
    m_lights.clear();
    m_lightsLoaded = false;
    m_lightsFailed = false;
    m_lightApplier.forget();
    setProblem(m_lightProblem, {});
}

bool Lighting::fansAvailable() const
{
    return m_fanApplier.isOpen();
}

QStringList Lighting::problems() const
{
    QStringList list{m_fanProblem, m_lightProblem, m_layoutProblem};
    list.removeAll(QString());
    return list;
}

void Lighting::setProblem(QString &problem, const QString &text)
{
    if (problem != text) {
        problem = text;
        Q_EMIT problemsChanged();
    }
}

bool Lighting::followsCpuTemperature() const
{
    return m_fanApplier.followsTemperature();
}

std::optional<double> Lighting::cpuTemperature() const
{
    const CpuTemperature *t = m_fanApplier.temperature();
    return t && t->isRunning() ? t->temperature() : std::nullopt;
}

Ite8297::Rgb Lighting::cpuTemperatureColour() const
{
    const CpuTemperature *t = m_fanApplier.temperature();
    return t ? t->colour() : Ite8297::Rgb{};
}

const TemperatureScale *Lighting::temperatureScale() const
{
    const CpuTemperature *t = m_fanApplier.temperature();
    return t ? &t->scale() : nullptr;
}

std::optional<TemperatureRange> Lighting::temperatureRange() const
{
    const CpuTemperature *t = m_fanApplier.temperature();
    return t ? std::optional(t->range()) : std::nullopt;
}

void Lighting::setTemperatureRange(TemperatureRange range)
{
    if (temperatureRange().value_or(range) == range) {
        return; // the same, or no such mode
    }
    m_fanApplier.setTemperatureRange(range);
    m_rangeSave.start();
    Q_EMIT changed();
}

void Lighting::loadTemperatureRange()
{
    if (m_model.cpuTemperature) {
        m_fanApplier.setTemperatureRange(Settings::temperatureRange(*m_model.cpuTemperature));
    }
}

void Lighting::saveTemperatureRange()
{
    m_rangeSave.stop();
    if (const auto range = temperatureRange()) {
        Settings::setTemperatureRange(*range);
    }
}

// Something in the state or settings directories changed: the command line,
// most likely, or our own save, which reads back what we already show.
void Lighting::reloadSaved()
{
    if (isBusy()) {
        m_savedReload.start(); // after our own change is out
        return;
    }
    m_currentProfile = Settings::currentProfile();
    const FanState::Headers before = m_fans;
    const QMap<QString, bool> linksBefore = m_links;
    const auto rangeBefore = temperatureRange();
    const QMap<quint8, EcLightState> lightsBefore = m_lights;
    if (!m_rangeSave.isActive()) { // otherwise ours is newer
        loadTemperatureRange();
    }
    loadFans(); // also the split choices, from the settings
    takeCommandLineLights();
    if (std::exchange(m_settingsChanged, false)) {
        Q_EMIT savedChanged();
    }
    if (m_fans != before || m_links != linksBefore || temperatureRange() != rangeBefore || m_lights != lightsBefore) {
        Q_EMIT changed();
    }
}

// The command line records the case lights after it sets them. A window that
// knows them takes that over, without a call to the root helper.
// A record taken over (or older than the window) isn't taken again: the window
// may have changed the lights since.
void Lighting::takeCommandLineLights()
{
    const auto recorded = EcLights::loadState(m_model);
    if (!recorded || recorded->written == m_lightsSeen) {
        return;
    }
    m_lightsSeen = recorded->written;
    if (!m_lightsLoaded) {
        return; // without a window they're read when one opens
    }
    for (const EcLightState &l : recorded->lights) {
        m_lights.insert(l.index, l);
    }
    m_lightApplier.setKnown(recorded->lights);
}

// Read-only: tells whether the BIOS currently sees every fan. Never changes
// anything – a fan the BIOS misses still gets its settings.
void Lighting::checkFanLayout()
{
    if (m_layoutChecked) {
        return;
    }
    m_layoutChecked = true;
    m_lightApplier.readLayout(); // an error is not worth a message: the lights already work
}

bool Lighting::isUndetected(const FanDef &fan) const
{
    return m_model.undetectedFans(m_fanLayout).contains(&fan);
}

HeaderSetting Lighting::led(quint8 header) const
{
    return m_fans.value(header, {.header = header});
}

bool Lighting::isLinked(const FanDef &fan) const
{
    return m_links.value(fan.id, true);
}

// Choosing what an LED already shows sends nothing (and writes nothing).
void Lighting::setLed(const FanDef &fan, quint8 header, HeaderSetting values)
{
    QList<HeaderSetting> changedLeds;
    for (const LedDef &led : fan.leds) {
        if (led.header == header || (fan.isDouble() && isLinked(fan))) {
            HeaderSetting s = values;
            s.header = led.header;
            if (isKnown(led.header) && m_fans.value(led.header) == s) {
                continue;
            }
            m_fans.insert(led.header, s);
            changedLeds << s;
        }
    }
    if (changedLeds.isEmpty()) {
        return;
    }
    m_fanApplier.queue(changedLeds);
    Q_EMIT changed();
}

void Lighting::setLinked(const FanDef &fan, bool linked)
{
    const QString id = fan.id;
    m_links.insert(id, linked);
    Settings::setLinked(id, linked);
    const quint8 first = fan.leds.constFirst().header;
    if (linked && isKnown(first)) {
        setLed(fan, first, led(first)); // the first LED wins
        return;
    }
    Q_EMIT changed();
}

std::optional<EcLightState> Lighting::light(quint8 index) const
{
    if (!m_lightsLoaded || !m_lights.contains(index)) {
        return std::nullopt;
    }
    return m_lights.value(index);
}

void Lighting::setLight(quint8 index, quint8 mode, quint8 brightness)
{
    m_lights.insert(index, {index, mode, brightness});
    m_lightApplier.queue({index, mode, brightness}); // dropped if nothing changes
    Q_EMIT changed();
}

Profile Lighting::current() const
{
    Profile p;
    p.fans = m_fans;
    p.links = m_links;
    p.temperatureRange = temperatureRange();
    if (m_lightsLoaded) {
        p.lights = m_lights;
    }
    return p;
}

// Only what the profile contains counts, and the case lights only once their
// state is known.
bool Lighting::matches(const Profile &profile) const
{
    for (auto it = profile.fans.cbegin(); it != profile.fans.cend(); ++it) {
        if (!m_fans.contains(it.key()) || m_fans.value(it.key()) != it.value()) {
            return false;
        }
    }
    for (auto it = profile.links.cbegin(); it != profile.links.cend(); ++it) {
        if (m_links.value(it.key(), true) != it.value()) {
            return false;
        }
    }
    // Compared as used: values past a limit count as the limit.
    const TemperatureScale *scale = temperatureScale();
    if (const auto &def = m_model.cpuTemperature; def && scale && profile.temperatureRange) {
        if (usableRange(*def, *profile.temperatureRange, scale->top()) != scale->range()) {
            return false;
        }
    }
    if (m_lightsLoaded) { // otherwise unknown: they don't count
        for (const EcLightState &l : profile.lights) {
            if (m_lights.value(l.index) != l) {
                return false;
            }
        }
    }
    return true;
}

void Lighting::apply(const Profile &profile)
{
    for (const HeaderSetting &s : profile.fans) {
        m_fans.insert(s.header, s);
    }
    m_fanApplier.queue(profile.fans.values());
    // With the profile's fans queued, the new range's colour goes out with
    // them: the queued LEDs in the batch, the other following ones just before.
    if (profile.temperatureRange && temperatureRange()) {
        m_fanApplier.setTemperatureRange(*profile.temperatureRange);
    }
    m_fanApplier.flushNow();

    for (auto it = profile.links.cbegin(); it != profile.links.cend(); ++it) {
        m_links.insert(it.key(), it.value());
    }
    for (const EcLightState &l : profile.lights) {
        m_lights.insert(l.index, l);
        m_lightApplier.queue(l);
    }
    m_rangeSave.stop(); // the range goes with the rest, in one write
    m_currentProfile = profile.name;
    Settings::setApplied(profile.links, temperatureRange(), profile.name);
    Q_EMIT changed();
}

void Lighting::setCurrentProfile(const QString &name)
{
    m_currentProfile = name;
    Settings::setCurrentProfile(name);
}

void Lighting::turnAllOff()
{
    QList<HeaderSetting> off;
    for (const FanDef &fan : m_model.fans) {
        for (const LedDef &l : fan.leds) {
            HeaderSetting s = led(l.header); // colours and levels are kept
            s.mode = Mode::Off;
            m_fans.insert(l.header, s);
            off << s;
        }
    }
    m_fanApplier.queue(off);
    m_fanApplier.flushNow();

    if (m_lightsLoaded) {
        for (EcLightState &l : m_lights) {
            l.mode = EcLights::ModeOff;
            m_lightApplier.queue(l);
        }
    }
    Q_EMIT changed();
}

bool Lighting::isBusy() const
{
    return m_fanApplier.isBusy() || m_lightApplier.isBusy();
}

// What the LEDs show: the saved state, and what was sent but couldn't be saved.
void Lighting::loadFans()
{
    m_fans = FanState::load(m_model);
    m_fans.insert(m_fanApplier.unsaved());
    m_links = FanState::links(m_model, m_fans);
    m_fanApplier.setCurrent(m_fans);
}

void Lighting::setError(const QString &text, ErrorSource source)
{
    m_errorSource = source;
    Q_EMIT error(text);
}

void Lighting::clearError(ErrorSource source)
{
    if (m_errorSource == source) {
        m_errorSource = NoErrors;
        Q_EMIT errorCleared();
    }
}

void Lighting::finish()
{
    if (m_rangeSave.isActive()) {
        saveTemperatureRange();
    }
    m_fanApplier.flushNow();
    m_lightApplier.finish();
}

} // namespace Glowzone::Gui
