// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "app.h"
#include "mainwindow.h"

#include "core/settings.h"

#include "config-glowzone.h"

#include <KLocalizedString>
#include <KStatusNotifierItem>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>

#include <utility>

using namespace Qt::StringLiterals;

namespace Glowzone::Gui
{

App::App(const ModelProfile &model, QObject *parent)
    : QObject(parent)
    , m_lighting(model)
    , m_keepRunning(Settings::keepRunning())
{
    // Closing the window decides itself whether Glowzone goes on.
    QApplication::setQuitOnLastWindowClosed(false);
    connect(&m_lighting, &Lighting::temperatureChanged, this, &App::updateTrayToolTip);
    connect(&m_lighting, &Lighting::changed, this, &App::updateTrayToolTip);
    connect(&m_lighting, &Lighting::problemsChanged, this, &App::updateTrayToolTip);
}

App::~App() = default;

void App::start(bool background)
{
    m_lighting.start(nullptr);
    updateTray();
    if (!background) {
        showWindow();
    }
}

void App::showWindow()
{
    if (std::exchange(m_quitting, false)) {
        disconnect(m_quitWhenIdle); // started again while finishing: stay
    }
    if (!m_window) {
        m_window = new MainWindow(&m_lighting, this);
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
    m_lighting.windowClosed();
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
    updateTray(); // removes it
    // Don't lose a change made in the last moment: idle() follows once
    // everything is sent (at once if nothing is pending).
    m_quitWhenIdle = connect(&m_lighting, &Lighting::idle, qApp, &QApplication::quit, Qt::QueuedConnection);
    m_lighting.finish();
}

void App::setKeepRunning(bool on)
{
    if (on == m_keepRunning) {
        return;
    }
    m_keepRunning = on;
    Settings::setKeepRunning(on);
    if (!on) {
        changeStartAtLogin(false); // starting at login would only start and quit again
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
    changeStartAtLogin(on);
    if (on && Settings::startsAtLogin() && !m_keepRunning) {
        setKeepRunning(true); // tells about both
        return;
    }
    Q_EMIT backgroundChanged();
}

void App::changeStartAtLogin(bool on)
{
    QString error;
    if (Settings::startsAtLogin() != on && !Settings::setStartAtLogin(on, &error)) {
        // Opened, not exec()'d: the window may be closed from the tray meanwhile.
        auto *box = new QMessageBox(QMessageBox::Warning, i18nc("@title:window", "Start at Login"), error, QMessageBox::Ok, m_window);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
}

// The tray icon is there only while Glowzone keeps running in the background.
void App::updateTray()
{
    if (!m_keepRunning || m_quitting) {
        if (m_tray) {
            m_tray->deleteLater(); // and its menu – this may run in one of its actions (Quit)
        }
        m_tray = nullptr;
        m_trayShow = nullptr;
        m_trayToolTip.clear();
        return;
    }
    if (!m_tray) {
        m_tray = new KStatusNotifierItem(u"glowzone"_s, this);
        m_tray->setCategory(KStatusNotifierItem::Hardware);
        m_tray->setStatus(KStatusNotifierItem::Active);
        m_tray->setIconByName(QStringLiteral(GLOWZONE_APP_ID));
        m_tray->setTitle(i18n("Glowzone"));
        m_tray->setStandardActionsEnabled(false); // our own Quit, which finishes pending changes
        auto *menu = new QMenu;
        m_trayShow = menu->addAction(QIcon::fromTheme(u"window"_s), QString(), this, &App::toggleWindow);
        connect(menu, &QMenu::aboutToShow, this, [this] {
            m_trayShow->setText(m_window && m_window->isVisible() ? i18nc("@action:inmenu", "Hide Glowzone")
                                                                   : i18nc("@action:inmenu", "Show Glowzone"));
        });
        menu->addSeparator();
        menu->addAction(QIcon::fromTheme(u"application-exit"_s), i18nc("@action:inmenu", "Quit"), this, &App::quit);
        m_tray->setContextMenu(menu); // takes ownership
        connect(m_tray, &KStatusNotifierItem::activateRequested, this, &App::toggleWindow);
    }
    updateTrayToolTip();
}

// Called on every temperature reading; only a new text goes out over D-Bus.
void App::updateTrayToolTip()
{
    if (!m_tray) {
        return;
    }
    QString text = i18nc("@info:tooltip", "Running in the background");
    if (!m_lighting.fansAvailable()) {
        text = m_lighting.fanProblem();
    } else if (m_lighting.followsCpuTemperature()) {
        const auto celsius = m_lighting.cpuTemperature();
        if (!m_lighting.cpuTemperatureError().isEmpty()) {
            text = m_lighting.cpuTemperatureError();
        } else if (celsius) {
            text = i18nc("@info:tooltip", "Fans follow the CPU temperature: %1 °C", QString::number(*celsius, 'f', 0));
        }
    }
    if (text != m_trayToolTip) {
        m_trayToolTip = text;
        m_tray->setToolTip(QStringLiteral(GLOWZONE_APP_ID), i18n("Glowzone"), text);
    }
}

} // namespace Glowzone::Gui
