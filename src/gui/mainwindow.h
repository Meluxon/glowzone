// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/profiles.h"

#include <QMainWindow>

class KMessageWidget;
class QLabel;
class QVBoxLayout;
class QStackedWidget;

namespace Glowzone::Gui
{

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
    explicit MainWindow(const ModelProfile &model, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Call once the window is shown (KAuth wants a real window as parent).
    void start();

protected:
    void closeEvent(QCloseEvent *event) override;

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

    void showProblem(const QString &text);
    void showError(const QString &text);
    static void showMessage(KMessageWidget *message);
    static void hideMessage(KMessageWidget *message);

    const ModelProfile &m_model;
    Lighting *m_lighting;
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
    QStringList m_problems;
    QList<Profile> m_profiles;
    QString m_currentProfile;
};

} // namespace Glowzone::Gui
