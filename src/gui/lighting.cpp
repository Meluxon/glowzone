// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lighting.h"

#include "core/appliers.h"
#include "core/eclights.h"
#include "core/settings.h"

namespace Glowzone::Gui
{

using namespace Ite8297;

Lighting::Lighting(const ModelProfile &model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_helper(std::make_unique<KAuthHelperCalls>())
    , m_fanApplier(new FanApplier(model, this))
    , m_lightApplier(new LightApplier(*m_helper, this))
{
    const FanState::Headers saved = FanState::load(model);
    for (const FanDef &fan : model.fans) {
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

    connect(m_fanApplier, &FanApplier::failed, this, &Lighting::error);
    connect(m_fanApplier, &FanApplier::applied, this, &Lighting::errorCleared);
    connect(m_lightApplier, &LightApplier::stateChanged, this, [this](const QList<EcLightState> &lights) {
        for (const EcLightState &l : lights) {
            m_lights.insert(l.index, l);
        }
        m_lightsLoaded = true;
        Q_EMIT changed();
        checkFanLayout(); // the helper does one thing at a time
    });
    connect(m_lightApplier, &LightApplier::failed, this, [this](const QString &text) {
        if (m_lightsLoaded) {
            Q_EMIT error(text);
        } else {
            m_lightsFailed = true;
            Q_EMIT problem(text);
            Q_EMIT changed();
        }
    });
    connect(m_lightApplier, &LightApplier::idle, this, &Lighting::idle);
}

void Lighting::start(QWindow *window)
{
    m_helper->setWindow(window);
    QString error;
    if (!m_fanApplier->open(&error)) {
        m_fansAvailable = false;
        Q_EMIT problem(error);
        Q_EMIT changed();
    }
    if (!m_model.ecLights.isEmpty()) {
        m_lightApplier->refresh();
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
    HelperCall *call = m_helper->create(HelperCall::Layout, {}, this);
    connect(call, &HelperCall::finished, this, [this](const HelperResult &r) {
        // An error here is not worth a message: the lights already work.
        if (r.ok()) {
            m_fanLayout = r.layout;
            const QString warning = EcLights::layoutWarning(m_model, r.layout);
            if (!warning.isEmpty()) {
                Q_EMIT problem(warning);
                Q_EMIT changed(); // mark the fans in the sidebar
            }
        }
    });
    call->start();
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
    return m_lightApplier->isBusy();
}

void Lighting::finish()
{
    m_fanApplier->flushNow();
    m_lightApplier->finish();
}

} // namespace Glowzone::Gui
