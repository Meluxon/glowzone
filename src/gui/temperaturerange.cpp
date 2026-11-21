// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "temperaturerange.h"
#include "forms.h"

#include "core/cputemperature.h"

#include <KLocalizedString>
#include <KStandardGuiItem>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace Glowzone::Gui
{

// The strip, top to bottom: the marker's tip, the colours, the range bracket,
// the °C labels.
static constexpr int MarkerHeight = 7;
static constexpr double MarkerHalfWidth = 4.5;
static constexpr int BracketHeight = 6;
static constexpr int MinStripHeight = 10;
static constexpr double StripHeightPerLine = 0.75; // of the text's line height
static constexpr double BracketPenWidth = 1.5;
static constexpr double MarkerLineWidth = 2;
static constexpr double MarkerHaloWidth = 4; // the line plus 1 px on each side
static constexpr double StripCornerRadius = 3;
static constexpr double FrameOpacity = 0.25;
static constexpr int LabelEvery = 20; // °C

// The sliders. Room for the widest value next to them: three digits.
static constexpr int WidestValue = 888;
static constexpr int SliderPageStep = 5; // °C per Page Up/Down

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
    return MarkerHeight;
}

int TemperatureStrip::stripHeight() const
{
    return std::max(MinStripHeight, int(fontMetrics().height() * StripHeightPerLine));
}

QSize TemperatureStrip::sizeHint() const
{
    const QFontMetrics small(smallFont(font()));
    return {m_alignWith->width(), stripTop() + stripHeight() + BracketHeight + small.height()};
}

// Where a slider of the same width has its groove, and its handle's centre at
// the minimum and the maximum – as the style places them (styles differ in
// whether the handle travels within the groove or the whole slider).
TemperatureStrip::Axis TemperatureStrip::axis() const
{
    QStyleOptionSlider opt;
    opt.initFrom(m_alignWith);
    opt.rect = QRect(0, 0, width(), m_alignWith->height());
    opt.orientation = Qt::Horizontal;
    opt.minimum = m_minimum;
    opt.maximum = m_maximum;
    const QStyle *style = m_alignWith->style();
    const QRect groove = style->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, m_alignWith);
    const auto handleCentre = [&](int value) {
        opt.sliderPosition = value;
        opt.sliderValue = value;
        return QRectF(style->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, m_alignWith)).center().x();
    };
    const double first = handleCentre(m_minimum);
    const double last = std::max(first + 1, handleCentre(m_maximum));
    return {double(groove.left()), double(groove.right() + 1), first, last, m_minimum, m_maximum};
}

