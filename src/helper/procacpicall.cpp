// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "procacpicall.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

namespace Glowzone
{

static QString procPath()
{
    return QStringLiteral("/proc/acpi/call");
}

bool ProcAcpiCall::ensureReady(QString *detail)
{
    if (QFileInfo::exists(procPath())) {
        return true;
    }

    // Load the module on demand (no boot-time setup). Fixed path, fixed argument.
    QString modprobe;
    for (const char *candidate : {"/usr/bin/modprobe", "/usr/sbin/modprobe", "/sbin/modprobe"}) {
        if (QFileInfo(QLatin1String(candidate)).isExecutable()) {
            modprobe = QLatin1String(candidate);
            break;
        }
    }
    if (modprobe.isEmpty()) {
        *detail = QStringLiteral("modprobe not found");
        return false;
    }

    QProcess p;
    p.setProcessEnvironment(QProcessEnvironment()); // empty environment
    p.start(modprobe, {QStringLiteral("acpi_call")});
    if (!p.waitForFinished(10000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *detail = QStringLiteral("modprobe acpi_call failed: ") + QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return false;
    }

    for (int i = 0; i < 20 && !QFileInfo::exists(procPath()); ++i) {
        QThread::msleep(50);
    }
    if (!QFileInfo::exists(procPath())) {
        *detail = QStringLiteral("acpi_call loaded but %1 did not appear").arg(procPath());
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
