// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "app.h"
#include "lighting.h"
#include "mainwindow.h"

#include "core/settings.h"

#include "config-glowzone.h"

#include <KLocalizedString>
#include <KStatusNotifierItem>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>

namespace Glowzone::Gui
{

App::App(const ModelProfile &model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_lighting(new Lighting(model, this))
    , m_keepRunning(Settings::keepRunning())
{
    // Closing the window decides itself whether Glowzone goes on.
    QApplication::setQuitOnLastWindowClosed(false);
    connect(m_lighting, &Lighting::temperatureChanged, this, &App::updateTrayToolTip);
    connect(m_lighting, &Lighting::changed, this, &App::updateTrayToolTip);
}

App::~App() = default;

void App::start(bool background)
{
    m_lighting->start(nullptr);
    updateTray();
    if (!background) {
        showWindow();
    }
}

void App::showWindow()
{
    if (!m_window) {
        m_window = new MainWindow(m_lighting, this);
        m_window->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_window, &MainWindow::closed, this, &App::windowClosed);
        connect(m_window, &MainWindow::quitRequested, this, &App::quit);
        m_window->show();
        m_window->start();
    } else {
        m_window->show();
        m_window->setWindowState((m_window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        m_window->raise();
        m_window->activateWindow();
    }
    updateTray();
}

void App::toggleWindow()
{
    if (m_window && m_window->isVisible()) {
        m_window->close();
    } else {
        showWindow();
    }
}

void App::windowClosed()
{
    updateTray();
    if (!m_quitting && !m_keepRunning) {
        quit();
    }
}

void App::quit()
{
    if (m_quitting) {
        return;
    }
    m_quitting = true;
    if (m_window) {
        m_window->close();
    }
    delete m_tray;
    m_tray = nullptr;
    m_trayShow = nullptr;
    // Don't lose a change made in the last moment.
    if (m_lighting->isBusy()) {
        connect(m_lighting, &Lighting::idle, qApp, &QApplication::quit, Qt::QueuedConnection);
        m_lighting->finish();
        return;
    }
    m_lighting->finish();
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
}

void App::setKeepRunning(bool on)
{
    if (on == m_keepRunning) {
        return;
    }
    m_keepRunning = on;
    Settings::setKeepRunning(on);
    if (!on && Settings::startsAtLogin()) {
        setStartAtLogin(false); // starting at login would only start and quit again
    }
    updateTray();
    Q_EMIT backgroundChanged();
}

bool App::startsAtLogin() const
{
    return Settings::startsAtLogin();
}

void App::setStartAtLogin(bool on)
{
    QString error;
    if (!Settings::setStartAtLogin(on, &error)) {
        QMessageBox::warning(m_window, i18nc("@title:window", "Start at Login"), error);
    }
    if (on && Settings::startsAtLogin() && !m_keepRunning) {
        setKeepRunning(true);
    }
    Q_EMIT backgroundChanged();
}

// The tray icon is there only while Glowzone keeps running in the background.
void App::updateTray()
{
    if (!m_keepRunning || m_quitting) {
        delete m_tray; // and its menu
        m_tray = nullptr;
        m_trayShow = nullptr;
        return;
    }
    if (!m_tray) {
        m_tray = new KStatusNotifierItem(QStringLiteral("glowzone"), this);
        m_tray->setCategory(KStatusNotifierItem::Hardware);
        m_tray->setStatus(KStatusNotifierItem::Active);
        m_tray->setIconByName(QStringLiteral(GLOWZONE_APP_ID));
        m_tray->setTitle(i18n("Glowzone"));
        m_tray->setStandardActionsEnabled(false); // our own Quit, which finishes pending changes
        auto *menu = new QMenu;
        m_trayShow = menu->addAction(QIcon::fromTheme(QStringLiteral("window")), QString(), this, &App::toggleWindow);
        connect(menu, &QMenu::aboutToShow, this, [this] {
            m_trayShow->setText(m_window && m_window->isVisible() ? i18nc("@action:inmenu", "Hide Glowzone")
                                                                   : i18nc("@action:inmenu", "Show Glowzone"));
        });
        menu->addSeparator();
        menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action:inmenu", "Quit"), this, &App::quit);
        m_tray->setContextMenu(menu); // takes ownership
        connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this] {
            toggleWindow();
        });
    }
    updateTrayToolTip();
}

void App::updateTrayToolTip()
{
    if (!m_tray) {
        return;
    }
    QString text = i18nc("@info:tooltip", "Running in the background");
    if (m_lighting->followsCpuTemperature()) {
        const auto celsius = m_lighting->cpuTemperature();
        if (m_lighting->cpuTemperatureFailed()) {
            text = m_lighting->cpuTemperatureError();
        } else if (celsius) {
            text = i18nc("@info:tooltip", "Fans follow the CPU temperature: %1 °C", QString::number(*celsius, 'f', 0));
        }
    }
    m_tray->setToolTip(QStringLiteral(GLOWZONE_APP_ID), i18n("Glowzone"), text);
}

} // namespace Glowzone::Gui
