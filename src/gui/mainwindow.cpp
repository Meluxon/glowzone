// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mainwindow.h"
#include "aboutdialog.h"
#include "app.h"
#include "lighting.h"
#include "pages.h"
#include "sidebar.h"

#include "core/settings.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <QAction>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace Glowzone::Gui
{

static QFrame *line(Qt::Orientation orientation, QWidget *parent)
{
    auto *l = new QFrame(parent);
    l->setFrameShape(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine);
    l->setFrameShadow(QFrame::Plain);
    l->setForegroundRole(QPalette::Mid);
    return l;
}

// Header rows of the sidebar and the page share one height, like System Settings.
static QWidget *headerRow(QWidget *parent, QHBoxLayout **layout)
{
    auto *w = new QWidget(parent);
    *layout = new QHBoxLayout(w);
    w->setFixedHeight(w->fontMetrics().height() * 3);
    return w;
}

MainWindow::MainWindow(Lighting *lighting, App *app)
    : QMainWindow(nullptr)
    , m_model(lighting->model())
    , m_lighting(lighting)
    , m_app(app)
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSidebarPanel());
    layout->addWidget(line(Qt::Vertical, central));
    layout->addWidget(buildPagePanel(), 1);
    setCentralWidget(central);

    auto *quit = new QAction(this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &MainWindow::quitRequested);
    addAction(quit);

    connect(m_lighting, &Lighting::changed, this, &MainWindow::refresh);
    connect(m_lighting, &Lighting::stateFileChanged, this, &MainWindow::reloadCurrentProfile);
    connect(m_lighting, &Lighting::temperatureChanged, this, &MainWindow::refreshTemperature);
    connect(m_app, &App::backgroundChanged, this, &MainWindow::updateBackgroundMessage);
    connect(m_lighting, &Lighting::problem, this, &MainWindow::showProblem);
    connect(m_lighting, &Lighting::error, this, &MainWindow::showError);
    connect(m_lighting, &Lighting::errorCleared, this, [this] {
        hideMessage(m_errorMessage);
    });

    connect(m_sidebar, &Sidebar::fanSelected, this, &MainWindow::showFan);
    connect(m_sidebar, &Sidebar::lightSelected, this, &MainWindow::showLight);
    connect(m_sidebar, &Sidebar::profileActivated, this, &MainWindow::applyProfile);
    connect(m_sidebar, &Sidebar::newProfileRequested, this, &MainWindow::saveNewProfile);
    connect(m_sidebar, &Sidebar::renameRequested, this, &MainWindow::renameProfile);
    connect(m_sidebar, &Sidebar::deleteRequested, this, &MainWindow::deleteProfile);

    m_currentProfile = Settings::currentProfile();
    reloadProfiles();
    m_sidebar->selectFirst();
    const QStringList earlier = m_lighting->problems(); // found before this window was opened
    for (const QString &problem : earlier) {
        showProblem(problem);
    }
    updateBackgroundMessage();

    const QSize stored = Settings::windowSize();
    resize(stored.isValid() ? stored : QSize(fontMetrics().averageCharWidth() * 125, fontMetrics().height() * 36));
}

MainWindow::~MainWindow() = default;

void MainWindow::start()
{
    QWindow *window = windowHandle();
    if (window->isExposed()) {
        m_lighting->start(window);
    } else {
        window->installEventFilter(this); // see eventFilter()
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == windowHandle() && event->type() == QEvent::Expose && windowHandle()->isExposed()) {
        windowHandle()->removeEventFilter(this);
        m_lighting->start(windowHandle());
    }
    return QMainWindow::eventFilter(watched, event);
}

