// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "eclights.h"
#include "model/find.h"

#include "helpercall.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

using namespace Qt::StringLiterals;

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
    return findIf(modes(), [value](const ModeInfo &m) { return m.value == value; });
}

const ModeInfo *modeInfo(const QString &id)
{
    return findIf(modes(), [&id](const ModeInfo &m) { return id == QLatin1String(m.id); });
}

QJsonObject lightsToJson(const ModelProfile &model, const QList<EcLightState> &lights)
{
    QJsonObject json;
    for (const EcLightState &l : lights) {
        const EcLightDef *def = model.ecLightByIndex(l.index);
        const ModeInfo *mode = modeInfo(l.mode);
        if (def && mode) {
            json.insert(QLatin1String(def->id), QJsonObject{{"mode"_L1, QLatin1String(mode->id)}, {"brightness"_L1, l.brightness}});
        }
    }
    return json;
}

QMap<quint8, EcLightState> lightsFromJson(const ModelProfile &model, const QJsonObject &json)
{
    QMap<quint8, EcLightState> lights;
    for (const EcLightDef &def : model.ecLights) {
        const QJsonObject o = json.value(QLatin1String(def.id)).toObject();
        const ModeInfo *mode = modeInfo(o.value("mode"_L1).toString());
        const int brightness = o.value("brightness"_L1).toInt();
        if (mode && model.hasEcLightMode(mode->value) && brightness >= MinBrightness && brightness <= model.ecBrightnessLevels) {
            lights.insert(def.index, {def.index, mode->value, quint8(brightness)});
        }
    }
    return lights;
}

static constexpr int StateFormatVersion = 2;

QString stateFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + "/lights.json"_L1;
}

// {"version": 2, "written": "<uuid>", "lights": {"logo": {"mode": "on", "brightness": 3}, ...}}.
// If it can't be written, a running window shows what it read last.
void saveState(const ModelProfile &model, const QList<EcLightState> &lights)
{
    const QString path = stateFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{
                                  {"version"_L1, StateFormatVersion},
                                  {"written"_L1, QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                  {"lights"_L1, lightsToJson(model, lights)},
                              })
                    .toJson());
        f.commit();
    }
}

std::optional<RecordedState> loadState(const ModelProfile &model)
{
    QFile f(stateFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QString written = root.value("written"_L1).toString();
    if (root.value("version"_L1).toInt() != StateFormatVersion || written.isEmpty()) {
        return std::nullopt;
    }
    return RecordedState{written, lightsFromJson(model, root.value("lights"_L1).toObject()).values()};
}

HelperResult call(const ModelProfile &model, HelperCall::Kind kind, const QList<EcRequest> &requests)
{
    KAuthHelperCalls calls;
    const HelperResult r = runToEnd(calls, kind, requests);
    if (kind == HelperCall::Set && r.ok()) {
        saveState(model, r.lights);
    }
    return r;
}

QString layoutCode(int code)
{
    return u"0x%1"_s.arg(code, 2, 16, u'0');
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
                     missing.join(", "_L1),
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
