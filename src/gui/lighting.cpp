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

enum ErrorSource { FanErrors = 1, LightErrors = 2 };

Lighting::Lighting(const ModelProfile &model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_helper(std::make_unique<KAuthHelperCalls>())
    , m_fanApplier(new FanApplier(model, this))
    , m_lightApplier(new LightApplier(*m_helper, this))
{
    loadFans();

    // The command line may change things while Glowzone runs – maybe only in
    // the tray, with LEDs following the CPU temperature: the fan state, the
    // split choices and current profile (glowzone.conf), the profiles. They are
    // read again when they change. Files are replaced, not written in place, so
    // the directories are watched.
    m_savedReload.setSingleShot(true);
    m_savedReload.setInterval(300); // a command writes several files
    connect(&m_savedReload, &QTimer::timeout, this, &Lighting::reloadSaved);
    const QString stateDir = QFileInfo(FanState::filePath()).absolutePath();
    QStringList dirs{stateDir, QFileInfo(Settings::filePath()).absolutePath(), Profiles::directory()};
    dirs.removeIf([](const QString &dir) {
        return !QDir().mkpath(dir);
    });
    auto *watcher = new QFileSystemWatcher(dirs, this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this, stateDir](const QString &dir) {
        m_settingsChanged = m_settingsChanged || dir != stateDir;
        m_savedReload.start();
    });

    // Not a lasting problem: the sensor may come back. The fan page and the
    // tray show it while it lasts.
    connect(m_fanApplier, &FanApplier::temperatureUpdated, this, &Lighting::temperatureChanged);
    connect(m_fanApplier, &FanApplier::temperatureFailed, this, [this](const QString &text) {
        m_temperatureError = text;
        Q_EMIT temperatureChanged();
    });
    connect(m_fanApplier, &FanApplier::temperatureRecovered, this, [this] {
        m_temperatureError.clear();
        Q_EMIT temperatureChanged();
    });

    connect(m_fanApplier, &FanApplier::failed, this, [this](const QString &text) {
        // Show what was last sent successfully, not values that never arrived.
        loadFans();
        setError(text, FanErrors);
        Q_EMIT changed();
    });
    connect(m_fanApplier, &FanApplier::applied, this, [this] {
        clearError(FanErrors);
    });
    connect(m_fanApplier, &FanApplier::saved, this, [this] {
        m_stateStamp = QFileInfo(FanState::filePath()).lastModified(); // no need to read it back
    });
    connect(m_lightApplier, &LightApplier::succeeded, this, [this] {
        clearError(LightErrors);
    });
    connect(m_lightApplier, &LightApplier::stateChanged, this, [this](const QList<EcLightState> &lights) {
        for (const EcLightState &l : lights) {
            m_lights.insert(l.index, l);
        }
        m_lightsLoaded = true;
        m_lightsFailed = false;
        setProblem(m_lightProblem, {});
        Q_EMIT changed();
        checkFanLayout();
    });
    connect(m_lightApplier, &LightApplier::failed, this, [this](const QString &text) {
        if (m_lightsLoaded) {
            setError(text, LightErrors);
        } else {
            m_lightsFailed = true;
            setProblem(m_lightProblem, text);
            Q_EMIT changed();
        }
    });
    connect(m_lightApplier, &LightApplier::idle, this, &Lighting::idle);
    connect(m_lightApplier, &LightApplier::layoutRead, this, [this](int code) {
        m_fanLayout = code;
        setProblem(m_layoutProblem, EcLights::layoutWarning(m_model, code));
        Q_EMIT changed(); // mark undetected fans in the sidebar
    });
}

void Lighting::start(QWindow *window)
{
    if (!m_fanApplier->isOpen()) { // the first time, or the controller was missing
        QString error;
        m_fansAvailable = m_fanApplier->open(&error);
        setProblem(m_fanProblem, error);
        Q_EMIT changed();
    }
    m_helper->setWindow(window);
    if (window && !m_model.ecLights.isEmpty() && !m_lightsLoaded && !m_lightApplier->isBusy()) {
        m_lightsFailed = false; // try again with this window
        m_lightApplier->refresh();
    }
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
    return m_fanApplier->followsTemperature();
}

std::optional<double> Lighting::cpuTemperature() const
{
    const CpuTemperature *t = m_fanApplier->temperature();
    return t && t->isRunning() ? t->temperature() : std::nullopt;
}

Ite8297::Rgb Lighting::cpuTemperatureColour() const
{
    const CpuTemperature *t = m_fanApplier->temperature();
    return t ? t->colour() : Ite8297::Rgb{};
}

