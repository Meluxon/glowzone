// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/modelprofile.h"

#include <QObject>
#include <QWidget>

#include <optional>

class QFormLayout;
class QLabel;
class QSlider;

namespace Glowzone
{
class TemperatureScale;
}

namespace Glowzone::Gui
{

// The colour for every temperature as a strip, with a marker at the
// temperature now and a few °C labels under it. Its temperature axis lines up
// with the sliders it is shown with: same width, same handle inset.
class TemperatureStrip : public QWidget
{
    Q_OBJECT

public:
    TemperatureStrip(const QSlider *alignWith, QWidget *parent);

    void setScale(const TemperatureScale *scale, int minimum, int maximum);
    void setTemperature(std::optional<double> celsius);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double xFor(double celsius) const;
    int stripTop() const;
    int stripHeight() const;

    const QSlider *m_alignWith;
    const TemperatureScale *m_scale = nullptr;
    int m_minimum = 0;
    int m_maximum = 100;
    std::optional<double> m_temperature;
};

// The CPU-temperature range as form rows: the strip, "Coolest" and "Hottest"
// sliders over the same axis (darkAt to the sensor's limit), and a note that
// the range is shared by every fan in the mode.
class TemperatureRangeForm : public QObject
{
    Q_OBJECT

public:
    TemperatureRangeForm(const CpuTemperatureDef &def, QFormLayout *form, QWidget *parent);

    void setScale(const TemperatureScale &scale);
    void setTemperature(std::optional<double> celsius);
    void setVisible(bool visible, bool withHeading);
    void setEnabled(bool enabled);

Q_SIGNALS:
    void edited(Glowzone::TemperatureRange range);

private:
    void sliderMoved(QSlider *moved);
    void updateLabels();

    const CpuTemperatureDef &m_def;
    QFormLayout *m_form;
    QLabel *m_heading;
    TemperatureStrip *m_strip;
    QWidget *m_stripField;
    QSlider *m_coolest;
    QSlider *m_hottest;
    QWidget *m_coolestField;
    QWidget *m_hottestField;
    QLabel *m_coolestValue;
    QLabel *m_hottestValue;
    QLabel *m_note;
};

} // namespace Glowzone::Gui