QWidget *MainWindow::buildSidebarPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QHBoxLayout *header = nullptr;
    QWidget *headerWidget = headerRow(panel, &header);
    auto *modelName = new QLabel(QLatin1String(m_model.productVersion), headerWidget);
    modelName->setToolTip(i18nc("@info:tooltip", "Controlling: %1", m_model.displayName()));
    auto *menuButton = new QToolButton(headerWidget);
    menuButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    menuButton->setAutoRaise(true);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setToolTip(i18nc("@info:tooltip", "Menu"));
    menuButton->setAccessibleName(i18nc("@info:tooltip", "Menu"));
    auto *menu = new QMenu(menuButton);
    // Running on in the tray is what keeps LEDs following the CPU temperature
    // after the window is closed.
    auto *keepRunning = menu->addAction(i18nc("@option:check", "Keep Running in Background"));
    keepRunning->setCheckable(true);
    keepRunning->setToolTip(i18nc("@info:tooltip", "Closing the window keeps Glowzone in the system tray, so fans following the CPU temperature keep changing colour."));
    auto *startAtLogin = menu->addAction(i18nc("@option:check", "Start at Login"));
    startAtLogin->setCheckable(true);
    startAtLogin->setToolTip(i18nc("@info:tooltip", "Start Glowzone in the system tray when you log in."));
    menu->setToolTipsVisible(true);
    auto syncBackground = [this, keepRunning, startAtLogin] {
        keepRunning->setChecked(m_app->keepsRunning());
        startAtLogin->setChecked(m_app->startsAtLogin());
    };
    syncBackground();
    connect(m_app, &App::backgroundChanged, menu, syncBackground);
    connect(keepRunning, &QAction::triggered, m_app, &App::setKeepRunning);
    connect(startAtLogin, &QAction::triggered, m_app, &App::setStartAtLogin);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), i18nc("@action:inmenu", "About Glowzone"), this, [this] {
        AboutDialog(m_lighting, this).exec();
    });
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action:inmenu", "Quit"), this, &MainWindow::quitRequested);
    menuButton->setMenu(menu);
    header->addWidget(modelName, 1);
    header->addWidget(menuButton);

    m_sidebar = new Sidebar(m_lighting, panel);
    layout->addWidget(headerWidget);
    layout->addWidget(line(Qt::Horizontal, panel));
    layout->addWidget(m_sidebar, 1);
    panel->setFixedWidth(m_sidebar->width());
    return panel;
}

QWidget *MainWindow::buildPagePanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QHBoxLayout *header = nullptr;
    QWidget *headerWidget = headerRow(panel, &header);
    m_pageTitle = new QLabel(headerWidget);
    QFont f = m_pageTitle->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    m_pageTitle->setFont(f);
    auto *allOff = new QPushButton(QIcon::fromTheme(QStringLiteral("system-shutdown-symbolic")), i18nc("@action:button", "Turn All Off"), headerWidget);
    allOff->setToolTip(i18nc("@info:tooltip", "Switch every fan LED and case light off. Colours and levels are kept."));
    allOff->setFlat(true);
    connect(allOff, &QPushButton::clicked, m_lighting, &Lighting::turnAllOff);
    header->addWidget(m_pageTitle, 1);
    header->addWidget(allOff);

    m_stack = new QStackedWidget(panel);
    for (const FanDef &fan : m_model.fans) {
        auto *page = new FanPage(fan, m_lighting, m_stack);
        m_stack->addWidget(page);
        m_fanPages << page;
    }
    for (const EcLightDef &light : m_model.ecLights) {
        auto *page = new LightPage(light, m_lighting, m_stack);
        m_stack->addWidget(page);
        m_lightPages << page;
    }
    auto *scroll = new QScrollArea(panel);
    scroll->setWidget(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus); // the controls inside take focus, not the area

    // Messages at the bottom, below the (top-aligned) page: appearing there
    // only shortens the empty space under the form, so nothing moves.
    auto makeMessage = [panel](KMessageWidget::MessageType type, bool closable) {
        auto *m = new KMessageWidget(panel);
        m->setPosition(KMessageWidget::Footer);
        m->setMessageType(type);
        m->setWordWrap(true);
        m->setCloseButtonVisible(closable);
        m->hide();
        return m;
    };
    m_problemMessage = makeMessage(KMessageWidget::Warning, true);
    m_errorMessage = makeMessage(KMessageWidget::Error, true);
    m_changedMessage = makeMessage(KMessageWidget::Information, false);
    auto *save = new QAction(QIcon::fromTheme(QStringLiteral("document-save")), i18nc("@action:button", "Save"), m_changedMessage);
    auto *saveNew = new QAction(QIcon::fromTheme(QStringLiteral("document-save-as")), i18nc("@action:button", "Save as New…"), m_changedMessage);
    connect(save, &QAction::triggered, this, &MainWindow::saveCurrentProfile);
    connect(saveNew, &QAction::triggered, this, &MainWindow::saveNewProfile);
    m_changedMessage->addAction(save);
    m_changedMessage->addAction(saveNew);
    m_backgroundMessage = makeMessage(KMessageWidget::Information, true);
    m_backgroundMessage->setText(i18n("Fans following the CPU temperature only change colour while Glowzone is running. "
                                      "Closing the window quits it."));
    auto *runOn = new QAction(i18nc("@action:button", "Keep Running in Background"), m_backgroundMessage);
    connect(runOn, &QAction::triggered, m_app, [this] {
        m_app->setKeepRunning(true);
    });
    m_backgroundMessage->addAction(runOn);
    connect(m_backgroundMessage, &KMessageWidget::hideAnimationFinished, this, [this] {
        // Hidden while it still applies: the user closed it. Not again while
        // this window is open.
        if (m_lighting->followsCpuTemperature() && !m_app->keepsRunning()) {
            m_backgroundMessageDismissed = true;
        }
    });

    layout->addWidget(headerWidget);
    layout->addWidget(line(Qt::Horizontal, panel));
    layout->addWidget(scroll, 1);
    layout->addWidget(m_problemMessage);
    layout->addWidget(m_errorMessage);
    layout->addWidget(m_changedMessage);
    layout->addWidget(m_backgroundMessage);
    return panel;
}

