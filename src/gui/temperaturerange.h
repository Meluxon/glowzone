// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/modelprofile.h"

#include <QObject>
#include <QWidget>

#include <optional>

class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;

namespace Glowzone
{
class TemperatureScale;
}

namespace Glowzone::Gui
{

// The colour for every temperature as a strip, with a marker at the
// temperature now, a bracket under the range and a few °C labels. It is as
// long as the grooves of the sliders it is shown with, and its temperature
// axis lines up with their handles.
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
    struct Axis {
        double left;  // groove ends
        double right;
        double first; // handle centres at minimum and maximum
        double last;
    };
    Axis axis() const;
    double xFor(double celsius) const;
    int stripTop() const;
    int stripHeight() const;
    int bracketHeight() const;

    const QSlider *m_alignWith;
    const TemperatureScale *m_scale = nullptr;
    int m_minimum = 0;
    int m_maximum = 100;
    std::optional<double> m_temperature;
};

// The CPU-temperature range as form rows under a heading of their own: the
// strip, "Coolest" and "Hottest" sliders over the same axis (darkAt to the
// sensor's limit), a note that the range is shared by every fan in the mode,
// and a button back to the model's default range.
class TemperatureRangeForm : public QObject
{
    Q_OBJECT

public:
    TemperatureRangeForm(const CpuTemperatureDef &def, QFormLayout *form, QWidget *parent);

    void setScale(const TemperatureScale &scale);
    void setTemperature(std::optional<double> celsius);
    void setVisible(bool visible);
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
    QWidget *m_noteField;
    QPushButton *m_defaults;
};

} // namespace Glowzone::Gui
