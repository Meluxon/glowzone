// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fanstate.h"
#include "settings.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDeadlineTimer>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace Glowzone::FanState
{

using namespace std::chrono_literals;

static constexpr int FormatVersion = 1;

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + "/fans.json"_L1;
}

static QString headerKey(quint8 header)
{
    return u"0x%1"_s.arg(header, 2, 16, u'0');
}

static QJsonObject settingToJson(Ite8297::HeaderSetting s)
{
    return {
        {"mode"_L1, QLatin1String(Ite8297::modeInfo(s.mode)->id)},
        {"speed"_L1, s.speed},
        {"brightness"_L1, s.brightness},
        {"colour"_L1, Ite8297::rgbToString(s.colour)},
    };
}

QJsonObject headersToJson(const Headers &headers)
{
    QJsonObject h;
    for (const Ite8297::HeaderSetting &s : headers) {
        h.insert(headerKey(s.header), settingToJson(s));
    }
    return h;
}

bool fitsModel(const ModelProfile &model, Ite8297::HeaderSetting setting)
{
    const bool ownHeader = std::ranges::any_of(model.fans, [&setting](const FanDef &fan) {
        return fan.headers().contains(setting.header);
    });
    return ownHeader && model.hasFanMode(quint8(setting.mode)) && setting.speed >= Ite8297::MinLevel && setting.speed <= model.fanLevels
        && setting.brightness >= Ite8297::MinLevel && setting.brightness <= model.fanLevels;
}

Headers headersFromJson(const QJsonObject &headers, const ModelProfile &model)
{
    Headers result;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        bool ok = false;
        const uint header = it.key().toUInt(&ok, 0);
        const QJsonObject o = it.value().toObject();
        const Ite8297::ModeInfo *mode = Ite8297::modeInfo(o.value("mode"_L1).toString());
        const auto colour = Ite8297::parseRgb(o.value("colour"_L1).toString());
        const int speed = o.value("speed"_L1).toInt();
        const int brightness = o.value("brightness"_L1).toInt();
        if (!ok || header > 0xff || !mode || !colour || speed < 0 || speed > 0xff || brightness < 0 || brightness > 0xff) {
            continue; // not even a value the controller could take
        }
        const Ite8297::HeaderSetting s{quint8(header), mode->mode, quint8(speed), quint8(brightness), *colour};
        if (fitsModel(model, s)) {
            result.insert(s.header, s);
        }
    }
    return result;
}

bool linked(const FanDef &fan, const Headers &saved)
{
    if (const auto chosen = Settings::linked(QLatin1String(fan.id))) {
        return *chosen;
    }
    QList<Ite8297::HeaderSetting> known;
    for (const LedDef &led : fan.leds) {
        if (saved.contains(led.header)) {
            Ite8297::HeaderSetting s = saved.value(led.header);
            s.header = 0;
            known << s;
        }
    }
    return known.size() < 2 || known.count(known.constFirst()) == known.size();
}

Headers load(const ModelProfile &model)
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value("version"_L1).toInt() != FormatVersion) {
        return {};
    }
    return headersFromJson(root.value("headers"_L1).toObject(), model);
}

// Only the changed headers are replaced; every other entry stays exactly as it
// is in the file, even one this version doesn't understand.
bool merge(const Lock &, const QList<Ite8297::HeaderSetting> &changed, QString *error)
{
    const QString path = filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = i18n("Could not create the directory for %1.", path);
        return false;
    }

    // No file: start empty. A file that can't be opened (permissions, I/O
    // error) is left alone, as replacing it would lose the other entries.
    QJsonObject headers;
    QFile current(path);
    if (current.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(current.readAll(), &parseError);
        current.close();
        const QJsonObject root = doc.object();
        const int version = root.value("version"_L1).toInt();
        const bool stateFile = parseError.error == QJsonParseError::NoError && doc.isObject() && version >= FormatVersion
            && (version > FormatVersion || root.value("headers"_L1).isObject());
        if (!stateFile) {
            // Keep a copy for inspection and start afresh. rename() replaces an
            // older copy only once this one is in its place.
            const QString broken = path + ".broken"_L1;
            if (::rename(QFile::encodeName(path).constData(), QFile::encodeName(broken).constData()) != 0) {
                *error = i18n("%1 is damaged and could not be moved aside to %2.", path, broken);
                return false;
            }
        } else if (version > FormatVersion) {
            *error = i18n("%1 was saved by a newer version of Glowzone and is left as it is.", path);
            return false;
        } else {
            headers = root.value("headers"_L1).toObject();
        }
    } else if (current.exists()) {
        *error = i18n("Could not read %1: %2", path, current.errorString());
        return false;
    }
    for (const Ite8297::HeaderSetting &s : changed) {
        headers.insert(headerKey(s.header), settingToJson(s));
    }

    const QByteArray data = QJsonDocument(QJsonObject{{"version"_L1, FormatVersion}, {"headers"_L1, headers}}).toJson();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = i18n("Could not save the fan state to %1: %2", path, f.errorString());
        return false;
    }
    return true;
}

Lock::Lock()
{
    const QString path = QFileInfo(filePath()).absolutePath() + "/fans.lock"_L1;
    const QByteArray name = QFile::encodeName(path);
    m_fd = ::open(name.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_fd < 0 && errno == ENOENT && QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_fd = ::open(name.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    }
    if (m_fd < 0) {
        return; // can't lock: go ahead without
    }
    const QDeadlineTimer deadline(2s);
    while (::flock(m_fd, LOCK_EX | LOCK_NB) != 0 && (errno == EWOULDBLOCK || errno == EINTR) && !deadline.hasExpired()) {
        QThread::msleep(10);
    }
}

Lock::~Lock()
{
    if (m_fd >= 0) {
        ::close(m_fd); // releases the lock
    }
}

} // namespace Glowzone::FanState
