// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles.h"
#include "eclights.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

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

std::optional<QString> checkedName(const QString &name, QString *error)
{
    const QString clean = cleanName(name);
    if (clean.isEmpty()) {
        *error = i18n("A profile name must be 1 to %1 characters long.", MaxNameLength);
        return std::nullopt;
    }
    return clean;
}

static QString notFound(const QString &name)
{
    return i18n("There is no profile called “%1”.", name);
}

struct Stored {
    Profile profile;
    QString path;
    QJsonObject root; // the whole file, also what this version doesn't read
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

    p.fans = FanState::headersFromJson(root.value("fans"_L1).toObject(), model);

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

    p.lights = EcLights::lightsFromJson(model, root.value("lights"_L1).toObject());
    return p;
}

static QJsonObject toJson(const ModelProfile &model, const Profile &p)
{
    QJsonObject links;
    for (auto it = p.links.cbegin(); it != p.links.cend(); ++it) {
        links.insert(it.key(), it.value());
    }
    QJsonObject root{
        {"version"_L1, FormatVersion},
        {"name"_L1, p.name},
        {"model"_L1, QLatin1String(model.productVersion)},
        {"fans"_L1, FanState::headersToJson(p.fans)},
        {"links"_L1, links},
        {"lights"_L1, EcLights::lightsToJson(model, p.lights.values())},
    };
    if (p.temperatureRange) {
        root.insert("cpuTemperature"_L1,
                    QJsonObject{{"coolest"_L1, p.temperatureRange->coolest}, {"hottest"_L1, p.temperatureRange->hottest}});
    }
    return root;
}

// Digit runs by value, however long: without leading zeros, the longer one is
// larger, otherwise the first differing digit decides.
static int compareNumbers(QStringView a, QStringView b)
{
    const auto significant = [](QStringView s) {
        while (s.size() > 1 && s.front().digitValue() == 0) {
            s = s.sliced(1);
        }
        return s;
    };
    a = significant(a);
    b = significant(b);
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (const int d = a.at(i).digitValue() - b.at(i).digitValue(); d != 0) {
            return d;
        }
    }
    return 0;
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
        const int c = ca.at(0).isDigit() && cb.at(0).isDigit() ? compareNumbers(ca, cb)
                                                               : QString::localeAwareCompare(ca.toLower(), cb.toLower());
        if (c != 0) {
            return c < 0;
        }
    }
    return !ia.hasNext() && ib.hasNext();
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
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        if (auto p = fromJson(model, root)) {
            out.append({*p, f.fileName(), std::move(root)});
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

std::optional<Profile> find(const ModelProfile &model, const QString &name, QString *error)
{
    if (const auto s = findStored(model, name)) {
        return s->profile;
    }
    if (error) {
        *error = notFound(name);
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

QString lockFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/glowzone-profiles.lock"_L1;
}

// Saving, renaming and deleting read the directory, choose a file and change
// it; the window and the command line may do that at the same moment. One at
// a time: QLockFile also notices a holder that died. Holders take
// milliseconds, so waiting longer means something is wrong.
class ChangeLock
{
public:
    explicit ChangeLock(QString *error)
    {
        if (!m_file.tryLock(Wait)) {
            *error = i18n("Another Glowzone window or command is changing the profiles. Try again in a moment.");
        }
    }
    explicit operator bool() const { return m_file.isLocked(); }

private:
    static constexpr std::chrono::milliseconds Wait = std::chrono::seconds(2);
    QLockFile m_file{lockFilePath()};
};

// What this version writes replaces what the file has – inside the objects it
// writes (fans, lights, ...) entry by entry. Everything else stays: the
// settings of LEDs and lights that aren't known now, and whatever another
// version added.
static QJsonObject merged(QJsonObject stored, const QJsonObject &ours)
{
    for (auto it = ours.constBegin(); it != ours.constEnd(); ++it) {
        const QJsonValue old = stored.value(it.key());
        if (!it->isObject() || !old.isObject()) {
            stored.insert(it.key(), *it);
            continue;
        }
        QJsonObject entries = old.toObject();
        const QJsonObject mine = it->toObject();
        for (auto e = mine.constBegin(); e != mine.constEnd(); ++e) {
            entries.insert(e.key(), *e);
        }
        stored.insert(it.key(), entries);
    }
    return stored;
}

bool save(const ModelProfile &model, const Profile &profile, QString *error)
{
    const ChangeLock lock(error);
    if (!lock) {
        return false;
    }
    Profile p = profile;
    const auto name = checkedName(profile.name, error);
    if (!name) {
        return false;
    }
    p.name = *name;
    if (!QDir().mkpath(directory())) {
        *error = i18n("Could not create the directory %1.", directory());
        return false;
    }
    const QJsonObject ours = toJson(model, p);
    if (const auto existing = findStored(model, p.name)) {
        return writeFile(existing->path, merged(existing->root, ours), error);
    }
    return writeFile(newFilePath(p.name), ours, error);
}

std::optional<QString> remove(const ModelProfile &model, const QString &name, QString *error)
{
    const ChangeLock lock(error);
    if (!lock) {
        return std::nullopt;
    }
    const auto existing = findStored(model, name);
    if (!existing) {
        *error = notFound(name);
        return std::nullopt;
    }
    if (!QFile::remove(existing->path)) {
        *error = i18n("Could not delete %1.", existing->path);
        return std::nullopt;
    }
    return existing->profile.name;
}

std::optional<QString> rename(const ModelProfile &model, const QString &oldName, const QString &newName, QString *error)
{
    const ChangeLock lock(error);
    if (!lock) {
        return std::nullopt;
    }
    const auto existing = findStored(model, oldName);
    if (!existing) {
        *error = notFound(oldName);
        return std::nullopt;
    }
    const auto checked = checkedName(newName, error);
    if (!checked) {
        return std::nullopt;
    }
    const QString &name = *checked;
    const auto clash = findStored(model, name);
    if (clash && clash->path != existing->path) {
        *error = i18n("A profile called “%1” already exists.", clash->profile.name);
        return std::nullopt;
    }
    // Only the name changes: everything else in the file stays as it is,
    // including what this version doesn't read.
    QJsonObject root = existing->root;
    root.insert("name"_L1, name);
    if (clash) { // the same profile, only the case changes: rewrite it in place
        return writeFile(existing->path, root, error) ? std::optional(existing->profile.name) : std::nullopt;
    }
    // Write the new file first, then remove the old one – never lose the profile.
    const QString path = newFilePath(name);
    if (!writeFile(path, root, error)) {
        return std::nullopt;
    }
    if (!QFile::remove(existing->path)) {
        *error = i18n("Could not delete %1.", existing->path);
        QFile::remove(path); // undo: keep exactly one copy
        return std::nullopt;
    }
    return existing->profile.name;
}

} // namespace Profiles
} // namespace Glowzone
