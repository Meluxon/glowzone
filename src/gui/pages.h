// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/modelprofile.h"

#include <QList>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QLabel;
class QCheckBox;

namespace Glowzone::Gui
{

class LedForm;
class LevelSlider;
class Lighting;

// Settings of one fan. A double fan gets a "Set separately" checkbox; checked,
// each LED gets its own section named by its position.
class FanPage : public QWidget
{
    Q_OBJECT

public:
    FanPage(const FanDef &fan, Lighting *lighting, QWidget *parent);
    const FanDef &fan() const { return m_fan; }
    void refresh();
    void focusLed(quint8 header);

private:
    const FanDef &m_fan;
    Lighting *m_lighting;
    QCheckBox *m_separately = nullptr;
    QList<QLabel *> m_headings;
    QList<LedForm *> m_forms;
};

// Settings of one case light.
class LightPage : public QWidget
{
    Q_OBJECT

public:
    LightPage(const EcLightDef &light, Lighting *lighting, QWidget *parent);
    void refresh();

private:
    void edited();

    const EcLightDef &m_light;
    Lighting *m_lighting;
    QFormLayout *m_form = nullptr;
    QComboBox *m_mode;
    LevelSlider *m_brightness;
    QLabel *m_waiting;
};

// "front" -> "Front"
QString capitalised(const QString &s);

} // namespace Glowzone::Gui
