// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

class QCommandLineParser;

namespace Glowzone::Cli
{

enum ExitCode {
    ExitOk = 0,
    ExitFailed = 1,        // something did not work
    ExitUnsupported = 2,   // not a supported PC model
    ExitUsage = 64,        // bad command line (EX_USAGE)
};

void addOptions(QCommandLineParser &parser);
int run(QCommandLineParser &parser);

} // namespace Glowzone::Cli