void MainWindow::showFan(int fanIndex, int header)
{
    FanPage *page = m_fanPages.at(fanIndex);
    m_stack->setCurrentWidget(page);
    m_pageTitle->setText(page->fan().name.toString());
    if (header >= 0) {
        page->focusLed(quint8(header));
    }
}

void MainWindow::showLight(int lightIndex)
{
    m_stack->setCurrentWidget(m_lightPages.at(lightIndex));
    m_pageTitle->setText(m_model.ecLights.at(lightIndex).name.toString());
}

void MainWindow::refresh()
{
    for (FanPage *p : std::as_const(m_fanPages)) {
        p->refresh();
    }
    for (LightPage *p : std::as_const(m_lightPages)) {
        p->refresh();
    }
    m_sidebar->refresh();
    updateBanner();
    updateBackgroundMessage();
}

void MainWindow::refreshTemperature()
{
    for (FanPage *p : std::as_const(m_fanPages)) {
        p->refreshTemperature();
    }
    // The dots show the colour of the moment – it changes only now and then.
    if (const Ite8297::Rgb colour = m_lighting->cpuTemperatureColour(); colour != m_temperatureColour) {
        m_temperatureColour = colour;
        m_sidebar->refresh();
    }
}

// The command line changed the fans – maybe by applying a profile.
void MainWindow::reloadCurrentProfile()
{
    m_currentProfile = Settings::currentProfile();
    reloadProfiles();
}

void MainWindow::updateBackgroundMessage()
{
    if (m_lighting->followsCpuTemperature() && !m_app->keepsRunning() && !m_backgroundMessageDismissed) {
        showMessage(m_backgroundMessage);
    } else {
        hideMessage(m_backgroundMessage);
    }
}

// --- profiles ---------------------------------------------------------------

void MainWindow::reloadProfiles()
{
    m_profiles = Profiles::loadAll(m_model);
    if (!currentProfile()) {
        m_currentProfile.clear(); // deleted or renamed elsewhere
    }
    m_sidebar->setProfiles(m_profiles, m_currentProfile);
    updateBanner();
}

std::optional<Profile> MainWindow::currentProfile() const
{
    for (const Profile &p : m_profiles) {
        if (!m_currentProfile.isEmpty() && p.name.compare(m_currentProfile, Qt::CaseInsensitive) == 0) {
            return p;
        }
    }
    return std::nullopt;
}

void MainWindow::updateBanner()
{
    const auto profile = currentProfile();
    const bool changed = profile && !m_lighting->matches(*profile);
    m_sidebar->setCurrentProfileChanged(changed);
    if (changed) {
        m_changedMessage->setText(i18n("You changed “%1”.", profile->name));
        showMessage(m_changedMessage);
    } else {
        hideMessage(m_changedMessage);
    }
}

// Called on every change (e.g. while dragging a slider): only start an
// animation when the visibility actually changes.
void MainWindow::showMessage(KMessageWidget *message)
{
    if (message->isHideAnimationRunning() || !message->isVisible()) {
        message->animatedShow();
    }
}

void MainWindow::hideMessage(KMessageWidget *message)
{
    if (message->isShowAnimationRunning() || (message->isVisible() && !message->isHideAnimationRunning())) {
        message->animatedHide();
    }
}

