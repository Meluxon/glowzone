// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fanstate.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace Glowzone::FanState
{

static constexpr int FormatVersion = 1;

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QLatin1String("/fans.json");
}

static QString headerKey(quint8 header)
{
    return QStringLiteral("0x%1").arg(header, 2, 16, QLatin1Char('0'));
}

QJsonObject headersToJson(const Headers &headers)
{
    QJsonObject h;
    for (const Ite8297::HeaderSetting &s : headers) {
        h.insert(headerKey(s.header),
                 QJsonObject{
                     {QLatin1String("mode"), QLatin1String(Ite8297::modeInfo(s.mode)->id)},
                     {QLatin1String("speed"), s.speed},
                     {QLatin1String("brightness"), s.brightness},
                     {QLatin1String("colour"), Ite8297::rgbToString(s.colour)},
                 });
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

bool fitsModel(const ModelProfile &model, const Ite8297::HeaderSetting &s)
{
    bool ownHeader = false;
    for (const FanDef &fan : model.fans) {
        ownHeader = ownHeader || fan.headers().contains(s.header);
    }
    return ownHeader && model.hasFanMode(quint8(s.mode)) && s.speed >= Ite8297::MinLevel && s.speed <= model.fanLevels
        && s.brightness >= Ite8297::MinLevel && s.brightness <= model.fanLevels;
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

bool save(const Headers &headers, QString *error)
{
    const QJsonObject root{{QLatin1String("version"), FormatVersion}, {QLatin1String("headers"), headersToJson(headers)}};

    const QString path = filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = i18n("Could not create the directory for %1.", path);
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson()) < 0 || !f.commit()) {
        *error = i18n("Could not save the fan state to %1: %2", path, f.errorString());
        return false;
    }
    return true;
}

bool merge(const QList<Ite8297::HeaderSetting> &changed, QString *error)
{
    // Keep whatever else is in the file untouched.
    Headers all = loadFile();
    for (const Ite8297::HeaderSetting &s : changed) {
        all.insert(s.header, s);
    }
    return save(all, error);
}

} // namespace Glowzone::FanState
