// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// One binary: "glowzone" alone opens the window, "glowzone <command>" is the CLI.

#include "commands.h"
#include "config-glowzone.h"
#include "gui/gui.h"

#include <KAboutData>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <memory>

// The window when there are no arguments – or only Qt's own (e.g.
// "-style fusion", "--reverse"), which QApplication takes. Anything else is
// the CLI.
static bool wantsWindow(int argc, char **argv)
{
    static const QByteArrayList withValue{"-display", "-geometry", "-name", "-platform", "-platformpluginpath",
                                          "-platformtheme", "-plugin", "-qmljsdebugger", "-qwindowgeometry", "-qwindowicon",
                                          "-qwindowtitle", "-session", "-style", "-stylesheet", "-title"};
    static const QByteArrayList flags{"-reverse", "-widgetcount", "-nograb", "-dograb", "-sync"};
    for (int i = 1; i < argc; ++i) {
        QByteArray arg(argv[i]);
        if (arg.startsWith("--")) {
            arg.remove(0, 1); // Qt accepts both -style and --style
        }
        const qsizetype eq = arg.indexOf('=');
        const QByteArray name = eq >= 0 ? arg.left(eq) : arg;
        if (flags.contains(name)) {
            continue;
        }
        if (withValue.contains(name)) {
            i += eq >= 0 ? 0 : 1; // "-style fusion" or "-style=fusion"
            continue;
        }
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    const bool gui = wantsWindow(argc, argv);
    std::unique_ptr<QCoreApplication> app;
    if (gui) {
        app = std::make_unique<QApplication>(argc, argv);
    } else {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("glowzone"));

    KAboutData about(QStringLiteral("glowzone"),
                     i18n("Glowzone"),
                     QStringLiteral(GLOWZONE_VERSION_STRING),
                     i18n("Lighting control for supported Lenovo desktops: fan LEDs and case lights."),
                     KAboutLicense::GPL_V2,
                     i18n("© 2026 Meluxon"));
    about.setLicense(KAboutLicense::GPL_V2, KAboutLicense::OrLaterVersions);
    about.setOrganizationDomain(QByteArrayLiteral("meluxon.github.io"));
    about.setHomepage(QStringLiteral("https://github.com/Meluxon/glowzone"));
    about.setBugAddress(QByteArrayLiteral("https://github.com/Meluxon/glowzone/issues"));
    about.setDesktopFileName(QStringLiteral(GLOWZONE_APP_ID));
    KAboutData::setApplicationData(about);

    if (gui) {
        return Glowzone::Gui::run();
    }

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    Glowzone::Cli::addOptions(parser);
    parser.process(*app);
    about.processCommandLine(&parser);
    return Glowzone::Cli::run(parser);
}
