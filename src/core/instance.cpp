// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "instance.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>

#include <algorithm>

namespace Glowzone::Instance
{

QString serviceName()
{
    QStringList parts = QCoreApplication::organizationDomain().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    std::reverse(parts.begin(), parts.end());
    parts << QCoreApplication::applicationName();
    return parts.join(QLatin1Char('.'));
}

bool isRunning()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface() && bus.interface()->isServiceRegistered(serviceName()).value();
}

} // namespace Glowzone::Instance
