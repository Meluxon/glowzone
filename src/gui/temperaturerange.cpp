// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "temperaturerange.h"
#include "forms.h"

#include "core/cputemperature.h"

#include <KLocalizedString>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>

namespace Glowzone::Gui
{

static QFont smallFont(const QWidget *w)
{
    QFont f = w->font();
    f.setPointSizeF(f.pointSizeF() * 0.9); // like the words under a LevelSlider
    return f;
}

static QString celsiusText(int celsius)
{
    return i18nc("@item temperature in degrees Celsius", "%1 °C", celsius);
}

TemperatureStrip::TemperatureStrip(const QSlider *alignWith, QWidget *parent)
    : QWidget(parent)
    , m_alignWith(alignWith)
{
    setToolTip(i18nc("@info:tooltip", "The colour for each CPU temperature. The marker shows the temperature now."));
}

void TemperatureStrip::setScale(const TemperatureScale *scale, int minimum, int maximum)
{
    m_scale = scale;
    m_minimum = minimum;
    m_maximum = std::max(maximum, minimum + 1);
    update();
}

void TemperatureStrip::setTemperature(std::optional<double> celsius)
{
    if (celsius != m_temperature) {
        m_temperature = celsius;
        update();
    }
}

int TemperatureStrip::stripTop() const
{
    return 7; // room for the marker's tip
}

int TemperatureStrip::stripHeight() const
{
    return std::max(10, fontMetrics().height() * 3 / 4);
}

QSize TemperatureStrip::sizeHint() const
{
    const QFontMetrics small(smallFont(this));
    return {m_alignWith->width(), stripTop() + stripHeight() + 2 + small.height()};
}

// Where a slider of the same width puts its handle's centre for this value.
double TemperatureStrip::xFor(double celsius) const
{
    QStyleOptionSlider opt;
    opt.initFrom(m_alignWith);
    opt.rect = QRect(0, 0, width(), m_alignWith->height());
    opt.orientation = Qt::Horizontal;
    opt.minimum = m_minimum;
    opt.maximum = m_maximum;
    const QStyle *style = m_alignWith->style();
    const QRect groove = style->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, m_alignWith);
    const int handle = style->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, m_alignWith).width();
    const double span = std::max(1, groove.width() - handle);
    const double f = std::clamp((celsius - m_minimum) / double(m_maximum - m_minimum), 0.0, 1.0);
    return groove.left() + handle / 2.0 + f * span;
}

