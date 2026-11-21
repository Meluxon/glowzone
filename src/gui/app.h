// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "lighting.h"

#include "model/modelprofile.h"

#include <QObject>
#include <QPointer>

class KStatusNotifierItem;
class QAction;

namespace Glowzone::Gui
{

class MainWindow;

// The running Glowzone: the lighting state, the window when it's open, and –
// if the user wants it – the tray icon that keeps Glowzone running after the
// window is closed, so LEDs following the CPU temperature keep changing.
// The window is created when shown and deleted when closed: in the tray only
// the lighting state stays in memory.
class App : public QObject
{
    Q_OBJECT

public:
    explicit App(const ModelProfile &model, QObject *parent = nullptr);
    ~App() override;

    // Opens the fan controller; shows the window unless started in the background.
    void start(bool background);

    void showWindow();
    void toggleWindow();
    // Sends anything still pending, then quits.
    void quit();

    bool keepsRunning() const { return m_keepRunning; }
    void setKeepRunning(bool on);
    bool startsAtLogin() const;
    void setStartAtLogin(bool on);

Q_SIGNALS:
    void backgroundChanged();

private:
    void changeStartAtLogin(bool on); // without telling: the caller does
    void windowClosed();
    void updateTray();
    void updateTrayToolTip();

    Lighting m_lighting;
    QPointer<MainWindow> m_window;
    KStatusNotifierItem *m_tray = nullptr;
    QAction *m_trayShow = nullptr;
    QString m_trayToolTip; // as last sent
    bool m_keepRunning = false;
    bool m_quitting = false;
    QMetaObject::Connection m_quitWhenIdle;
};

} // namespace Glowzone::Gui
