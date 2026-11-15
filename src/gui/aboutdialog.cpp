// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "aboutdialog.h"
#include "lighting.h"

#include "config-glowzone.h"
#include "core/eclights.h"

#include <KCoreAddons>
#include <KLocalizedString>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

namespace Glowzone::Gui
{

AboutDialog::AboutDialog(const Lighting *lighting, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "About Glowzone"));
    const ModelProfile &model = lighting->model();

    auto *layout = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(QApplication::windowIcon().pixmap(64, 64));
    auto *title = new QLabel(this);
    QFont big = title->font();
    big.setPointSizeF(big.pointSizeF() * 1.4);
    title->setFont(big);
    title->setText(i18nc("@title app name and version", "Glowzone %1", QStringLiteral(GLOWZONE_VERSION_STRING)));
    auto *subtitle = new QLabel(i18n("Lighting control for supported Lenovo desktops"), this);
    auto *titles = new QVBoxLayout;
    titles->addWidget(title);
    titles->addWidget(subtitle);
    top->addWidget(icon);
    top->addLayout(titles, 1);
    layout->addLayout(top);

    auto *note = new QLabel(i18n("Written with an AI assistant (Claude) and reviewed by its author. "
                                 "It works on the author’s machine; read the code before you trust it with yours."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);

    // Each value twice: translated for the dialog, English for the bug report.
    struct State {
        QString shown;
        QString report;
    };
    const State fans = lighting->fansAvailable() ? State{i18nc("@info hardware state", "Connected"), QStringLiteral("connected")}
                                                 : State{i18nc("@info hardware state", "Not found"), QStringLiteral("not found")};
    State lights{i18nc("@info hardware state", "Not read yet"), QStringLiteral("not read yet")};
    if (lighting->lightsAvailable()) {
        lights = {i18nc("@info hardware state", "Available"), QStringLiteral("available")};
    } else if (lighting->lightsFailed()) {
        lights = {i18nc("@info hardware state", "Not available"), QStringLiteral("not available")};
    }
    const int layoutCode = lighting->fanLayout();
    State detection{i18nc("@info hardware state", "Not read yet"), QStringLiteral("unknown")};
    if (layoutCode == model.normalFanLayout) {
        detection = {i18nc("@info BIOS fan detection, %1 layout code", "All fans detected (layout %1)", EcLights::layoutCode(layoutCode)),
                     EcLights::layoutCode(layoutCode)};
    } else if (layoutCode >= 0) {
        detection = {i18nc("@info BIOS fan detection, %1 layout code", "Unusual layout %1 – see the message in the window", EcLights::layoutCode(layoutCode)),
                     EcLights::layoutCode(layoutCode) + QLatin1String(", normally ") + EcLights::layoutCode(model.normalFanLayout)};
    }
    const QString usbId = QStringLiteral("%1:%2")
                              .arg(model.usbVendorId, 4, 16, QLatin1Char('0'))
                              .arg(model.usbProductId, 4, 16, QLatin1Char('0'))
                              .toUpper();

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    auto addRow = [this, form](const QString &label, const QString &value) {
        auto *v = new QLabel(value, this);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, v);
    };
    addRow(i18nc("@label", "Computer:"), model.displayName());
    addRow(i18nc("@label", "Board:"), QLatin1String(model.boardName));
    addRow(i18nc("@label", "Fan controller:"), i18nc("@info USB id, state", "USB %1 – %2", usbId, fans.shown));
    if (!model.ecLights.isEmpty()) {
        addRow(i18nc("@label", "Case lights:"), lights.shown);
    }
    addRow(i18nc("@label BIOS fan detection", "Fan detection:"), detection.shown);
    addRow(i18nc("@label", "Licence:"), QStringLiteral("GPL-2.0-or-later"));
    addRow(i18nc("@label", "Copyright:"), i18n("© 2026 Meluxon"));
    layout->addLayout(form);

    m_report = QStringList{
        QStringLiteral("Glowzone ") + QStringLiteral(GLOWZONE_VERSION_STRING),
        QStringLiteral("Computer: %1 (board %2)").arg(model.displayName(), QLatin1String(model.boardName)),
        QStringLiteral("Fan controller: USB %1 – %2").arg(usbId, fans.report),
        QStringLiteral("Case lights: ") + lights.report,
        QStringLiteral("BIOS fan layout: ") + detection.report,
        QStringLiteral("Qt: %1, KDE Frameworks: %2").arg(QString::fromLatin1(qVersion()), KCoreAddons::versionString()),
        QStringLiteral("System: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion()),
    }.join(QLatin1Char('\n'));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copy = buttons->addButton(i18nc("@action:button", "Copy to Clipboard"), QDialogButtonBox::ActionRole);
    copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copy->setToolTip(i18nc("@info:tooltip", "Copy version and hardware details, e.g. for a bug report"));
    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_report);
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace Glowzone::Gui