void TemperatureStrip::paintEvent(QPaintEvent *)
{
    if (!m_scale) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double x0 = xFor(m_minimum);
    const double x1 = xFor(m_maximum);
    const QRectF strip(x0, stripTop(), x1 - x0, stripHeight());

    QPainterPath shape;
    shape.addRoundedRect(strip, 3, 3);
    p.save();
    p.setClipPath(shape);
    for (int x = int(std::floor(x0)); x < int(std::ceil(x1)); ++x) {
        const double celsius = m_minimum + (x + 0.5 - x0) / (x1 - x0) * (m_maximum - m_minimum);
        const Ite8297::Rgb c = m_scale->colourAt(celsius);
        p.fillRect(QRectF(x, strip.top(), 1, strip.height()), QColor(c.r, c.g, c.b));
    }
    p.restore();
    QColor frame = palette().color(QPalette::WindowText);
    frame.setAlphaF(0.25);
    p.setPen(QPen(frame, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);

    // °C labels every 20, inside the widget.
    p.setFont(smallFont(this));
    p.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics small(p.font());
    const int baseline = int(strip.bottom()) + 2 + small.ascent();
    for (int t = (m_minimum + 19) / 20 * 20; t <= m_maximum; t += 20) {
        const QString text = QString::number(t);
        const int w = small.horizontalAdvance(text);
        const int left = std::clamp(int(std::lround(xFor(t) - w / 2.0)), 0, width() - w);
        p.drawText(left, baseline, text);
    }

    if (m_temperature) {
        const double x = xFor(*m_temperature);
        const QColor ink = palette().color(QPalette::WindowText);
        // The line gets a halo in the window colour, to show on dark and light colours alike.
        p.setPen(QPen(palette().color(QPalette::Window), 4, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, strip.top() + 1), QPointF(x, strip.bottom() - 1));
        p.setPen(QPen(ink, 2, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, strip.top()), QPointF(x, strip.bottom()));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        const QPointF tip[] = {{x - 4.5, 0}, {x + 4.5, 0}, {x, stripTop() - 1.0}};
        p.drawPolygon(tip, 3);
    }
}

TemperatureRangeForm::TemperatureRangeForm(const CpuTemperatureDef &def, QFormLayout *form, QWidget *parent)
    : QObject(parent)
    , m_def(def)
    , m_form(form)
    , m_heading(sectionHeading(i18nc("@title", "Temperature colours"), parent))
    , m_coolest(new QSlider(Qt::Horizontal, parent))
    , m_hottest(new QSlider(Qt::Horizontal, parent))
    , m_coolestValue(new QLabel(parent))
    , m_hottestValue(new QLabel(parent))
    , m_note(new QLabel(i18nc("@info", "Shared by all fans in this mode."), parent))
{
    const int width = parent->fontMetrics().averageCharWidth() * 28; // like a LevelSlider
    const int valueWidth = parent->fontMetrics().horizontalAdvance(celsiusText(188));
    for (QSlider *s : {m_coolest, m_hottest}) {
        s->setFixedWidth(width);
        s->setSingleStep(1);
        s->setPageStep(5);
    }
    m_coolest->setToolTip(i18nc("@info:tooltip", "The first colour starts here. Cooler than this, it fades towards dark."));
    m_hottest->setToolTip(i18nc("@info:tooltip", "The last colour is reached here. Hotter than this, it fades towards white."));

    // Each field: the slider (or the strip) and room for a value on its right,
    // so the strip's axis lines up with the sliders'.
    auto field = [parent, valueWidth](QWidget *main, QLabel *value) {
        auto *w = new QWidget(parent);
        auto *row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(main);
        if (value) {
            value->setFixedWidth(valueWidth);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row->addWidget(value);
        } else {
            row->addSpacing(valueWidth + row->spacing());
        }
        return w;
    };
    m_strip = new TemperatureStrip(m_coolest, parent);
    m_strip->setFixedWidth(width);
    m_stripField = field(m_strip, nullptr);
    m_coolestField = field(m_coolest, m_coolestValue);
    m_hottestField = field(m_hottest, m_hottestValue);

    m_note->setFont(smallFont(parent));

    form->addRow(m_heading);
    form->addRow(i18nc("@label colours for each CPU temperature", "Colours:"), m_stripField);
    form->addRow(i18nc("@label:slider temperature where the colours start", "Coolest:"), m_coolestField);
    form->addRow(i18nc("@label:slider temperature where the colours end", "Hottest:"), m_hottestField);
    form->addRow(QString(), m_note);

    connect(m_coolest, &QSlider::valueChanged, this, [this] {
        sliderMoved(m_coolest);
    });
    connect(m_hottest, &QSlider::valueChanged, this, [this] {
        sliderMoved(m_hottest);
    });
}

void TemperatureRangeForm::setScale(const TemperatureScale &scale)
{
    const QSignalBlocker b1(m_coolest), b2(m_hottest);
    for (QSlider *s : {m_coolest, m_hottest}) {
        s->setRange(m_def.darkAt, scale.top()); // one axis for both, the strip's
    }
    m_coolest->setValue(scale.range().coolest);
    m_hottest->setValue(scale.range().hottest);
    m_strip->setScale(&scale, m_def.darkAt, scale.top());
    updateLabels();
}

void TemperatureRangeForm::setTemperature(std::optional<double> celsius)
{
    m_strip->setTemperature(celsius);
}

// The two can't come closer than one °C per colour: the one moved stops there.
void TemperatureRangeForm::sliderMoved(QSlider *moved)
{
    const int gap = minimumRangeWidth(m_def);
    if (m_hottest->value() - m_coolest->value() < gap) {
        const QSignalBlocker blocker(moved);
        moved->setValue(moved == m_coolest ? m_hottest->value() - gap : m_coolest->value() + gap);
    }
    updateLabels();
    Q_EMIT edited({m_coolest->value(), m_hottest->value()});
}

void TemperatureRangeForm::updateLabels()
{
    m_coolestValue->setText(celsiusText(m_coolest->value()));
    m_hottestValue->setText(celsiusText(m_hottest->value()));
}

void TemperatureRangeForm::setVisible(bool visible, bool withHeading)
{
    m_form->setRowVisible(m_heading, visible && withHeading);
    for (QWidget *w : {m_stripField, m_coolestField, m_hottestField, static_cast<QWidget *>(m_note)}) {
        m_form->setRowVisible(w, visible);
    }
}

void TemperatureRangeForm::setEnabled(bool enabled)
{
    for (QWidget *w : {m_stripField, m_coolestField, m_hottestField}) {
        w->setEnabled(enabled);
    }
}

} // namespace Glowzone::Gui
