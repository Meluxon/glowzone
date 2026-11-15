// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fanstate.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace Glowzone::FanState
{

using namespace std::chrono_literals;

static constexpr int FormatVersion = 1;

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QLatin1String("/fans.json");
}

static QString headerKey(quint8 header)
{
    return QStringLiteral("0x%1").arg(header, 2, 16, QLatin1Char('0'));
}

static QJsonObject settingToJson(Ite8297::HeaderSetting s)
{
    return {
        {QLatin1String("mode"), QLatin1String(Ite8297::modeInfo(s.mode)->id)},
        {QLatin1String("speed"), s.speed},
        {QLatin1String("brightness"), s.brightness},
        {QLatin1String("colour"), Ite8297::rgbToString(s.colour)},
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

// Protocol-level check only: a header the controller has, a known mode, sane values.
static Headers parseHeaders(const QJsonObject &headers)
{
    Headers result;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        bool ok = false;
        const uint header = it.key().toUInt(&ok, 0);
        if (!ok || header > 0xff || !Ite8297::isKnownHeader(quint8(header))) {
            continue;
        }
        const QJsonObject o = it.value().toObject();
        const Ite8297::ModeInfo *mode = Ite8297::modeInfo(o.value(QLatin1String("mode")).toString());
        const auto colour = Ite8297::parseRgb(o.value(QLatin1String("colour")).toString());
        const int speed = o.value(QLatin1String("speed")).toInt();
        const int brightness = o.value(QLatin1String("brightness")).toInt();
        if (!mode || !colour || speed < Ite8297::MinLevel || speed > 0xff || brightness < Ite8297::MinLevel || brightness > 0xff) {
            continue;
        }
        result.insert(quint8(header), {quint8(header), mode->mode, quint8(speed), quint8(brightness), *colour});
    }
    return result;
}

bool fitsModel(const ModelProfile &model, Ite8297::HeaderSetting setting)
{
    bool ownHeader = false;
    for (const FanDef &fan : model.fans) {
        ownHeader = ownHeader || fan.headers().contains(setting.header);
    }
    return ownHeader && model.hasFanMode(quint8(setting.mode)) && setting.speed >= Ite8297::MinLevel && setting.speed <= model.fanLevels
        && setting.brightness >= Ite8297::MinLevel && setting.brightness <= model.fanLevels;
}

Headers headersFromJson(const QJsonObject &headers, const ModelProfile &model)
{
    Headers result = parseHeaders(headers);
    result.removeIf([&model](const auto &it) {
        return !fitsModel(model, it.value());
    });
    return result;
}

bool defaultLinked(const FanDef &fan, const Headers &saved)
{
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

static Headers loadFile()
{
    Headers result;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QLatin1String("version")).toInt() != FormatVersion) {
        return result;
    }
    return parseHeaders(root.value(QLatin1String("headers")).toObject());
}

Headers load(const ModelProfile &model)
{
    Headers result = loadFile();
    result.removeIf([&model](const auto &it) {
        return !fitsModel(model, it.value());
    });
    return result;
}

// Only the changed headers are replaced; every other entry stays exactly as it
// is in the file, even one this version doesn't understand. The window and the
// CLI may both save at the same moment, hence the lock.
bool merge(const QList<Ite8297::HeaderSetting> &changed, QString *error)
{
    const QString path = filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = i18n("Could not create the directory for %1.", path);
        return false;
    }
    QLockFile lock(path + QLatin1String(".lock"));
    lock.setStaleLockTime(30s);
    if (!lock.tryLock(5s)) {
        *error = i18n("Could not save the fan state to %1: %2", path, i18n("another Glowzone is saving it"));
        return false;
    }

    QJsonObject headers;
    QFile current(path);
    if (current.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(current.readAll(), &parseError);
        current.close();
        const int version = doc.object().value(QLatin1String("version")).toInt();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            // Unreadable: keep a copy for inspection, start afresh.
            QFile::remove(path + QLatin1String(".broken"));
            QFile::rename(path, path + QLatin1String(".broken"));
        } else if (version > FormatVersion) {
            *error = i18n("%1 was saved by a newer version of Glowzone and is left as it is.", path);
            return false;
        } else if (version == FormatVersion) {
            headers = doc.object().value(QLatin1String("headers")).toObject();
        }
    }
    for (const Ite8297::HeaderSetting &s : changed) {
        headers.insert(headerKey(s.header), settingToJson(s));
    }

    const QByteArray data = QJsonDocument(QJsonObject{{QLatin1String("version"), FormatVersion}, {QLatin1String("headers"), headers}}).toJson();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = i18n("Could not save the fan state to %1: %2", path, f.errorString());
        return false;
    }
    return true;
}

} // namespace Glowzone::FanState
