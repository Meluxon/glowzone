// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "instance.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>

#include <algorithm>

namespace Glowzone::Instance
{

QString serviceName()
{
    QStringList parts = QCoreApplication::organizationDomain().split(u'.', Qt::SkipEmptyParts);
    std::ranges::reverse(parts);
    parts << QCoreApplication::applicationName();
    return parts.join(u'.');
}

bool isRunning()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface() && bus.interface()->isServiceRegistered(serviceName()).value();
}

uint processId()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()) {
        return 0;
    }
    const QDBusReply<uint> pid = bus.interface()->servicePid(serviceName());
    return pid.isValid() ? pid.value() : 0;
}

} // namespace Glowzone::Instance
