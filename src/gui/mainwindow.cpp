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

#include <algorithm>

using namespace Qt::StringLiterals;

static constexpr double PageTitleScale = 1.3;
static constexpr int HeaderHeightInLines = 3;
static constexpr int DefaultWidthInChars = 125; // the first start
static constexpr int DefaultHeightInLines = 36;

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

// Header rows of the sidebar and the page share one height, like System
// Settings. On a new widget: parentWidget().
static QHBoxLayout *headerRow(QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setFixedHeight(w->fontMetrics().height() * HeaderHeightInLines);
    return new QHBoxLayout(w);
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
    connect(m_lighting, &Lighting::savedChanged, this, &MainWindow::reloadProfiles); // e.g. a profile applied by the CLI
    connect(m_lighting, &Lighting::temperatureChanged, this, &MainWindow::refreshTemperature);
    connect(m_app, &App::backgroundChanged, this, &MainWindow::updateBackgroundMessage);
    connect(m_lighting, &Lighting::problemsChanged, this, &MainWindow::updateProblems);
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

    reloadProfiles();
    m_sidebar->selectFirst();
    updateProblems(); // found before this window was opened
    updateBackgroundMessage();

    if (!restoreGeometry(Settings::windowGeometry())) {
        resize(fontMetrics().averageCharWidth() * DefaultWidthInChars, fontMetrics().height() * DefaultHeightInLines);
    }
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

    QHBoxLayout *header = headerRow(panel);
    QWidget *headerWidget = header->parentWidget();
    auto *modelName = new QLabel(m_model.productVersion, headerWidget);
    modelName->setToolTip(i18nc("@info:tooltip", "Controlling: %1", m_model.displayName()));
    auto *menuButton = new QToolButton(headerWidget);
    menuButton->setIcon(QIcon::fromTheme(u"application-menu"_s));
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
    menu->addAction(QIcon::fromTheme(u"help-about"_s), i18nc("@action:inmenu", "About Glowzone"), this, [this] {
        auto *about = new AboutDialog(m_lighting, this);
        about->setAttribute(Qt::WA_DeleteOnClose);
        about->open();
    });
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(u"application-exit"_s), i18nc("@action:inmenu", "Quit"), this, &MainWindow::quitRequested);
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

    QHBoxLayout *header = headerRow(panel);
    QWidget *headerWidget = header->parentWidget();
    m_pageTitle = new QLabel(headerWidget);
    QFont f = m_pageTitle->font();
    f.setPointSizeF(f.pointSizeF() * PageTitleScale);
    m_pageTitle->setFont(f);
    auto *allOff = new QPushButton(QIcon::fromTheme(u"system-shutdown-symbolic"_s), i18nc("@action:button", "Turn All Off"), headerWidget);
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
    auto *save = new QAction(QIcon::fromTheme(u"document-save"_s), i18nc("@action:button", "Save"), m_changedMessage);
    auto *saveNew = new QAction(QIcon::fromTheme(u"document-save-as"_s), i18nc("@action:button", "Save as New…"), m_changedMessage);
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
    m_sidebar->setProfiles(m_profiles, m_lighting->currentProfile());
    updateBanner();
}

std::optional<Profile> MainWindow::currentProfile() const
{
    const QString name = m_lighting->currentProfile();
    for (const Profile &p : m_profiles) {
        if (!name.isEmpty() && p.name.compare(name, Qt::CaseInsensitive) == 0) {
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
            m_lighting->apply(p); // also stores it as the current profile
            m_sidebar->setProfiles(m_profiles, p.name);
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
    QString error;
    if (!Profiles::save(m_model, p, &error)) {
        showError(error);
        return;
    }
    reloadProfiles();
}

void MainWindow::saveNewProfile()
{
    askName(i18nc("@title:window", "Save Current Lighting"), i18nc("@label:textbox", "Profile name:"), QString(), [this](const QString &raw) {
        QString error;
        const auto name = Profiles::checkedName(raw, &error);
        if (!name) {
            showError(error);
            return;
        }
        if (const auto existing = Profiles::find(m_model, *name)) {
            confirm(i18nc("@title:window", "Replace Profile"),
                    i18n("A profile called “%1” already exists. Replace it?", existing->name),
                    [this, name = *name] {
                        saveAs(name);
                    });
            return;
        }
        saveAs(*name);
    });
}

void MainWindow::saveAs(const QString &name)
{
    Profile p = m_lighting->current();
    p.name = name;
    QString error;
    if (!Profiles::save(m_model, p, &error)) {
        showError(error);
        return;
    }
    m_lighting->setCurrentProfile(name);
    reloadProfiles();
}

void MainWindow::renameProfile(const QString &name)
{
    askName(i18nc("@title:window", "Rename Profile"), i18nc("@label:textbox", "New name:"), name, [this, name](const QString &raw) {
        if (raw == name) {
            return;
        }
        QString error;
        if (!Profiles::rename(m_model, name, raw, &error)) {
            showError(error);
            return;
        }
        if (m_lighting->currentProfile().compare(name, Qt::CaseInsensitive) == 0) {
            m_lighting->setCurrentProfile(Profiles::cleanName(raw));
        }
        reloadProfiles();
    });
}

void MainWindow::deleteProfile(const QString &name)
{
    confirm(i18nc("@title:window", "Delete Profile"), i18n("Delete the profile “%1”? The lighting stays as it is.", name), [this, name] {
        QString error;
        if (!Profiles::remove(m_model, name, &error)) {
            showError(error);
            return;
        }
        if (m_lighting->currentProfile().compare(name, Qt::CaseInsensitive) == 0) {
            m_lighting->setCurrentProfile({});
        }
        reloadProfiles();
    });
}

// Dialogs are opened, never exec()'d: the window may be closed from the tray
// while one is open. It then takes the dialog along, and the answer with it –
// no nested event loop returns into a deleted window.
void MainWindow::askName(const QString &title, const QString &label, const QString &text, const std::function<void(const QString &)> &done)
{
    auto *dialog = new QInputDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setTextValue(text);
    connect(dialog, &QInputDialog::textValueSelected, this, done);
    dialog->open();
}

void MainWindow::confirm(const QString &title, const QString &question, const std::function<void()> &yes)
{
    auto *box = new QMessageBox(QMessageBox::Question, title, question, QMessageBox::Yes | QMessageBox::Cancel, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setDefaultButton(QMessageBox::Cancel);
    connect(box->button(QMessageBox::Yes), &QAbstractButton::clicked, this, yes);
    box->open();
}

// --- messages and closing ----------------------------------------------------

// Shown again (if it was closed) only when a problem is new.
void MainWindow::updateProblems()
{
    const QStringList problems = m_lighting->problems();
    const bool added = std::any_of(problems.cbegin(), problems.cend(), [this](const QString &p) {
        return !m_problems.contains(p);
    });
    m_problems = problems;
    if (problems.isEmpty()) {
        hideMessage(m_problemMessage);
        return;
    }
    m_problemMessage->setText(problems.join("\n\n"_L1));
    if (added) {
        showMessage(m_problemMessage);
    }
}

void MainWindow::showError(const QString &text)
{
    m_errorMessage->setText(text);
    showMessage(m_errorMessage);
}

// Whether Glowzone goes on in the tray or quits is the app's decision; the
// lighting state outlives this window either way.
void MainWindow::closeEvent(QCloseEvent *event)
{
    Settings::setWindowGeometry(saveGeometry());
    event->accept();
    Q_EMIT closed();
}

} // namespace Glowzone::Gui
