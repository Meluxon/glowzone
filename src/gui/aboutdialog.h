// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>

namespace Glowzone::Gui
{

class Lighting;

// Version, the detected computer and what's available on it, licence and the
// "vibecoded" note. "Copy to Clipboard" gives everything a bug report needs.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    AboutDialog(const Lighting *lighting, QWidget *parent);

private:
    QString m_report;
};

} // namespace Glowzone::Gui
