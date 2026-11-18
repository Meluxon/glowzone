// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "eclights.h"

#include "helpercall.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace Glowzone::EcLights
{

const QList<ModeInfo> &modes()
{
    static const QList<ModeInfo> list = {
        {1, "off", kli18nc("@item light mode", "Off")},
        {2, "on", kli18nc("@item light mode", "On")},
        {3, "blink-fast", kli18nc("@item light mode", "Blink fast")},
        {4, "blink-slow", kli18nc("@item light mode", "Blink slow")},
        {5, "breathe", kli18nc("@item light mode", "Breathing")},
    };
    return list;
}

QList<ModeInfo> modesFor(const ModelProfile &model)
{
    QList<ModeInfo> list;
    for (const ModeInfo &m : modes()) {
        if (model.hasEcLightMode(m.value)) {
            list << m;
        }
    }
    return list;
}

const ModeInfo *modeInfo(quint8 value)
{
    for (const ModeInfo &m : modes()) {
        if (m.value == value) {
            return &m;
        }
    }
    return nullptr;
}

const ModeInfo *modeInfo(const QString &id)
{
    for (const ModeInfo &m : modes()) {
        if (id == QLatin1String(m.id)) {
            return &m;
        }
    }
    return nullptr;
}

std::optional<QList<EcLightState>> status(QString *error)
{
    KAuthHelperCalls calls;
    HelperResult r = runToEnd(calls, HelperCall::Status);
    *error = r.error;
    return r.ok() ? std::optional(r.lights) : std::nullopt;
}

static constexpr int StateFormatVersion = 1;

QString stateFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QLatin1String("/lights.json");
}

// {"version": 1, "lights": {"1": {"mode": 2, "brightness": 3}, ...}}. If it
// can't be written, a running window shows what it read last.
void saveState(const QList<EcLightState> &lights)
{
    QJsonObject entries;
    for (const EcLightState &l : lights) {
        entries.insert(QString::number(l.index), QJsonObject{{QLatin1String("mode"), l.mode}, {QLatin1String("brightness"), l.brightness}});
    }
    const QString path = stateFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{QLatin1String("version"), StateFormatVersion}, {QLatin1String("lights"), entries}}).toJson());
        f.commit();
    }
}

std::optional<QList<EcLightState>> loadState(const ModelProfile &model)
{
    QFile f(stateFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QLatin1String("version")).toInt() != StateFormatVersion) {
        return std::nullopt;
    }
    QList<EcLightState> lights;
    const QJsonObject entries = root.value(QLatin1String("lights")).toObject();
    for (const EcLightDef &def : model.ecLights) {
        const QJsonObject o = entries.value(QString::number(def.index)).toObject();
        const int mode = o.value(QLatin1String("mode")).toInt();
        const int brightness = o.value(QLatin1String("brightness")).toInt();
        if (mode > 0 && mode <= 0xff && model.hasEcLightMode(quint8(mode)) && brightness >= MinBrightness && brightness <= model.ecBrightnessLevels) {
            lights << EcLightState{def.index, quint8(mode), quint8(brightness)};
        }
    }
    return lights.isEmpty() ? std::nullopt : std::optional(lights);
}

std::optional<QList<EcLightState>> set(const QList<EcRequest> &requests, QString *error)
{
    KAuthHelperCalls calls;
    HelperResult r = runToEnd(calls, HelperCall::Set, requests);
    *error = r.error;
    if (!r.ok()) {
        return std::nullopt;
    }
    saveState(r.lights);
    return r.lights;
}

std::optional<int> layout(QString *error)
{
    KAuthHelperCalls calls;
    HelperResult r = runToEnd(calls, HelperCall::Layout);
    *error = r.error;
    return r.ok() ? std::optional(r.layout) : std::nullopt;
}

QString layoutCode(int code)
{
    return QStringLiteral("0x%1").arg(code, 2, 16, QLatin1Char('0'));
}

QString layoutWarning(const ModelProfile &model, int code)
{
    if (code == model.normalFanLayout) {
        return {};
    }
    QStringList missing;
    for (const FanDef *fan : model.undetectedFans(code)) {
        missing << fan->name.toString();
    }
    const QString why = i18n("This is a BIOS detection glitch that can happen after a cold start, when a fan isn’t spinning yet. "
                             "The fan LEDs usually still work, and your settings are unchanged. A reboot normally clears it.");
    if (!missing.isEmpty()) {
        return i18nc("%1 fan names, %2 and %3 layout codes, %4 explanation",
                     "The BIOS doesn’t detect these fans right now: %1 (fan layout %2, normally %3).\n%4",
                     missing.join(QLatin1String(", ")),
                     layoutCode(code),
                     layoutCode(model.normalFanLayout),
                     why);
    }
    return i18nc("%1 and %2 layout codes, %3 explanation",
                 "The BIOS reports an unusual fan layout right now (%1, normally %2); a fan may not be detected.\n%3",
                 layoutCode(code),
                 layoutCode(model.normalFanLayout),
                 why);
}

} // namespace Glowzone::EcLights
