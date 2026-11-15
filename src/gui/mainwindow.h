// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/ite8297.h"
#include "core/profiles.h"

#include <QElapsedTimer>
#include <QMainWindow>

class KMessageWidget;
class QLabel;
class QVBoxLayout;
class QStackedWidget;

namespace Glowzone::Gui
{

class App;
class FanPage;
class LightPage;
class Lighting;
class Sidebar;

// Sidebar with the overview on the left, settings of the selected fan or light
// on the right – the System Settings / Discover layout.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // The lighting state and the app outlive the window (it may be closed to the tray).
    MainWindow(Lighting *lighting, App *app);
    ~MainWindow() override;

    // Call after show(). The lighting gets the window as parent for a polkit
    // dialog once it is on screen: on Wayland it can only be referenced then.
    void start();

Q_SIGNALS:
    void closed();
    void quitRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildSidebarPanel();
    QWidget *buildPagePanel();
    void showFan(int fanIndex, int header);
    void showLight(int lightIndex);
    void refresh();

    // profiles
    void reloadProfiles();
    std::optional<Profile> currentProfile() const;
    void applyProfile(const QString &name);
    void saveCurrentProfile();
    void saveNewProfile();
    void renameProfile(const QString &name);
    void deleteProfile(const QString &name);
    void updateBanner();
    void updateBackgroundMessage();
    void refreshTemperature();
    void reloadCurrentProfile();

    void showProblem(const QString &text);
    void showError(const QString &text);
    static void showMessage(KMessageWidget *message);
    static void hideMessage(KMessageWidget *message);

    const ModelProfile &m_model;
    Lighting *m_lighting;
    App *m_app;
    Sidebar *m_sidebar = nullptr;
    QLabel *m_pageTitle = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<FanPage *> m_fanPages;
    QList<LightPage *> m_lightPages;
    // Messages at the bottom of the page (KMessageWidget footer position):
    // they only take empty space below the top-aligned form, so no control moves.
    KMessageWidget *m_problemMessage = nullptr; // lasting: missing hardware, BIOS detection
    KMessageWidget *m_errorMessage = nullptr;   // a change failed
    KMessageWidget *m_changedMessage = nullptr; // "You changed “X”. [Save] [Save as New…]"
    KMessageWidget *m_backgroundMessage = nullptr; // LEDs follow the temperature, but closing quits
    bool m_backgroundMessageDismissed = false;
    QStringList m_problems;
    Ite8297::Rgb m_temperatureColour; // as the sidebar shows it
    QList<Profile> m_profiles;
    QString m_currentProfile;
    QElapsedTimer m_lastReload; // the CLI may change things while the window is open
    bool m_closing = false; // deleted once closed (WA_DeleteOnClose), so never reset
};

} // namespace Glowzone::Gui
