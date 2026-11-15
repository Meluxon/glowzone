// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/ite8297.h"

#include <QWidget>

class QComboBox;
class QFormLayout;
class QSlider;

namespace Glowzone::Gui
{

class ColourButton;

// A short slider with words under its ends, like System Settings'
// "Slow … Instant". Fixed size: it doesn't grow with the window.
class LevelSlider : public QWidget
{
    Q_OBJECT

public:
    LevelSlider(int min, int max, const QString &minText, const QString &maxText, QWidget *parent);
    QSlider *slider() const { return m_slider; }

private:
    QSlider *m_slider;
};

// The settings of one fan LED as form rows. Rows the chosen mode doesn't use
// are hidden; until a mode is known only the mode row is shown.
class LedForm : public QObject
{
    Q_OBJECT

public:
    // Adds its rows to the given form layout; offers what the model offers.
    LedForm(const ModelProfile &model, QFormLayout *form, QWidget *parent);

    void setValues(const Ite8297::HeaderSetting &values, bool known);
    Ite8297::HeaderSetting values() const;
    void setEnabled(bool enabled);
    void setVisible(bool visible);
    QWidget *focusWidget() const;

Q_SIGNALS:
    void edited(const Ite8297::HeaderSetting &values);

private:
    void updateRows();
    void emitEdited();

    QFormLayout *m_form;
    QComboBox *m_mode;
    ColourButton *m_colour;
    LevelSlider *m_speed;
    LevelSlider *m_brightness;
    bool m_visible = true;
};

// A centred form like a System Settings page: labels right-aligned, fields at
// their natural size, everything at the top.
QWidget *centredForm(QFormLayout **form, QWidget *parent);

} // namespace Glowzone::Gui
