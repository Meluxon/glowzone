// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "model/helperprotocol.h"

#include <QObject>
#include <QPointer>

class QWindow;

namespace Glowzone
{

// What a call to the root helper produced.
struct HelperResult {
    QString error;              // empty on success; translated, ready to show
    QList<EcLightState> lights; // after Status and Set
    int layout = -1;            // after Layout
    bool ok() const { return error.isEmpty(); }
};

// One call to the root helper. start() it and wait for finished(); the call
// deletes itself afterwards.
class HelperCall : public QObject
{
    Q_OBJECT

public:
    enum Kind { Status, Set, Layout };
    using QObject::QObject;
    virtual void start() = 0;

Q_SIGNALS:
    void finished(const Glowzone::HelperResult &result);
};

// Makes helper calls: the real helper through KAuth, or a fake in tests.
class HelperCalls
{
public:
    HelperCalls() = default;
    virtual ~HelperCalls() = default;
    Q_DISABLE_COPY_MOVE(HelperCalls)

    virtual HelperCall *create(HelperCall::Kind kind, const QList<EcRequest> &requests, QObject *parent) = 0;
};

class KAuthHelperCalls : public HelperCalls
{
public:
    // The window a polkit dialog would belong to (none for the CLI).
    void setWindow(QWindow *window);
    HelperCall *create(HelperCall::Kind kind, const QList<EcRequest> &requests, QObject *parent) override;

private:
    QPointer<QWindow> m_window; // the window may close while Glowzone runs on
};

// Runs one call to the end, for the CLI.
HelperResult runToEnd(HelperCalls &calls, HelperCall::Kind kind, const QList<EcRequest> &requests = {});

} // namespace Glowzone
