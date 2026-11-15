// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/ite8297.h"

#include <QToolButton>

namespace Glowzone::Gui
{

// A swatch button. Clicking opens a colour dialog that previews live; Cancel
// restores the previous colour.
class ColourButton : public QToolButton
{
    Q_OBJECT

public:
    explicit ColourButton(QWidget *parent = nullptr);

    Ite8297::Rgb colour() const { return m_colour; }
    void setColour(const Ite8297::Rgb &colour); // no signal

Q_SIGNALS:
    void colourEdited(const Ite8297::Rgb &colour);

private:
    void pick();
    void updateIcon();

    Ite8297::Rgb m_colour{0xff, 0xff, 0xff};
};

} // namespace Glowzone::Gui
