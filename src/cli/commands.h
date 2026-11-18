// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QStringList>

class QCommandLineParser;

namespace Glowzone::Cli
{

enum ExitCode {
    ExitOk = 0,
    ExitFailed = 1,        // something did not work
    ExitUnsupported = 2,   // not a supported PC model
    ExitUsage = 64,        // bad command line (EX_USAGE)
};

// Call after the help option is added (KAboutData::setupCommandLine()).
void addOptions(QCommandLineParser &parser, const QStringList &arguments);
int run(QCommandLineParser &parser);

} // namespace Glowzone::Cli
