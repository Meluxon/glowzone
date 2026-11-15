// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pages.h"
#include "forms.h"
#include "lighting.h"

#include "core/eclights.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace Glowzone::Gui
{

QString capitalised(const QString &s)
{
    return s.isEmpty() ? s : s.at(0).toUpper() + s.mid(1);
}

static QLabel *sectionHeading(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    l->setContentsMargins(0, l->fontMetrics().height() / 2, 0, 0);
    return l;
}

FanPage::FanPage(const FanDef &fan, Lighting *lighting, QWidget *parent)
    : QWidget(parent)
    , m_fan(fan)
    , m_lighting(lighting)
{
    QFormLayout *form = nullptr;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(centredForm(&form, this));

    if (fan.isDouble()) {
        m_separately = new QCheckBox(i18nc("@option:check LEDs of a double fan", "Set separately"), this);
        m_separately->setToolTip(i18nc("@info:tooltip", "Give each fan of the pair its own settings. Unchecking copies the first fan’s settings to the other."));
        form->addRow(i18nc("@label LEDs of a double fan", "LEDs:"), m_separately);
        connect(m_separately, &QCheckBox::toggled, this, [this](bool on) {
            m_lighting->setLinked(m_fan, !on);
        });
    }

    for (const LedDef &led : fan.leds) {
        QLabel *heading = nullptr;
        if (fan.isDouble()) {
            heading = sectionHeading(capitalised(i18nc("@title one fan of a pair, %1 is its position", "%1 fan", led.name.toString())), this);
            form->addRow(heading);
        }
        auto *ledForm = new LedForm(m_lighting->model(), form, this);
        const quint8 header = led.header;
        connect(ledForm, &LedForm::edited, this, [this, header](Ite8297::HeaderSetting values) {
            m_lighting->setLed(m_fan, header, values);
        });
        m_headings << heading;
        m_forms << ledForm;
    }
    refresh();
}

void FanPage::refresh()
{
    const bool linked = m_fan.isDouble() && m_lighting->isLinked(m_fan);
    if (m_separately) {
        const QSignalBlocker blocker(m_separately);
        m_separately->setChecked(!linked);
        m_separately->setEnabled(m_lighting->fansAvailable());
    }
    for (int i = 0; i < m_forms.size(); ++i) {
        const quint8 header = m_fan.leds.at(i).header;
        const bool shown = !linked || i == 0;
        m_forms.at(i)->setValues(m_lighting->led(header), m_lighting->isKnown(header));
        m_forms.at(i)->setVisible(shown);
        m_forms.at(i)->setEnabled(m_lighting->fansAvailable());
        if (m_headings.at(i)) {
            m_headings.at(i)->setVisible(!linked);
        }
    }
    refreshTemperature();
}

void FanPage::refreshTemperature()
{
    for (LedForm *f : std::as_const(m_forms)) {
        f->setTemperature(m_lighting->cpuTemperature(), m_lighting->cpuTemperatureError());
    }
}

void FanPage::focusLed(quint8 header)
{
    for (int i = 0; i < m_fan.leds.size(); ++i) {
        if (m_fan.leds.at(i).header == header) {
            m_forms.at(i)->focusWidget()->setFocus(Qt::OtherFocusReason);
        }
    }
}

LightPage::LightPage(const EcLightDef &light, Lighting *lighting, QWidget *parent)
    : QWidget(parent)
    , m_light(light)
    , m_lighting(lighting)
    , m_mode(new QComboBox(this))
    , m_brightness(new LevelSlider(EcLights::MinBrightness, lighting->model().ecBrightnessLevels, i18nc("@item:inrange", "Dim"), i18nc("@item:inrange", "Bright"), this))
    , m_waiting(new QLabel(i18nc("@info", "Reading the current state…"), this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(centredForm(&m_form, this));

    for (const EcLights::ModeInfo &m : EcLights::modesFor(lighting->model())) {
        m_mode->addItem(m.name.toString(), int(m.value));
    }
    m_mode->setPlaceholderText(i18nc("@item light mode not known", "Unknown"));
    // Every change makes the BIOS run an SMI that can stall the PC for a moment –
    // so the slider applies on release, not while dragging.
    m_brightness->slider()->setTracking(false);

    m_form->addRow(m_waiting);
    m_form->addRow(i18nc("@label:listbox", "Mode:"), m_mode);
    m_form->addRow(i18nc("@label:slider", "Brightness:"), m_brightness);

    connect(m_mode, &QComboBox::activated, this, &LightPage::edited);
    connect(m_brightness->slider(), &QSlider::valueChanged, this, &LightPage::edited);
    refresh();
}

void LightPage::refresh()
{
    const auto state = m_lighting->light(m_light.index);
    const QSignalBlocker b1(m_mode), b2(m_brightness->slider());
    m_mode->setCurrentIndex(state ? m_mode->findData(int(state->mode)) : -1);
    if (state) {
        m_brightness->slider()->setValue(state->brightness);
    }
    m_waiting->setText(m_lighting->lightsFailed() ? i18nc("@info", "Not available – see the message below.")
                                                  : i18nc("@info", "Reading the current state…"));
    m_form->setRowVisible(m_waiting, !m_lighting->lightsAvailable());
    m_mode->setEnabled(state.has_value());
    m_form->setRowVisible(m_brightness, state && state->mode != EcLights::ModeOff);
}

void LightPage::edited()
{
    if (m_mode->currentIndex() < 0) {
        return;
    }
    m_lighting->setLight(m_light.index, quint8(m_mode->currentData().toUInt()), quint8(m_brightness->slider()->value()));
}

} // namespace Glowzone::Gui
