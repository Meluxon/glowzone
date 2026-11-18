// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// One binary: "glowzone" alone opens the window, "glowzone --background" starts
// in the tray, "glowzone <command>" is the CLI.

#include "commands.h"
#include "config-glowzone.h"
#include "gui/gui.h"

#include <KAboutData>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <memory>

using namespace Qt::StringLiterals;

enum class Start { Window, Background, Cli };

// The window when there are no arguments – or only Qt's own (e.g.
// "-style fusion", "--reverse"), which QApplication takes – and the tray with
// "--background" besides those. Anything else is the CLI.
static Start startKind(int argc, char **argv)
{
    bool background = false;
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
        if (name == "-background" && eq < 0) {
            background = true;
            continue;
        }
        if (flags.contains(name)) {
            continue;
        }
        if (withValue.contains(name)) {
            i += eq >= 0 ? 0 : 1; // "-style fusion" or "-style=fusion"
            continue;
        }
        return Start::Cli;
    }
    return background ? Start::Background : Start::Window;
}

int main(int argc, char **argv)
{
    const Start start = startKind(argc, argv);
    const bool gui = start != Start::Cli;
    std::unique_ptr<QCoreApplication> app;
    if (gui) {
        app = std::make_unique<QApplication>(argc, argv);
    } else {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("glowzone"));

    KAboutData about(u"glowzone"_s,
                     i18n("Glowzone"),
                     QStringLiteral(GLOWZONE_VERSION_STRING),
                     i18n("Lighting control for supported Lenovo desktops: fan LEDs and case lights."),
                     KAboutLicense::GPL_V2,
                     i18n("© 2026 Meluxon"));
    about.setLicense(KAboutLicense::GPL_V2, KAboutLicense::OrLaterVersions);
    about.setOrganizationDomain(QByteArrayLiteral("meluxon.github.io"));
    about.setHomepage(u"https://github.com/Meluxon/glowzone"_s);
    about.setBugAddress(QByteArrayLiteral("https://github.com/Meluxon/glowzone/issues"));
    about.setDesktopFileName(QStringLiteral(GLOWZONE_APP_ID));
    KAboutData::setApplicationData(about);

    if (gui) {
        return Glowzone::Gui::run(start == Start::Background);
    }

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    Glowzone::Cli::addOptions(parser, app->arguments());
    parser.process(*app);
    about.processCommandLine(&parser);
    return Glowzone::Cli::run(parser);
}
