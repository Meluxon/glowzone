// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>

// A fake hidapi with one ITE 8297: linked into a test, it takes the place of
// the real library. Every feature report sent is recorded.
namespace FakeHid
{
extern QList<QByteArray> reports;
extern int failures; // the next sends fail
extern bool missing; // no controller: opening fails

void reset();
// "13:00ff50" per settings report, "commit" per commit – easy to compare.
// Takes them: the next call starts afresh.
QStringList sent();
}
