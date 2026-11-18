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
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace Glowzone
{

namespace Profiles
{

static constexpr int FormatVersion = 1;

QString directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/profiles"_L1;
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

// An int stored as a JSON number; nullopt for fractions, huge values and other types.
static std::optional<int> wholeNumber(const QJsonValue &v)
{
    const double d = v.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!(d >= std::numeric_limits<int>::min() && d <= std::numeric_limits<int>::max()) || d != std::trunc(d)) {
        return std::nullopt; // also NaN: not a number at all
    }
    return int(d);
}

static std::optional<Profile> fromJson(const ModelProfile &model, const QJsonObject &root)
{
    if (root.value("version"_L1).toInt() != FormatVersion
        || root.value("model"_L1).toString() != QLatin1String(model.productVersion)) {
        return std::nullopt;
    }
    Profile p;
    p.name = cleanName(root.value("name"_L1).toString());
    if (p.name.isEmpty()) {
        return std::nullopt;
    }

    // Only headers this model actually has.
    const FanState::Headers all = FanState::headersFromJson(root.value("fans"_L1).toObject(), model);
    for (const FanDef &fan : model.fans) {
        for (const LedDef &led : fan.leds) {
            if (all.contains(led.header)) {
                p.fans.insert(led.header, all.value(led.header));
            }
        }
    }

    const QJsonObject links = root.value("links"_L1).toObject();
    for (const FanDef &fan : model.fans) {
        const QJsonValue v = links.value(QLatin1String(fan.id));
        if (fan.isDouble() && v.isBool()) {
            p.links.insert(QLatin1String(fan.id), v.toBool());
        }
    }

    if (model.cpuTemperature) {
        // A file without a usable range gets the default one.
        const QJsonObject range = root.value("cpuTemperature"_L1).toObject();
        const auto coolest = wholeNumber(range.value("coolest"_L1));
        const auto hottest = wholeNumber(range.value("hottest"_L1));
        p.temperatureRange = coolest && hottest ? TemperatureRange{*coolest, *hottest} : model.cpuTemperature->defaultRange;
    }

    const QJsonObject lights = root.value("lights"_L1).toObject();
    for (const EcLightDef &light : model.ecLights) {
        const QJsonObject o = lights.value(QLatin1String(light.id)).toObject();
        const EcLights::ModeInfo *mode = EcLights::modeInfo(o.value("mode"_L1).toString());
        const int brightness = o.value("brightness"_L1).toInt();
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
                          QJsonObject{{"mode"_L1, QLatin1String(mode->id)}, {"brightness"_L1, s.brightness}});
        }
    }
    QJsonObject root{
        {"version"_L1, FormatVersion},
        {"name"_L1, p.name},
        {"model"_L1, QLatin1String(model.productVersion)},
        {"fans"_L1, FanState::headersToJson(p.fans)},
        {"links"_L1, links},
        {"lights"_L1, lights},
    };
    if (p.temperatureRange) {
        root.insert("cpuTemperature"_L1,
                    QJsonObject{{"coolest"_L1, p.temperatureRange->coolest}, {"hottest"_L1, p.temperatureRange->hottest}});
    }
    return root;
}

// "Profile 2" before "Profile 10"; letters compared case-insensitively and
// locale-aware. (QCollator's numeric mode needs ICU, which not every Qt has.)
static bool naturalLess(const QString &a, const QString &b)
{
    static const QRegularExpression chunks(u"\\d+|\\D+"_s);
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
    const QStringList files = dir.entryList({u"*.json"_s}, QDir::Files);
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
    static const QRegularExpression separators(u"[^a-z0-9]+"_s);
    static const QRegularExpression edges(u"^-+|-+$"_s);
    QString slug = name.toLower();
    slug.replace(separators, u"-"_s);
    slug.remove(edges);
    if (slug.isEmpty()) {
        slug = u"profile"_s;
    }
    const QDir dir(directory());
    QString path = dir.filePath(slug + ".json"_L1);
    for (int n = 2; QFile::exists(path); ++n) {
        path = dir.filePath(slug + u'-' + QString::number(n) + ".json"_L1);
    }
    return path;
}

static bool writeFile(const QString &path, const QJsonObject &root, QString *error)
{
    const QByteArray data = QJsonDocument(root).toJson();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = i18n("Could not save the profile to %1: %2", path, f.errorString());
        return false;
    }
    return true;
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
    return writeFile(existing ? existing->path : newFilePath(p.name), toJson(model, p), error);
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
    // Only the name changes: everything else in the file stays as it is,
    // including what this version doesn't read.
    QFile f(existing->path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = i18n("Could not read %1: %2", existing->path, f.errorString());
        return false;
    }
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    root.insert("name"_L1, name);
    if (clash) { // the same profile, only the case changes: rewrite it in place
        return writeFile(existing->path, root, error);
    }
    // Write the new file first, then remove the old one – never lose the profile.
    const QString path = newFilePath(name);
    if (!writeFile(path, root, error)) {
        return false;
    }
    if (!QFile::remove(existing->path)) {
        *error = i18n("Could not delete %1.", existing->path);
        QFile::remove(path); // undo: keep exactly one copy
        return false;
    }
    return true;
}

} // namespace Profiles
} // namespace Glowzone
