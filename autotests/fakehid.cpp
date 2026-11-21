// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fakehid.h"

#include "core/ite8297.h"

#include <cstdlib>
#include <cstring>
#include <hidapi.h>

using namespace Qt::StringLiterals;

namespace FakeHid
{
QList<QByteArray> reports;
int failures = 0;
int shortWrites = 0;
bool missing = false;

void reset()
{
    reports.clear();
    failures = 0;
    shortWrites = 0;
    missing = false;
}

QStringList sent()
{
    QStringList out;
    for (const QByteArray &r : std::as_const(reports)) {
        if (r.at(1) == char(0x28) || r.at(1) == char(0x2D)) {
            out << u"commit"_s;
        } else {
            out << u"%1:%2"_s.arg(quint8(r.at(1)), 2, 16, u'0').arg(QString::fromLatin1(r.mid(5, 3).toHex()));
        }
    }
    reports.clear();
    return out;
}
}

static int s_device;

extern "C" {
int hid_init(void)
{
    return 0;
}
int hid_exit(void)
{
    return 0;
}
hid_device_info *hid_enumerate(unsigned short, unsigned short)
{
    if (FakeHid::missing) {
        return nullptr;
    }
    auto *info = static_cast<hid_device_info *>(calloc(1, sizeof(hid_device_info)));
    info->path = strdup("/dev/hidraw-test");
    info->vendor_id = 0x17ef;
    info->product_id = 0xc955;
    info->usage_page = Glowzone::Ite8297::UsagePage;
    info->usage = Glowzone::Ite8297::Usage;
    return info;
}
void hid_free_enumeration(hid_device_info *info)
{
    while (info) {
        hid_device_info *next = info->next;
        free(info->path);
        free(info);
        info = next;
    }
}
hid_device *hid_open_path(const char *)
{
    return reinterpret_cast<hid_device *>(&s_device);
}
void hid_close(hid_device *)
{
}
const wchar_t *hid_error(hid_device *)
{
    return L"fake";
}
int hid_send_feature_report(hid_device *, const unsigned char *data, size_t length)
{
    if (FakeHid::failures > 0) {
        --FakeHid::failures;
        return -1;
    }
    if (FakeHid::shortWrites > 0) {
        --FakeHid::shortWrites;
        return int(length) - 1;
    }
    FakeHid::reports << QByteArray(reinterpret_cast<const char *>(data), int(length));
    return int(length);
}
}
