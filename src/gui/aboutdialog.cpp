// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "aboutdialog.h"
#include "lighting.h"

#include "config-glowzone.h"

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

    const QString fans = lighting->fansAvailable() ? i18nc("@info hardware state", "Connected") : i18nc("@info hardware state", "Not found");
    const QString lights = lighting->lightsAvailable() ? i18nc("@info hardware state", "Available")
        : lighting->lightsFailed()                     ? i18nc("@info hardware state", "Not available")
                                                       : i18nc("@info hardware state", "Not read yet");
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
    addRow(i18nc("@label", "Fan controller:"), i18nc("@info USB id, state", "USB %1 – %2", usbId, fans));
    if (!model.ecLights.isEmpty()) {
        addRow(i18nc("@label", "Case lights:"), lights);
    }
    QString detection;
    if (lighting->fanLayout() < 0) {
        detection = i18nc("@info hardware state", "Not read yet");
    } else if (lighting->fanLayout() == model.normalFanLayout) {
        detection = i18nc("@info BIOS fan detection, %1 layout code", "All fans detected (layout 0x%1)", QString::number(lighting->fanLayout(), 16));
    } else {
        detection = i18nc("@info BIOS fan detection, %1 layout code", "Unusual layout 0x%1 – see the message in the window", QString::number(lighting->fanLayout(), 16));
    }
    addRow(i18nc("@label BIOS fan detection", "Fan detection:"), detection);
    addRow(i18nc("@label", "Licence:"), QStringLiteral("GPL-2.0-or-later"));
    addRow(i18nc("@label", "Copyright:"), i18n("© 2026 Meluxon"));
    layout->addLayout(form);

    m_report = QStringLiteral("Glowzone %1\n"
                              "Computer: %2 (board %3)\n"
                              "Fan controller: USB %4 – %5\n"
                              "Case lights: %6\n"
                              "BIOS fan layout: %10 (normal %11)\n"
                              "Qt: %7, KDE Frameworks: %8\n"
                              "System: %9")
                   .arg(QStringLiteral(GLOWZONE_VERSION_STRING),
                        model.displayName(),
                        QLatin1String(model.boardName),
                        usbId,
                        lighting->fansAvailable() ? QStringLiteral("connected") : QStringLiteral("not found"),
                        lighting->lightsAvailable() ? QStringLiteral("available")
                                                    : (lighting->lightsFailed() ? QStringLiteral("not available") : QStringLiteral("not read yet")),
                        QString::fromLatin1(qVersion()),
                        KCoreAddons::versionString(),
                        QSysInfo::prettyProductName() + QLatin1String(" (") + QSysInfo::kernelVersion() + QLatin1Char(')'))
                   .arg(lighting->fanLayout() < 0 ? QStringLiteral("unknown") : QStringLiteral("0x") + QString::number(lighting->fanLayout(), 16),
                        QStringLiteral("0x") + QString::number(model.normalFanLayout, 16));

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
