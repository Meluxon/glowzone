// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gui.h"
#include "mainwindow.h"

#include "core/messages.h"

#include "config-glowzone.h"

#include <KLocalizedString>
#include <QApplication>
#include <QMessageBox>

namespace Glowzone::Gui
{

int run()
{
    // Installed icon first (themes may override it); the built-in copy covers
    // running from the build directory.
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(GLOWZONE_APP_ID), QIcon(QStringLiteral(":/icons/glowzone.svg"))));

    // Model lock, as in the CLI: no profile, no window.
    const DmiInfo dmi = DmiInfo::readSystem();
    const ModelProfile *model = matchModel(dmi);
    if (!model) {
        QMessageBox::critical(nullptr, i18nc("@title:window", "Unsupported Computer"), unsupportedModelMessage(dmi));
        return 2;
    }

    MainWindow window(*model);
    window.show();
    window.start();
    return QApplication::exec();
}

} // namespace Glowzone::Gui