// Something in the state directory changed – the command line, most likely.
// Our own saves land here as well; they read back what we already show.
void Lighting::reloadSaved()
{
    if (m_fanApplier->isBusy()) {
        m_savedReload.start(); // after our own change is out
        return;
    }
    const bool settingsChanged = std::exchange(m_settingsChanged, false);
    if (!settingsChanged && QFileInfo(FanState::filePath()).lastModified() == m_stateStamp) {
        return; // our own save, the lock file, ...
    }
    const FanState::Headers before = m_fans;
    const QMap<QString, bool> linksBefore = m_links;
    loadFans(); // also the split choices, from the settings
    Q_EMIT savedChanged();
    if (m_fans != before || m_links != linksBefore) {
        Q_EMIT changed();
    }
}

// Read-only: tells whether the BIOS currently sees every fan. Never changes
// anything – a fan the BIOS misses still gets its settings.
void Lighting::checkFanLayout()
{
    if (m_layoutChecked) {
        return;
    }
    m_layoutChecked = true;
    m_lightApplier->readLayout(); // an error is not worth a message: the lights already work
}

bool Lighting::isUndetected(const FanDef &fan) const
{
    return m_model.undetectedFans(m_fanLayout).contains(&fan);
}

HeaderSetting Lighting::led(quint8 header) const
{
    return m_fans.value(header, initialSetting(header));
}

bool Lighting::isLinked(const FanDef &fan) const
{
    return m_links.value(QLatin1String(fan.id), true);
}

void Lighting::setLed(const FanDef &fan, quint8 header, HeaderSetting values)
{
    QList<HeaderSetting> changedLeds;
    for (const LedDef &led : fan.leds) {
        if (led.header == header || (fan.isDouble() && isLinked(fan))) {
            HeaderSetting s = values;
            s.header = led.header;
            m_fans.insert(led.header, s);
            changedLeds << s;
        }
    }
    m_fanApplier->queue(changedLeds);
    Q_EMIT changed();
}

void Lighting::setLinked(const FanDef &fan, bool linked)
{
    const QString id = QLatin1String(fan.id);
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
    m_lightApplier->queue({index, mode, brightness}); // dropped if nothing changes
    Q_EMIT changed();
}

Profile Lighting::current() const
{
    Profile p;
    p.fans = m_fans;
    p.links = m_links;
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
    if (m_lightsLoaded) {
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
    m_fanApplier->queue(profile.fans.values());
    m_fanApplier->flushNow();

    for (auto it = profile.links.cbegin(); it != profile.links.cend(); ++it) {
        m_links.insert(it.key(), it.value());
        Settings::setLinked(it.key(), it.value());
    }
    for (const EcLightState &l : profile.lights) {
        m_lights.insert(l.index, l);
        m_lightApplier->queue({l.index, l.mode, l.brightness});
    }
    Settings::setCurrentProfile(profile.name);
    Q_EMIT changed();
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
    m_fanApplier->queue(off);
    m_fanApplier->flushNow();

    if (m_lightsLoaded) {
        for (EcLightState &l : m_lights) {
            l.mode = EcLights::ModeOff;
            m_lightApplier->queue({l.index, l.mode, l.brightness});
        }
    }
    Q_EMIT changed();
}

bool Lighting::isBusy() const
{
    return m_fanApplier->isBusy() || m_lightApplier->isBusy();
}

void Lighting::loadFans()
{
    m_stateStamp = QFileInfo(FanState::filePath()).lastModified();
    const FanState::Headers saved = FanState::load(m_model);
    m_fans.clear();
    for (const FanDef &fan : m_model.fans) {
        for (const LedDef &led : fan.leds) {
            if (saved.contains(led.header)) {
                m_fans.insert(led.header, saved.value(led.header));
            }
        }
        if (fan.isDouble()) {
            const QString id = QLatin1String(fan.id);
            m_links.insert(id, Settings::linked(id).value_or(FanState::defaultLinked(fan, saved)));
        }
    }
    m_fanApplier->setCurrent(m_fans);
}

void Lighting::reloadFromDisk()
{
    if (isBusy()) {
        return;
    }
    loadFans();
    if (m_lightsLoaded) {
        m_lightApplier->refresh();
    }
    Q_EMIT changed();
}

void Lighting::setError(const QString &text, int source)
{
    m_errorSource = source;
    Q_EMIT error(text);
}

void Lighting::clearError(int source)
{
    if (m_errorSource == source) {
        m_errorSource = 0;
        Q_EMIT errorCleared();
    }
}

void Lighting::finish()
{
    m_fanApplier->flushNow();
    m_lightApplier->finish();
}

} // namespace Glowzone::Gui
