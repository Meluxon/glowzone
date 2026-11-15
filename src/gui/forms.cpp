// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "forms.h"
#include "colourbutton.h"

#include <KLocalizedString>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace Glowzone::Gui
{

using namespace Ite8297;

LevelSlider::LevelSlider(int min, int max, const QString &minText, const QString &maxText, QWidget *parent)
    : QWidget(parent)
    , m_slider(new QSlider(Qt::Horizontal, this))
{
    m_slider->setRange(min, max);
    m_slider->setPageStep(1);
    m_slider->setTickPosition(QSlider::TicksBelow);
    m_slider->setTickInterval(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_slider);
    auto *ends = new QHBoxLayout;
    auto *minLabel = new QLabel(minText, this);
    auto *maxLabel = new QLabel(maxText, this);
    for (QLabel *l : {minLabel, maxLabel}) {
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        l->setFont(f);
    }
    ends->addWidget(minLabel);
    ends->addStretch();
    ends->addWidget(maxLabel);
    layout->addLayout(ends);
    setFixedWidth(fontMetrics().averageCharWidth() * 28);
}

LedForm::LedForm(const ModelProfile &model, QFormLayout *form, QWidget *parent)
    : QObject(parent)
    , m_form(form)
    , m_mode(new QComboBox(parent))
    , m_colour(new ColourButton(parent))
    , m_speed(new LevelSlider(MinLevel, model.fanLevels, i18nc("@item:inrange", "Slow"), i18nc("@item:inrange", "Fast"), parent))
    , m_brightness(new LevelSlider(MinLevel, model.fanLevels, i18nc("@item:inrange", "Dim"), i18nc("@item:inrange", "Bright"), parent))
    , m_temperature(new QLabel(parent))
{
    for (const ModeInfo &m : modesFor(model)) {
        m_mode->addItem(m.name.toString(), int(m.mode));
    }
    m_mode->setPlaceholderText(i18nc("@item fan mode not known yet", "Not set yet"));
    m_mode->setCurrentIndex(-1);

    form->addRow(i18nc("@label:listbox", "Mode:"), m_mode);
    form->addRow(i18nc("@label CPU temperature now", "Temperature:"), m_temperature);
    form->addRow(i18nc("@label:chooser", "Colour:"), m_colour);
    form->addRow(i18nc("@label:slider", "Speed:"), m_speed);
    form->addRow(i18nc("@label:slider", "Brightness:"), m_brightness);

    connect(m_mode, &QComboBox::activated, this, [this] {
        updateRows();
        emitEdited();
    });
    connect(m_colour, &ColourButton::colourEdited, this, &LedForm::emitEdited);
    connect(m_speed->slider(), &QSlider::valueChanged, this, &LedForm::emitEdited);
    connect(m_brightness->slider(), &QSlider::valueChanged, this, &LedForm::emitEdited);
    updateRows();
}

void LedForm::setValues(HeaderSetting values, bool known)
{
    const QSignalBlocker b1(m_mode), b2(m_colour), b3(m_speed->slider()), b4(m_brightness->slider());
    m_mode->setCurrentIndex(known ? m_mode->findData(int(values.mode)) : -1);
    m_colour->setColour(values.colour);
    m_speed->slider()->setValue(values.speed);
    m_brightness->slider()->setValue(values.brightness);
    updateRows();
}

void LedForm::setTemperature(std::optional<double> celsius, const QString &error)
{
    if (!error.isEmpty()) {
        m_temperature->setText(i18nc("@info CPU temperature", "Not available"));
        m_temperature->setToolTip(error);
        return;
    }
    m_temperature->setToolTip(i18nc("@info:tooltip", "The colour shows how warm the CPU is. It only changes while Glowzone is running."));
    m_temperature->setText(celsius ? i18nc("@info CPU temperature in degrees Celsius", "%1 °C", QString::number(*celsius, 'f', 0))
                                   : i18nc("@info CPU temperature not read yet", "Reading…"));
}

HeaderSetting LedForm::values() const
{
    HeaderSetting s;
    s.mode = m_mode->currentIndex() >= 0 ? Mode(m_mode->currentData().toInt()) : Mode::Off;
    s.speed = quint8(m_speed->slider()->value());
    s.brightness = quint8(m_brightness->slider()->value());
    s.colour = m_colour->colour();
    return s;
}

void LedForm::setEnabled(bool enabled)
{
    for (QWidget *w : std::initializer_list<QWidget *>{m_mode, m_colour, m_speed, m_brightness, m_temperature}) {
        w->setEnabled(enabled);
    }
}

void LedForm::setVisible(bool visible)
{
    m_visible = visible;
    updateRows();
}

QWidget *LedForm::focusWidget() const
{
    return m_mode;
}

void LedForm::updateRows()
{
    const ModeInfo *info = m_mode->currentIndex() >= 0 ? modeInfo(Mode(m_mode->currentData().toInt())) : nullptr;
    const bool lit = info && info->mode != Mode::Off;
    m_form->setRowVisible(m_mode, m_visible);
    m_form->setRowVisible(m_temperature, m_visible && info && info->followsTemperature);
    m_form->setRowVisible(m_colour, m_visible && lit && info->usesColour);
    m_form->setRowVisible(m_speed, m_visible && lit && info->usesSpeed);
    m_form->setRowVisible(m_brightness, m_visible && lit);
}

void LedForm::emitEdited()
{
    if (m_mode->currentIndex() >= 0) {
        Q_EMIT edited(values());
    }
}

QWidget *centredForm(QFormLayout **form, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *outer = new QVBoxLayout(page);
    auto *row = new QHBoxLayout;
    auto *inner = new QWidget(page);
    *form = new QFormLayout(inner);
    (*form)->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    (*form)->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    (*form)->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    row->addStretch();
    row->addWidget(inner);
    row->addStretch();
    outer->addLayout(row);
    outer->addStretch();
    return page;
}

} // namespace Glowzone::Gui