void MainWindow::applyProfile(const QString &name)
{
    for (const Profile &p : std::as_const(m_profiles)) {
        if (p.name == name) {
            m_currentProfile = p.name;
            m_lighting->apply(p); // also stores it as the current profile
            m_sidebar->setProfiles(m_profiles, m_currentProfile);
            updateBanner();
            return;
        }
    }
}

void MainWindow::saveCurrentProfile()
{
    const auto old = currentProfile();
    if (!old) {
        return;
    }
    Profile p = m_lighting->current();
    p.name = old->name;
    if (!m_lighting->lightsAvailable()) {
        p.lights = old->lights; // keep what was saved
    }
    QString error;
    if (!Profiles::save(m_model, p, &error)) {
        showError(error);
        return;
    }
    reloadProfiles();
}

void MainWindow::saveNewProfile()
{
    bool ok = false;
    const QString raw = QInputDialog::getText(this,
                                              i18nc("@title:window", "Save Current Lighting"),
                                              i18nc("@label:textbox", "Profile name:"),
                                              QLineEdit::Normal,
                                              QString(),
                                              &ok);
    if (!ok) {
        return;
    }
    const QString name = Profiles::cleanName(raw);
    if (name.isEmpty()) {
        showError(i18n("A profile name must be 1 to %1 characters long.", Profiles::MaxNameLength));
        return;
    }
    if (const auto existing = Profiles::find(m_model, name)) {
        const auto answer = QMessageBox::question(this,
                                                  i18nc("@title:window", "Replace Profile"),
                                                  i18n("A profile called “%1” already exists. Replace it?", existing->name),
                                                  QMessageBox::Yes | QMessageBox::Cancel,
                                                  QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    Profile p = m_lighting->current();
    p.name = name;
    QString error;
    if (!Profiles::save(m_model, p, &error)) {
        showError(error);
        return;
    }
    m_currentProfile = name;
    Settings::setCurrentProfile(name);
    reloadProfiles();
}

void MainWindow::renameProfile(const QString &name)
{
    bool ok = false;
    const QString raw = QInputDialog::getText(this,
                                              i18nc("@title:window", "Rename Profile"),
                                              i18nc("@label:textbox", "New name:"),
                                              QLineEdit::Normal,
                                              name,
                                              &ok);
    if (!ok || raw == name) {
        return;
    }
    QString error;
    if (!Profiles::rename(m_model, name, raw, &error)) {
        showError(error);
        return;
    }
    if (m_currentProfile.compare(name, Qt::CaseInsensitive) == 0) {
        m_currentProfile = Profiles::cleanName(raw);
        Settings::setCurrentProfile(m_currentProfile);
    }
    reloadProfiles();
}

void MainWindow::deleteProfile(const QString &name)
{
    const auto answer = QMessageBox::question(this,
                                              i18nc("@title:window", "Delete Profile"),
                                              i18n("Delete the profile “%1”? The lighting stays as it is.", name),
                                              QMessageBox::Yes | QMessageBox::Cancel,
                                              QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!Profiles::remove(m_model, name, &error)) {
        showError(error);
        return;
    }
    if (m_currentProfile.compare(name, Qt::CaseInsensitive) == 0) {
        m_currentProfile.clear();
        Settings::setCurrentProfile({});
    }
    reloadProfiles();
}

// --- messages and closing ----------------------------------------------------

void MainWindow::showProblem(const QString &text)
{
    if (!m_problems.contains(text)) {
        m_problems << text;
    }
    m_problemMessage->setText(m_problems.join(QLatin1String("\n\n")));
    showMessage(m_problemMessage);
}

void MainWindow::showError(const QString &text)
{
    m_errorMessage->setText(text);
    showMessage(m_errorMessage);
}

// Coming back to the window: pick up what the CLI (e.g. a profile shortcut)
// changed meanwhile. Not more often than every few seconds – reading the
// case lights is a call to the root helper.
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || !isActiveWindow() || m_lighting->isBusy()) {
        return;
    }
    if (m_lastReload.isValid() && m_lastReload.elapsed() < 5000) {
        return;
    }
    m_lastReload.start();
    m_currentProfile = Settings::currentProfile();
    m_lighting->reloadFromDisk();
    reloadProfiles();
}

// Whether Glowzone goes on in the tray or quits is the app's decision; the
// lighting state outlives this window either way.
void MainWindow::closeEvent(QCloseEvent *event)
{
    Settings::setWindowSize(size());
    event->accept();
    Q_EMIT closed();
}

} // namespace Glowzone::Gui
