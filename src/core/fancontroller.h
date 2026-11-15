// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ite8297.h"
#include "model/modelprofile.h"

#include <QString>

struct hid_device_;

namespace Glowzone
{

// The fan LED controller over hidraw. Needs no root: the udev rule tags the
// device "uaccess" for the logged-in user.
class FanController
{
public:
    explicit FanController(const ModelProfile &model);
    ~FanController();
    FanController(const FanController &) = delete;
    FanController &operator=(const FanController &) = delete;

    // Error messages are translated and ready to show.
    bool open(QString *error);
    bool apply(const QList<Ite8297::HeaderSetting> &settings, QString *error);

private:
    const ModelProfile &m_model;
    hid_device_ *m_dev = nullptr;
    bool m_hidInitialised = false; // hidapi's init/exit are process-wide: counted
};

} // namespace Glowzone
