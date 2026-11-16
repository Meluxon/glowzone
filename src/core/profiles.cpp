// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles.h"
#include "eclights.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace Glowzone
{

namespace Profiles
{

static constexpr int FormatVersion = 1;

QString directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1String("/profiles");
}

QString cleanName(const QString &name)
{
    const QString n = name.simplified();
    return (n.isEmpty() || n.size() > MaxNameLength) ? QString() : n;
}

struct Stored {
    Profile profile;
    QString path;
};

static std::optional<Profile> fromJson(const ModelProfile &model, const QJsonObject &root)
{
    if (root.value(QLatin1String("version")).toInt() != FormatVersion
        || root.value(QLatin1String("model")).toString() != QLatin1String(model.productVersion)) {
        return std::nullopt;
    }
    Profile p;
    p.name = cleanName(root.value(QLatin1String("name")).toString());
    if (p.name.isEmpty()) {
        return std::nullopt;
    }

    // Only headers this model actually has.
    const FanState::Headers all = FanState::headersFromJson(root.value(QLatin1String("fans")).toObject(), model);
    for (const FanDef &fan : model.fans) {
        for (const LedDef &led : fan.leds) {
            if (all.contains(led.header)) {
                p.fans.insert(led.header, all.value(led.header));
            }
        }
    }

    const QJsonObject links = root.value(QLatin1String("links")).toObject();
    for (const FanDef &fan : model.fans) {
        const QJsonValue v = links.value(QLatin1String(fan.id));
        if (fan.isDouble() && v.isBool()) {
            p.links.insert(QLatin1String(fan.id), v.toBool());
        }
    }

    if (model.cpuTemperature) {
        // A file without a range was made with the default one.
        const QJsonObject range = root.value(QLatin1String("cpuTemperature")).toObject();
        const QJsonValue coolest = range.value(QLatin1String("coolest"));
        const QJsonValue hottest = range.value(QLatin1String("hottest"));
        p.temperatureRange = coolest.isDouble() && hottest.isDouble() ? TemperatureRange{coolest.toInt(), hottest.toInt()}
                                                                      : model.cpuTemperature->defaultRange;
    }

    const QJsonObject lights = root.value(QLatin1String("lights")).toObject();
    for (const EcLightDef &light : model.ecLights) {
        const QJsonObject o = lights.value(QLatin1String(light.id)).toObject();
        const EcLights::ModeInfo *mode = EcLights::modeInfo(o.value(QLatin1String("mode")).toString());
        const int brightness = o.value(QLatin1String("brightness")).toInt();
        if (mode && model.hasEcLightMode(mode->value) && brightness >= EcLights::MinBrightness && brightness <= model.ecBrightnessLevels) {
            p.lights.insert(light.index, {light.index, mode->value, quint8(brightness)});
        }
    }
    return p;
}

static QJsonObject toJson(const ModelProfile &model, const Profile &p)
{
    QJsonObject links;
    for (auto it = p.links.cbegin(); it != p.links.cend(); ++it) {
        links.insert(it.key(), it.value());
    }
    QJsonObject lights;
    for (const EcLightState &s : p.lights) {
        const EcLightDef *def = model.ecLightByIndex(s.index);
        const EcLights::ModeInfo *mode = EcLights::modeInfo(s.mode);
        if (def && mode) {
            lights.insert(QLatin1String(def->id),
                          QJsonObject{{QLatin1String("mode"), QLatin1String(mode->id)}, {QLatin1String("brightness"), s.brightness}});
        }
    }
    QJsonObject root{
        {QLatin1String("version"), FormatVersion},
        {QLatin1String("name"), p.name},
        {QLatin1String("model"), QLatin1String(model.productVersion)},
        {QLatin1String("fans"), FanState::headersToJson(p.fans)},
        {QLatin1String("links"), links},
        {QLatin1String("lights"), lights},
    };
    if (p.temperatureRange) {
        root.insert(QLatin1String("cpuTemperature"),
                    QJsonObject{{QLatin1String("coolest"), p.temperatureRange->coolest}, {QLatin1String("hottest"), p.temperatureRange->hottest}});
    }
    return root;
}

// "Profile 2" before "Profile 10"; letters compared case-insensitively and
// locale-aware. (QCollator's numeric mode needs ICU, which not every Qt has.)
static bool naturalLess(const QString &a, const QString &b)
{
    static const QRegularExpression chunks(QStringLiteral("\\d+|\\D+"));
    auto ia = chunks.globalMatch(a);
    auto ib = chunks.globalMatch(b);
    while (ia.hasNext() && ib.hasNext()) {
        const QString ca = ia.next().captured();
        const QString cb = ib.next().captured();
        const bool da = ca.at(0).isDigit();
        const bool db = cb.at(0).isDigit();
        if (da && db) {
            const qulonglong na = ca.toULongLong();
            const qulonglong nb = cb.toULongLong();
            if (na != nb) {
                return na < nb;
            }
        } else {
            const int c = QString::localeAwareCompare(ca.toLower(), cb.toLower());
            if (c != 0) {
                return c < 0;
            }
        }
    }
    return ia.hasNext() ? false : ib.hasNext();
}

static QList<Stored> loadStored(const ModelProfile &model)
{
    QList<Stored> out;
    const QDir dir(directory());
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (auto p = fromJson(model, QJsonDocument::fromJson(f.readAll()).object())) {
            out.append({*p, f.fileName()});
        }
    }
    std::sort(out.begin(), out.end(), [](const Stored &a, const Stored &b) {
        return naturalLess(a.profile.name, b.profile.name);
    });
    return out;
}

