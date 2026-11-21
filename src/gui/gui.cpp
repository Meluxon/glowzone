// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gui.h"
#include "app.h"

#include "core/instance.h"
#include "core/messages.h"
#include "core/settings.h"

#include "config-glowzone.h"

#include <KDBusService>
#include <KLocalizedString>
#include <QApplication>
#include <QMessageBox>
#include <QSessionManager>

using namespace Qt::StringLiterals;

namespace Glowzone::Gui
{

int run(bool background)
{
    // Installed icon first (themes may override it); the built-in copy covers
    // running from the build directory.
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(GLOWZONE_APP_ID), QIcon(u":/icons/glowzone.svg"_s)));

    // Started at login, but running in the background was switched off since.
    if (background && !Settings::keepRunning()) {
        return 0;
    }

    // One Glowzone per user: starting it again shows the running one's window
    // (unless it's the login start, which has nothing to show). This process
    // then exits here.
    KDBusService service(KDBusService::Unique | KDBusService::NoExitOnFailure);
    // Not registered: without a session bus there is nobody to ask, so go on.
    // But if the running Glowzone just didn't answer (KDBusService waits
    // minutes for it), a second one would drive the same fans next to it.
    if (const uint pid = service.isRegistered() ? 0 : Instance::processId()) {
        if (!background) {
            QMessageBox::critical(nullptr,
                                  i18nc("@title:window", "Glowzone Is Not Responding"),
                                  i18n("Glowzone is already running (process %1), but it doesn’t respond. End that process – for example in "
                                       "System Monitor, or with “kill %1” – and start Glowzone again.",
                                       pid));
        }
        return 1;
    }

    // Model lock, as in the CLI: no profile, no window.
    const DmiInfo dmi = DmiInfo::readSystem();
    const ModelProfile *model = matchModel(dmi);
    if (!model) {
        if (!background) {
            QMessageBox::critical(nullptr, i18nc("@title:window", "Unsupported Computer"), unsupportedModelMessage(dmi));
        }
        return 2;
    }

    // An X11 session restore would start Glowzone a second time, as a window,
    // next to the login start.
    QObject::connect(qApp, &QGuiApplication::saveStateRequest, qApp, [](QSessionManager &manager) {
        if (Settings::startsAtLogin()) {
            manager.setRestartHint(QSessionManager::RestartNever);
        }
    });

    App app(*model);
    QObject::connect(&service, &KDBusService::activateRequested, &app, [&app](const QStringList &arguments) {
        if (!arguments.contains("--background"_L1)) {
            app.showWindow();
        }
    });
    app.start(background);
    return QApplication::exec();
}

} // namespace Glowzone::Gui
