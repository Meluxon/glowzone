// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fancontroller.h"

#include <KLocalizedString>
#include <hidapi.h>

namespace Glowzone
{

static QString hidError(hid_device *dev)
{
    const wchar_t *e = hid_error(dev);
    return e ? QString::fromWCharArray(e) : QString();
}

FanController::FanController(const ModelProfile &model)
    : m_model(model)
{
}

FanController::~FanController()
{
    if (m_dev) {
        hid_close(m_dev);
    }
    hid_exit();
}

bool FanController::open(QString *error)
{
    if (m_dev) {
        return true;
    }
    if (hid_init() != 0) {
        *error = i18n("Could not initialise HID access: %1", hidError(nullptr));
        return false;
    }
    m_dev = hid_open(m_model.usbVendorId, m_model.usbProductId, nullptr);
    if (!m_dev) {
        const QString id = QStringLiteral("%1:%2")
                               .arg(m_model.usbVendorId, 4, 16, QLatin1Char('0'))
                               .arg(m_model.usbProductId, 4, 16, QLatin1Char('0'))
                               .toUpper();
        *error = i18n("Fan LED controller %1 not found or not accessible (%2).", id, hidError(nullptr));
        return false;
    }
    return true;
}

bool FanController::apply(const QList<Ite8297::HeaderSetting> &settings, QString *error)
{
    if (!open(error)) {
        return false;
    }
    for (const QByteArray &report : Ite8297::reportsFor(settings)) {
        const int sent = hid_send_feature_report(m_dev, reinterpret_cast<const unsigned char *>(report.constData()), size_t(report.size()));
        if (sent < 0) {
            *error = i18n("Sending to the fan LED controller failed: %1", hidError(m_dev));
            // Unplugged or reset? Reopen on the next call.
            hid_close(m_dev);
            m_dev = nullptr;
            return false;
        }
    }
    return true;
}

} // namespace Glowzone