static std::optional<Stored> findStored(const ModelProfile &model, const QString &name)
{
    const QString clean = cleanName(name);
    for (const Stored &s : loadStored(model)) {
        if (s.profile.name.compare(clean, Qt::CaseInsensitive) == 0) {
            return s;
        }
    }
    return std::nullopt;
}

QList<Profile> loadAll(const ModelProfile &model)
{
    QList<Profile> out;
    for (const Stored &s : loadStored(model)) {
        out << s.profile;
    }
    return out;
}

std::optional<Profile> find(const ModelProfile &model, const QString &name)
{
    if (const auto s = findStored(model, name)) {
        return s->profile;
    }
    return std::nullopt;
}

// "Night (blue)" -> "night-blue.json"; unique within the directory.
static QString newFilePath(const QString &name)
{
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression edges(QStringLiteral("^-+|-+$"));
    QString slug = name.toLower();
    slug.replace(separators, QStringLiteral("-"));
    slug.remove(edges);
    if (slug.isEmpty()) {
        slug = QStringLiteral("profile");
    }
    const QDir dir(directory());
    QString path = dir.filePath(slug + QLatin1String(".json"));
    for (int n = 2; QFile::exists(path); ++n) {
        path = dir.filePath(slug + QLatin1Char('-') + QString::number(n) + QLatin1String(".json"));
    }
    return path;
}

bool save(const ModelProfile &model, const Profile &profile, QString *error)
{
    Profile p = profile;
    p.name = cleanName(profile.name);
    if (p.name.isEmpty()) {
        *error = i18n("A profile name must be 1 to %1 characters long.", MaxNameLength);
        return false;
    }
    if (!QDir().mkpath(directory())) {
        *error = i18n("Could not create the directory %1.", directory());
        return false;
    }
    const auto existing = findStored(model, p.name);
    const QString path = existing ? existing->path : newFilePath(p.name);

    const QByteArray data = QJsonDocument(toJson(model, p)).toJson();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = i18n("Could not save the profile to %1: %2", path, f.errorString());
        return false;
    }
    return true;
}

bool remove(const ModelProfile &model, const QString &name, QString *error)
{
    const auto existing = findStored(model, name);
    if (!existing) {
        *error = i18n("There is no profile called “%1”.", name);
        return false;
    }
    if (!QFile::remove(existing->path)) {
        *error = i18n("Could not delete %1.", existing->path);
        return false;
    }
    return true;
}

bool rename(const ModelProfile &model, const QString &oldName, const QString &newName, QString *error)
{
    const auto existing = findStored(model, oldName);
    if (!existing) {
        *error = i18n("There is no profile called “%1”.", oldName);
        return false;
    }
    const QString name = cleanName(newName);
    if (name.isEmpty()) {
        *error = i18n("A profile name must be 1 to %1 characters long.", MaxNameLength);
        return false;
    }
    const auto clash = findStored(model, name);
    if (clash && clash->path != existing->path) {
        *error = i18n("A profile called “%1” already exists.", clash->profile.name);
        return false;
    }
    Profile p = existing->profile;
    p.name = name;
    if (clash) {
        // Same profile, only the case changes: rewrite it in place.
        return save(model, p, error);
    }
    // Write the new file first, then remove the old one – never lose the profile.
    if (!save(model, p, error)) {
        return false;
    }
    if (!QFile::remove(existing->path)) {
        *error = i18n("Could not delete %1.", existing->path);
        QString ignored;
        remove(model, name, &ignored); // undo: keep exactly one copy
        return false;
    }
    return true;
}

} // namespace Profiles
} // namespace Glowzone
