// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "colourbutton.h"

#include <KLocalizedString>
#include <QColorDialog>
#include <QPainter>

namespace Glowzone::Gui
{

static constexpr QSize SwatchSize(32, 16); // device-independent pixels

static QColor toQColor(Ite8297::Rgb c)
{
    return QColor(c.r, c.g, c.b);
}

static Ite8297::Rgb fromQColor(const QColor &c)
{
    return {quint8(c.red()), quint8(c.green()), quint8(c.blue())};
}

ColourButton::ColourButton(QWidget *parent)
    : QToolButton(parent)
{
    setIconSize(SwatchSize);
    setToolTip(i18nc("@info:tooltip", "Choose the colour"));
    connect(this, &QToolButton::clicked, this, &ColourButton::pick);
    updateIcon();
}

void ColourButton::setColour(Ite8297::Rgb colour)
{
    m_colour = colour;
    updateIcon();
}

void ColourButton::updateIcon()
{
    QPixmap pm(iconSize() * devicePixelRatioF());
    pm.setDevicePixelRatio(devicePixelRatioF());
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(toQColor(m_colour));
    p.drawRoundedRect(QRectF(0.5, 0.5, iconSize().width() - 1, iconSize().height() - 1), 3, 3);
    p.end();
    setIcon(QIcon(pm));
    setAccessibleName(i18nc("@info accessible name, colour value", "Colour %1", Ite8297::rgbToString(m_colour)));
}

void ColourButton::pick()
{
    const Ite8297::Rgb before = m_colour;
    QColorDialog dialog(toQColor(before), window());
    dialog.setWindowTitle(i18nc("@title:window", "Fan Colour"));
    connect(&dialog, &QColorDialog::currentColorChanged, this, [this](const QColor &c) {
        if (c.isValid()) {
            setColour(fromQColor(c));
            Q_EMIT colourEdited(m_colour);
        }
    });
    if (dialog.exec() == QDialog::Accepted) {
        setColour(fromQColor(dialog.selectedColor()));
    } else {
        setColour(before);
    }
    Q_EMIT colourEdited(m_colour);
}

} // namespace Glowzone::Gui
