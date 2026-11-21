// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "procacpicall.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

using namespace Qt::StringLiterals;

namespace Glowzone
{

static QString procPath()
{
    return u"/proc/acpi/call"_s;
}

bool ProcAcpiCall::ensureReady(QString *detail)
{
    if (QFileInfo::exists(procPath())) {
        return true;
    }

    // Load the module on demand (no boot-time setup). Fixed path, fixed argument.
    QString modprobe;
    for (const QLatin1StringView candidate : {"/usr/bin/modprobe"_L1, "/usr/sbin/modprobe"_L1, "/sbin/modprobe"_L1}) {
        if (QFileInfo(candidate).isExecutable()) {
            modprobe = candidate;
            break;
        }
    }
    if (modprobe.isEmpty()) {
        *detail = u"modprobe not found"_s;
        return false;
    }

    QProcess p;
    p.setProcessEnvironment(QProcessEnvironment()); // empty environment
    p.start(modprobe, {u"acpi_call"_s});
    if (!p.waitForFinished(10000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *detail = u"modprobe acpi_call failed: "_s + QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return false;
    }

    for (int i = 0; i < 20 && !QFileInfo::exists(procPath()); ++i) {
        QThread::msleep(50);
    }
    if (!QFileInfo::exists(procPath())) {
        *detail = u"acpi_call loaded but %1 did not appear"_s.arg(procPath());
        return false;
    }
    return true;
}

std::optional<QByteArray> ProcAcpiCall::call(const QByteArray &command, QString *detail)
{
    // acpi_call: write the call, then read the result back from the same file.
    QFile out(procPath());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        *detail = out.errorString();
        return std::nullopt;
    }
    if (out.write(command) != command.size()) {
        *detail = out.errorString();
        return std::nullopt;
    }
    out.close();

    QFile in(procPath());
    if (!in.open(QIODevice::ReadOnly)) {
        *detail = in.errorString();
        return std::nullopt;
    }
    return cleanReply(in.readAll());
}

QByteArray ProcAcpiCall::cleanReply(QByteArray raw)
{
    const qsizetype nul = raw.indexOf('\0');
    if (nul >= 0) {
        raw.truncate(nul);
    }
    return raw.trimmed();
}

} // namespace Glowzone