void TemperatureStrip::paintEvent(QPaintEvent *)
{
    if (!m_scale) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Axis a = axis();
    const QRectF strip(a.left, stripTop(), a.right - a.left, stripHeight());

    // One image pixel per screen pixel, drawn in one go: rectangle by rectangle
    // leaves seams at fractional scale factors. Past the handles' reach (the
    // groove's ends) it shows the end colours, which hold beyond the axis too.
    const double dpr = devicePixelRatioF();
    const int pixels = std::max(1, int(std::lround(strip.width() * dpr)));
    QImage colours(pixels, 1, QImage::Format_RGB32);
    for (int i = 0; i < pixels; ++i) {
        const double x = strip.left() + (i + 0.5) / dpr;
        const Ite8297::Rgb c = m_scale->colourAt(a.celsius(x));
        colours.setPixel(i, 0, qRgb(c.r, c.g, c.b));
    }
    QPainterPath shape;
    shape.addRoundedRect(strip, StripCornerRadius, StripCornerRadius);
    p.save();
    p.setClipPath(shape);
    p.drawImage(strip, colours);
    p.restore();
    QColor frame = palette().color(QPalette::WindowText);
    frame.setAlphaF(FrameOpacity);
    p.setPen(QPen(frame, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);

    // The range: a bracket under the strip, in the selection colour like the
    // sliders' filled grooves.
    const QColor accent = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Highlight);
    const double from = std::round(a.x(m_scale->range().coolest)) + 0.5;
    const double to = std::round(a.x(m_scale->range().hottest)) - 0.5;
    const double top = strip.bottom() + 1;
    const double bottom = strip.bottom() + BracketHeight - 2; // 1 px apart from the labels
    p.setPen(QPen(accent, BracketPenWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    const QPointF bracket[] = {{from, top}, {from, bottom}, {to, bottom}, {to, top}};
    p.drawPolyline(bracket, int(std::size(bracket)));

    // °C labels, inside the widget.
    p.setFont(smallFont(font()));
    p.setPen(palette().color(QPalette::WindowText));
    const QFontMetrics small(p.font());
    const int baseline = int(strip.bottom()) + BracketHeight + small.ascent();
    const int firstLabel = (m_minimum + LabelEvery - 1) / LabelEvery * LabelEvery;
    for (int t = firstLabel; t <= m_maximum; t += LabelEvery) {
        const QString text = QString::number(t);
        const int w = small.horizontalAdvance(text);
        const int left = std::clamp(int(std::lround(a.x(t) - w / 2.0)), 0, width() - w);
        p.drawText(left, baseline, text);
    }

    if (m_temperature) {
        const double x = a.x(std::clamp(*m_temperature, double(m_minimum), double(m_maximum)));
        const QColor ink = palette().color(QPalette::WindowText);
        // The line gets a halo in the window colour, to show on dark and light colours alike.
        p.setPen(QPen(palette().color(QPalette::Window), MarkerHaloWidth, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, strip.top() + 1), QPointF(x, strip.bottom() - 1));
        p.setPen(QPen(ink, MarkerLineWidth, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, strip.top()), QPointF(x, strip.bottom()));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        const QPointF tip[] = {{x - MarkerHalfWidth, 0}, {x + MarkerHalfWidth, 0}, {x, stripTop() - 1.0}};
        p.drawPolygon(tip, int(std::size(tip)));
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
    , m_defaults(new QPushButton(parent))
{
    const int width = sliderWidth(parent);
    const int valueWidth = parent->fontMetrics().horizontalAdvance(celsiusText(WidestValue));
    for (QSlider *s : {m_coolest, m_hottest}) {
        // Cool to hot from left to right, like the strip, whatever the language's direction.
        s->setLayoutDirection(Qt::LeftToRight);
        s->setFixedWidth(width);
        s->setSingleStep(1);
        s->setPageStep(SliderPageStep);
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

    auto *note = new QLabel(i18nc("@info", "Shared by all fans in this mode."), parent);
    note->setFont(smallFont(parent->font()));
    KGuiItem::assign(m_defaults, KStandardGuiItem::defaults());
    m_defaults->setToolTip(i18nc("@info:tooltip %1 and %2 are temperatures", "Sets the range back to %1–%2 °C.", def.defaultRange.coolest, def.defaultRange.hottest));
    m_noteField = new QWidget(parent);
    auto *noteRow = new QHBoxLayout(m_noteField);
    noteRow->setContentsMargins(0, 0, 0, 0);
    noteRow->addWidget(note);
    noteRow->addStretch();
    noteRow->addWidget(m_defaults);

    // More space above than a split pair's section headings: these rows are
    // one setting, apart from the LED's own.
    m_heading->setContentsMargins(0, m_heading->fontMetrics().height(), 0, 0);
    form->addRow(m_heading);
    form->addRow(i18nc("@label colours for each CPU temperature", "Colours:"), m_stripField);
    form->addRow(i18nc("@label:slider temperature where the colours start", "Coolest:"), m_coolestField);
    form->addRow(i18nc("@label:slider temperature where the colours end", "Hottest:"), m_hottestField);
    form->addRow(QString(), m_noteField);

    connect(m_coolest, &QSlider::valueChanged, this, [this] {
        sliderMoved(m_coolest);
    });
    connect(m_hottest, &QSlider::valueChanged, this, [this] {
        sliderMoved(m_hottest);
    });
    connect(m_defaults, &QPushButton::clicked, this, [this] {
        Q_EMIT edited(m_def.defaultRange);
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
    m_defaults->setEnabled(scale.range() != usableRange(m_def, m_def.defaultRange, scale.top()));
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

void TemperatureRangeForm::setVisible(bool visible)
{
    for (QWidget *w : {static_cast<QWidget *>(m_heading), m_stripField, m_coolestField, m_hottestField, m_noteField}) {
        m_form->setRowVisible(w, visible);
    }
}

void TemperatureRangeForm::setEnabled(bool enabled)
{
    for (QWidget *w : {m_stripField, m_coolestField, m_hottestField, m_noteField}) {
        w->setEnabled(enabled);
    }
}

} // namespace Glowzone::Gui
