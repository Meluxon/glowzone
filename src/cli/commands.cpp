// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "commands.h"

#include "core/cputemperature.h"
#include "core/eclights.h"
#include "core/fancontroller.h"
#include "core/fanstate.h"
#include "core/instance.h"
#include "core/messages.h"
#include "core/profiles.h"
#include "core/settings.h"
#include "model/modelprofile.h"

#include <KLocalizedString>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Glowzone::Cli
{

using namespace Ite8297;

namespace
{

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

const QCommandLineOption &colourOption()
{
    static const QCommandLineOption o({u"c"_s, u"colour"_s, u"color"_s},
                                      i18n("Fan colour as RRGGBB, for modes that use a colour."),
                                      u"RRGGBB"_s);
    return o;
}

const QCommandLineOption &speedOption()
{
    static const QCommandLineOption o({u"s"_s, u"speed"_s},
                                      i18n("Fan effect speed (levels: see below)."),
                                      u"level"_s);
    return o;
}

const QCommandLineOption &brightnessOption()
{
    static const QCommandLineOption o({u"b"_s, u"brightness"_s},
                                      i18n("Brightness of fans or case lights (levels: see below)."),
                                      u"level"_s);
    return o;
}

const QCommandLineOption &ledOption()
{
    static const QCommandLineOption o({u"l"_s, u"led"_s},
                                      i18n("Set only one LED of a double fan (positions: see below)."),
                                      u"position"_s);
    return o;
}

const QCommandLineOption &rangeOption()
{
    static const QCommandLineOption o({u"r"_s, u"range"_s},
                                      i18n("With cpu-temperature: the temperatures the colours are spread over, e.g. 35-80. Shared by all fans in that mode."),
                                      u"coolest-hottest"_s);
    return o;
}

const QCommandLineOption &purgeOption()
{
    static const QCommandLineOption o(u"purge-user-data"_s,
                                      i18n("Delete Glowzone's settings, profiles and saved state for this user, then exit."));
    return o;
}

// "cpu, rear, front" – the ids of fans, LEDs, lights or modes, for help and errors.
template<typename List>
QString idList(const List &list)
{
    QStringList ids;
    for (const auto &item : list) {
        ids << QLatin1String(item.id);
    }
    return ids.join(", "_L1);
}

int usageError(const QString &message)
{
    err() << message << Qt::endl << i18n("Run “glowzone --help” for usage.") << Qt::endl;
    return ExitUsage;
}

std::optional<int> levelOption(const QCommandLineParser &p, const QCommandLineOption &o, int min, int max, int *exitCode)
{
    if (!p.isSet(o)) {
        return std::nullopt;
    }
    bool ok = false;
    const int v = p.value(o).toInt(&ok);
    if (!ok || v < min || v > max) {
        *exitCode = usageError(i18n("“%1” must be a number from %2 to %3.", o.names().constLast(), min, max));
        return std::nullopt;
    }
    return v;
}

QString describe(HeaderSetting s)
{
    const ModeInfo *m = modeInfo(s.mode);
    QStringList parts{m->name.toString()};
    if (s.mode == Mode::Off) {
        return parts.constFirst();
    }
    if (m->usesColour) {
        parts << rgbToString(s.colour);
    }
    if (m->usesSpeed) {
        parts << i18nc("@info fan effect speed", "speed %1", s.speed);
    }
    parts << i18nc("@info", "brightness %1", s.brightness);
    return parts.join(", "_L1);
}

QString describe(EcLightState s)
{
    const EcLights::ModeInfo *m = EcLights::modeInfo(s.mode);
    const QString mode = m ? m->name.toString() : i18nc("@info unknown mode value", "mode %1", s.mode);
    return i18nc("@info light mode, brightness", "%1, brightness %2", mode, s.brightness);
}

template<typename List>
bool anyFollowsTemperature(const List &settings)
{
    return std::any_of(settings.cbegin(), settings.cend(), [](HeaderSetting s) {
        return followsTemperature(s.mode);
    });
}

// LEDs in the CPU-temperature mode get the colour for the temperature right
// now, with the given range or else the stored one. Updating it later is the
// window's (or the tray's) job; says so if that isn't running.
QList<HeaderSetting> resolveForNow(const ModelProfile &model, const QList<HeaderSetting> &settings, std::optional<TemperatureRange> range = std::nullopt)
{
    if (!model.cpuTemperature || !anyFollowsTemperature(settings)) {
        return settings;
    }
    const CpuTemperatureDef &def = *model.cpuTemperature;
    QString error;
    const auto now = cpuTemperatureNow(def, &error);
    if (!now) {
        err() << error << Qt::endl;
    }
    const Rgb colour = now ? TemperatureScale(def, range.value_or(Settings::temperatureRange(def)), now->top).colourAt(now->celsius)
                           : unknownTemperatureColour(def);
    QList<HeaderSetting> resolved;
    for (const HeaderSetting &s : settings) {
        resolved << resolve(s, colour);
    }
    if (!Instance::isRunning()) {
        err() << i18n("Note: LEDs following the CPU temperature only change colour while Glowzone is running (the window, or in the background). Start it with “glowzone” or “glowzone --background”.")
              << Qt::endl;
    }
    return resolved;
}

// The highest temperature a range may reach: the sensor's limit, or the
// model's while the sensor can't be read.
int temperatureTop(const CpuTemperatureDef &def)
{
    QString ignored;
    const auto now = cpuTemperatureNow(def, &ignored);
    return now ? now->top : def.hottestLimit;
}

// "35-80" (or with an en dash), within the limits.
std::optional<TemperatureRange> parseRange(const CpuTemperatureDef &def, const QString &text, int *exitCode)
{
    const int top = temperatureTop(def);
    static constexpr QChar EnDash(0x2013);
    const QStringList parts = QString(text).replace(EnDash, u'-').split(u'-');
    bool okCoolest = false;
    bool okHottest = false;
    const TemperatureRange range{parts.value(0).trimmed().toInt(&okCoolest), parts.value(1).trimmed().toInt(&okHottest)};
    if (parts.size() != 2 || !okCoolest || !okHottest || usableRange(def, range, top) != range) {
        *exitCode = usageError(i18n("“%1” is not a usable range. Give two whole numbers of °C from %2 to %3, at least %4 apart, e.g. 35-80.",
                                    text, def.darkAt, top, minimumRangeWidth(def)));
        return std::nullopt;
    }
    return range;
}

int nameWidth(const ModelProfile &model)
{
    int w = 0;
    for (const FanDef &f : model.fans) {
        w = std::max(w, int(f.name.toString().size()));
    }
    for (const EcLightDef &l : model.ecLights) {
        w = std::max(w, int(l.name.toString().size()));
    }
    return w + 2;
}

// One fan's settings, or each LED's when they differ.
QString describeFan(const FanDef &fan, const FanState::Headers &headers)
{
    QStringList perLed;
    for (const LedDef &led : fan.leds) {
        if (!headers.contains(led.header)) {
            return i18n("unknown (not set by Glowzone yet)");
        }
        perLed << describe(headers.value(led.header));
    }
    if (perLed.count(perLed.constFirst()) == perLed.size()) {
        return perLed.constFirst();
    }
    QStringList named;
    for (int i = 0; i < perLed.size(); ++i) {
        named << i18nc("@info fan position: description", "%1: %2", fan.leds.at(i).name.toString(), perLed.at(i));
    }
    return named.join("; "_L1);
}

void printLights(const ModelProfile &model, const QList<EcLightState> &lights, int width)
{
    for (const EcLightState &s : lights) {
        const EcLightDef *def = model.ecLightByIndex(s.index);
        const QString name = def ? def->name.toString() : QString::number(s.index);
        out() << "  "_L1 << name.leftJustified(width) << describe(s) << Qt::endl;
    }
}

// --- commands -------------------------------------------------------------

int cmdStatus(const ModelProfile &model)
{
    const int width = nameWidth(model);
    out() << i18n("Computer: %1", model.displayName()) << Qt::endl << Qt::endl;

    out() << i18n("Fans (as last set by Glowzone – the controller can’t be read back):") << Qt::endl;
    const FanState::Headers saved = FanState::load(model);
    for (const FanDef &fan : model.fans) {
        out() << "  "_L1 << fan.name.toString().leftJustified(width) << describeFan(fan, saved) << Qt::endl;
    }
    if (model.cpuTemperature && anyFollowsTemperature(saved)) {
        QString error;
        const CpuTemperatureDef &def = *model.cpuTemperature;
        if (const auto now = cpuTemperatureNow(def, &error)) {
            const TemperatureRange range = usableRange(def, Settings::temperatureRange(def), now->top);
            out() << "  "_L1
                  << i18n("CPU temperature: %1 °C, colours over %2–%3 °C (%4)",
                          QString::number(now->celsius, 'f', 0),
                          range.coolest,
                          range.hottest,
                          Instance::isRunning() ? i18n("the colours follow it") : i18n("not followed – Glowzone isn’t running"))
                  << Qt::endl;
        } else {
            out() << "  "_L1 << error << Qt::endl;
        }
    }

    out() << Qt::endl << i18n("Lights:") << Qt::endl;
    QString error;
    const auto lights = EcLights::status(&error);
    if (!lights) {
        out() << "  "_L1 << i18n("not available: %1", error) << Qt::endl;
        return ExitFailed;
    }
    printLights(model, *lights, width);

    if (const auto layout = EcLights::layout(&error)) {
        const QString warning = EcLights::layoutWarning(model, *layout);
        out() << Qt::endl
              << i18n("Fan detection (BIOS): %1",
                      warning.isEmpty() ? i18nc("@info", "all fans detected (layout %1)", EcLights::layoutCode(*layout)) : warning)
              << Qt::endl;
    }
    return ExitOk;
}

int cmdFan(const ModelProfile &model, const QStringList &args, const QCommandLineParser &p)
{
    if (args.size() != 2) {
        return usageError(i18n("Usage: glowzone fan <fan> <mode> [-c RRGGBB] [-s speed] [-b brightness] [-l position] [-r coolest-hottest]"));
    }

    QList<const FanDef *> fans;
    if (args.at(0) == "all"_L1) {
        for (const FanDef &f : model.fans) {
            fans << &f;
        }
    } else if (const FanDef *f = model.fan(args.at(0))) {
        fans << f;
    } else {
        return usageError(i18n("Unknown fan “%1”. Choose one of: %2", args.at(0), "all, "_L1 + idList(model.fans)));
    }

    const ModeInfo *mode = modeInfo(args.at(1));
    if (!mode || !model.hasFanMode(quint8(mode->mode))) {
        return usageError(i18n("Unknown fan mode “%1”. Choose one of: %2", args.at(1), idList(modesFor(model))));
    }

    int exitCode = ExitOk;
    const auto speed = levelOption(p, speedOption(), MinLevel, model.fanLevels, &exitCode);
    const auto brightness = levelOption(p, brightnessOption(), MinLevel, model.fanLevels, &exitCode);
    if (exitCode != ExitOk) {
        return exitCode;
    }
    std::optional<Rgb> colour;
    if (p.isSet(colourOption())) {
        colour = parseRgb(p.value(colourOption()));
        if (!colour) {
            return usageError(i18n("“%1” is not a colour. Use six hex digits, e.g. ff3d9c.", p.value(colourOption())));
        }
    }

    std::optional<TemperatureRange> range;
    if (p.isSet(rangeOption())) {
        if (!followsTemperature(mode->mode) || !model.cpuTemperature) {
            return usageError(i18n("--range only works with the mode %1.", QLatin1String(modeInfo(Mode::CpuTemperature)->id)));
        }
        range = parseRange(*model.cpuTemperature, p.value(rangeOption()), &exitCode);
        if (!range) {
            return exitCode;
        }
    }

    // --led: one LED of one double fan
    const LedDef *onlyLed = nullptr;
    if (p.isSet(ledOption())) {
        if (fans.size() != 1 || !fans.constFirst()->isDouble()) {
            return usageError(i18n("--led only works with a single fan that has more than one LED."));
        }
        onlyLed = fans.constFirst()->led(p.value(ledOption()));
        if (!onlyLed) {
            return usageError(i18n("Unknown position “%1”. Choose one of: %2", p.value(ledOption()), idList(fans.constFirst()->leds)));
        }
    }

    // Unspecified values keep what Glowzone last sent to that header. The lock
    // is taken first, so a running Glowzone can't change them before they're
    // sent and saved.
    const FanState::Lock lock;
    FanState::Headers state = FanState::load(model);
    QList<HeaderSetting> settings;
    for (const FanDef *fan : std::as_const(fans)) {
        for (const LedDef &led : fan->leds) {
            if (onlyLed && &led != onlyLed) {
                continue;
            }
            const quint8 h = led.header;
            HeaderSetting s = state.value(h, initialSetting(h));
            s.header = h;
            s.mode = mode->mode;
            if (speed) {
                s.speed = quint8(*speed);
            }
            if (brightness) {
                s.brightness = quint8(*brightness);
            }
            if (colour) {
                s.colour = *colour;
            }
            settings << s;
            state.insert(h, s);
        }
    }

    FanController controller(model);
    QString error;
    if (!controller.apply(resolveForNow(model, settings, range), &error)) {
        err() << error << Qt::endl;
        return ExitFailed;
    }
    if (range) {
        Settings::setTemperatureRange(*range); // only once the fans have it
    }
    if (!FanState::merge(lock, settings, &error)) {
        err() << i18n("The fans were changed, but the state could not be saved: %1", error) << Qt::endl;
        return ExitFailed;
    }
    if (onlyLed) {
        // One LED set on its own: the pair is now set separately (as in the window).
        Settings::setLinked(QLatin1String(fans.constFirst()->id), false);
    }

    if (onlyLed) {
        const QString label = i18nc("@item fan (position)", "%1 (%2)", fans.constFirst()->name.toString(), onlyLed->name.toString());
        out() << "  "_L1 << label << "  "_L1 << describe(settings.constFirst()) << Qt::endl;
        return ExitOk;
    }
    const int width = nameWidth(model);
    for (const FanDef *fan : std::as_const(fans)) {
        out() << "  "_L1 << fan->name.toString().leftJustified(width) << describeFan(*fan, state) << Qt::endl;
    }
    return ExitOk;
}

int cmdLight(const ModelProfile &model, const QStringList &args, const QCommandLineParser &p)
{
    if (args.isEmpty() || args.size() > 2) {
        return usageError(i18n("Usage: glowzone light <light> [mode] [-b brightness]"));
    }

    QList<const EcLightDef *> lights;
    if (args.at(0) == "all"_L1) {
        for (const EcLightDef &l : model.ecLights) {
            lights << &l;
        }
    } else if (const EcLightDef *l = model.ecLight(args.at(0))) {
        lights << l;
    } else {
        return usageError(i18n("Unknown light “%1”. Choose one of: %2", args.at(0), "all, "_L1 + idList(model.ecLights)));
    }

    quint8 modeValue = 0; // 0 = leave unchanged
    if (args.size() == 2) {
        const EcLights::ModeInfo *mode = EcLights::modeInfo(args.at(1));
        if (!mode || !model.hasEcLightMode(mode->value)) {
            return usageError(i18n("Unknown light mode “%1”. Choose one of: %2", args.at(1), idList(EcLights::modesFor(model))));
        }
        modeValue = mode->value;
    }

    int exitCode = ExitOk;
    const auto brightness = levelOption(p, brightnessOption(), EcLights::MinBrightness, model.ecBrightnessLevels, &exitCode);
    if (exitCode != ExitOk) {
        return exitCode;
    }
    if (modeValue == 0 && !brightness) {
        return usageError(i18n("Give a mode, a brightness (-b), or both."));
    }

    QList<EcRequest> requests;
    for (const EcLightDef *l : std::as_const(lights)) {
        requests << EcRequest{l->index, modeValue, quint8(brightness.value_or(0))};
    }

    QString error;
    const auto result = EcLights::set(requests, &error);
    if (!result) {
        err() << error << Qt::endl;
        return ExitFailed;
    }
    printLights(model, *result, nameWidth(model));
    return ExitOk;
}

int cmdReset(const ModelProfile &model)
{
    int exitCode = ExitOk;
    QString error;

    QList<HeaderSetting> settings;
    for (const FanDef &fan : model.fans) {
        for (const quint8 h : fan.headers()) {
            settings << initialSetting(h);
        }
    }
    FanController controller(model);
    {
        const FanState::Lock lock;
        if (controller.apply(settings, &error)) {
            out() << i18n("Fans: all off.") << Qt::endl;
            if (!FanState::merge(lock, settings, &error)) {
                err() << error << Qt::endl;
                exitCode = ExitFailed;
            }
        } else {
            err() << i18n("Fans: %1", error) << Qt::endl;
            exitCode = ExitFailed;
        }
    }

    QList<EcRequest> requests;
    for (const EcLightDef &l : model.ecLights) {
        requests << EcRequest{l.index, EcLights::ModeOff, model.ecDefaultBrightness};
    }
    if (EcLights::set(requests, &error)) {
        out() << i18n("Case lights: off.") << Qt::endl;
    } else {
        err() << i18n("Case lights: %1", error) << Qt::endl;
        exitCode = ExitFailed;
    }
    return exitCode;
}

// --- profiles -----------------------------------------------------------------

int profileUsage()
{
    return usageError(i18n("Usage: glowzone profile list | apply <name> | save <name> | rename <old> <new> | delete <name>"));
}

int cmdProfileList(const ModelProfile &model)
{
    const QList<Profile> profiles = Profiles::loadAll(model);
    if (profiles.isEmpty()) {
        out() << i18n("No profiles saved yet. Save one with “glowzone profile save <name>”.") << Qt::endl;
        return ExitOk;
    }
    const QString current = Settings::currentProfile();
    for (const Profile &p : profiles) {
        const bool isCurrent = p.name.compare(current, Qt::CaseInsensitive) == 0;
        out() << (isCurrent ? "* "_L1 : "  "_L1) << p.name << Qt::endl;
    }
    return ExitOk;
}

int cmdProfileApply(const ModelProfile &model, const QString &name)
{
    const auto profile = Profiles::find(model, name);
    if (!profile) {
        err() << i18n("There is no profile called “%1”.", name) << Qt::endl;
        return ExitFailed;
    }
    int exitCode = ExitOk;
    QString error;

    if (!profile->fans.isEmpty()) {
        FanController controller(model);
        const QList<HeaderSetting> settings = profile->fans.values();
        const FanState::Lock lock;
        if (!controller.apply(resolveForNow(model, settings, profile->temperatureRange), &error) || !FanState::merge(lock, settings, &error)) {
            err() << i18n("Fans: %1", error) << Qt::endl;
            exitCode = ExitFailed;
        }
    }
    for (auto it = profile->links.cbegin(); it != profile->links.cend(); ++it) {
        Settings::setLinked(it.key(), it.value());
    }
    if (profile->temperatureRange && exitCode == ExitOk) {
        Settings::setTemperatureRange(*profile->temperatureRange);
    }
    // Recorded first, and put back if anything fails.
    const QString previous = Settings::currentProfile();
    if (exitCode == ExitOk) {
        Settings::setCurrentProfile(profile->name);
    }
    if (!profile->lights.isEmpty()) {
        QList<EcRequest> requests;
        for (const EcLightState &l : profile->lights) {
            requests << EcRequest{l.index, l.mode, l.brightness};
        }
        if (!EcLights::set(requests, &error)) {
            err() << i18n("Case lights: %1", error) << Qt::endl;
            exitCode = ExitFailed;
        }
    }
    if (exitCode != ExitOk) {
        Settings::setCurrentProfile(previous); // only partly applied
        return exitCode;
    }
    out() << i18n("Applied profile “%1”.", profile->name) << Qt::endl;
    return exitCode;
}

int cmdProfileSave(const ModelProfile &model, const QString &rawName)
{
    Profile p;
    p.name = Profiles::cleanName(rawName);
    if (p.name.isEmpty()) {
        return usageError(i18n("A profile name must be 1 to %1 characters long.", Profiles::MaxNameLength));
    }

    // The current state as Glowzone knows it.
    const FanState::Headers saved = FanState::load(model);
    for (const FanDef &fan : model.fans) {
        for (const LedDef &led : fan.leds) {
            if (saved.contains(led.header)) {
                p.fans.insert(led.header, saved.value(led.header));
            }
        }
        if (fan.isDouble()) {
            p.links.insert(QLatin1String(fan.id), Settings::linked(QLatin1String(fan.id)).value_or(FanState::defaultLinked(fan, saved)));
        }
    }
    if (model.cpuTemperature) {
        p.temperatureRange = Settings::temperatureRange(*model.cpuTemperature);
    }
    if (p.fans.isEmpty()) {
        err() << i18n("Note: Glowzone hasn’t set any fan yet, so the profile contains no fan settings.") << Qt::endl;
    }
    if (!model.ecLights.isEmpty()) {
        QString error;
        if (const auto lights = EcLights::status(&error)) {
            for (const EcLightState &l : *lights) {
                p.lights.insert(l.index, l);
            }
        } else {
            err() << i18n("Note: the case lights could not be read and are not part of the profile: %1", error) << Qt::endl;
        }
    }

    QString error;
    if (!Profiles::save(model, p, &error)) {
        err() << error << Qt::endl;
        return ExitFailed;
    }
    Settings::setCurrentProfile(p.name);
    out() << i18n("Saved profile “%1”.", p.name) << Qt::endl;
    return ExitOk;
}

int cmdProfileDelete(const ModelProfile &model, const QString &name)
{
    const auto profile = Profiles::find(model, name);
    QString error;
    if (!Profiles::remove(model, name, &error)) {
        err() << error << Qt::endl;
        return ExitFailed;
    }
    if (profile && Settings::currentProfile().compare(profile->name, Qt::CaseInsensitive) == 0) {
        Settings::setCurrentProfile({});
    }
    out() << i18n("Deleted profile “%1”.", profile ? profile->name : name) << Qt::endl;
    return ExitOk;
}

int cmdProfileRename(const ModelProfile &model, const QString &oldName, const QString &newName)
{
    const auto old = Profiles::find(model, oldName); // the name as stored, not as typed
    QString error;
    if (!old || !Profiles::rename(model, oldName, newName, &error)) {
        err() << (old ? error : i18n("There is no profile called “%1”.", oldName)) << Qt::endl;
        return ExitFailed;
    }
    const QString clean = Profiles::cleanName(newName);
    if (Settings::currentProfile().compare(old->name, Qt::CaseInsensitive) == 0) {
        Settings::setCurrentProfile(clean);
    }
    out() << i18n("Renamed profile “%1” to “%2”.", old->name, clean) << Qt::endl;
    return ExitOk;
}

int cmdProfile(const ModelProfile &model, const QStringList &args)
{
    if (args.isEmpty()) {
        return profileUsage();
    }
    const QString &sub = args.constFirst();
    if (sub == "list"_L1 && args.size() == 1) {
        return cmdProfileList(model);
    }
    if (sub == "rename"_L1 && args.size() == 3) {
        return cmdProfileRename(model, args.at(1), args.at(2));
    }
    if (args.size() != 2) {
        return profileUsage();
    }
    if (sub == "apply"_L1) {
        return cmdProfileApply(model, args.at(1));
    }
    if (sub == "save"_L1) {
        return cmdProfileSave(model, args.at(1));
    }
    if (sub == "delete"_L1) {
        return cmdProfileDelete(model, args.at(1));
    }
    return profileUsage();
}

// Glowzone keeps user data in two directories – settings and profiles, and the
// last fan state – plus the autostart entry if "Start at Login" is on.
int purgeUserData()
{
    if (Instance::isRunning()) {
        // It would write its settings and fan state again.
        err() << i18n("Glowzone is running (the window or in the system tray). Quit it first.") << Qt::endl;
        return ExitFailed;
    }
    const QStringList dirs = {
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
        QStandardPaths::writableLocation(QStandardPaths::StateLocation),
    };

    int removed = 0;
    int exitCode = ExitOk;
    for (const QString &path : dirs) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) { // a dangling link still counts
            continue;
        }
        bool ok = false;
        if (info.isSymLink()) {
            // Remove the link itself – never what it points to.
            ok = QFile::remove(path);
        } else if (info.fileName() != "glowzone"_L1) {
            // Never delete a directory that isn't Glowzone's own.
            err() << i18n("Skipping %1: not a Glowzone directory.", path) << Qt::endl;
            exitCode = ExitFailed;
            continue;
        } else {
            ok = QDir(path).removeRecursively();
        }
        if (ok) {
            out() << i18n("Removed %1", path) << Qt::endl;
            ++removed;
        } else {
            err() << i18n("Could not remove %1", path) << Qt::endl;
            exitCode = ExitFailed;
        }
    }
    // "Start at Login": the autostart entry is ours by its file name.
    const QString autostart = Settings::autostartPath();
    const QFileInfo autostartInfo(autostart);
    if (autostartInfo.exists() || autostartInfo.isSymLink()) {
        if (QFile::remove(autostart)) {
            out() << i18n("Removed %1", autostart) << Qt::endl;
            ++removed;
        } else {
            err() << i18n("Could not remove %1", autostart) << Qt::endl;
            exitCode = ExitFailed;
        }
    }
    if (removed == 0 && exitCode == ExitOk) {
        out() << i18n("No Glowzone user data found.") << Qt::endl;
    }
    return exitCode;
}

} // namespace

// Help is built from the model profile and the mode tables, so nothing about
// a particular PC is hard-coded here.
static QString helpText()
{
    using Row = std::pair<QString, QString>;
    auto table = [](const QList<Row> &rows) {
        int width = 0;
        for (const Row &r : rows) {
            width = std::max(width, int(r.first.size()));
        }
        QStringList lines;
        for (const Row &r : rows) {
            lines << "  "_L1 + r.first.leftJustified(width + 3) + r.second;
        }
        return lines.join(u'\n');
    };

    QString text = i18n("Lighting control for supported Lenovo desktops: fan LEDs and case lights.") + "\n\n"_L1
        + i18n("Commands:") + u'\n'
        + table({
            {u"status"_s, i18n("Show the current lighting")},
            {u"fan <fan> <mode>"_s, i18n("Set fan LEDs")},
            {u"light <light> [mode]"_s, i18n("Set a case light")},
            {u"reset"_s, i18n("Turn all lighting off")},
            {u"profile list"_s, i18n("List saved profiles (* = applied last)")},
            {u"profile apply <name>"_s, i18n("Apply a profile")},
            {u"profile save <name>"_s, i18n("Save the current lighting as a profile")},
            {u"profile rename <old> <new>"_s, i18n("Rename a profile")},
            {u"profile delete <name>"_s, i18n("Delete a profile")},
        })
        + u'\n' + i18n("Run glowzone without arguments to open the window, or with --background to start in the system tray.")
        + "\n\n"_L1;

    QList<Row> values;
    if (const ModelProfile *model = matchModel(DmiInfo::readSystem())) {
        text += i18n("This computer (%1):", model->displayName()) + u'\n';
        values << Row{i18n("Fans:"), "all, "_L1 + idList(model->fans)};
        QStringList positions;
        for (const FanDef &f : model->fans) {
            if (f.isDouble()) {
                positions << QLatin1String(f.id) + ": "_L1 + idList(f.leds);
            }
        }
        if (!positions.isEmpty()) {
            values << Row{i18n("LEDs (--led):"), positions.join("; "_L1)};
        }
        const QString range = u"%1–%2"_s;
        values << Row{i18n("Fan modes:"), idList(modesFor(*model))};
        values << Row{i18n("Fan speed, brightness:"), range.arg(MinLevel).arg(model->fanLevels)};
        if (const auto &t = model->cpuTemperature) {
            values << Row{i18n("Temperature range (--range):"),
                          i18n("%1 to %2 °C, at least %3 apart (default %4-%5)", t->darkAt, temperatureTop(*t), minimumRangeWidth(*t),
                               t->defaultRange.coolest, t->defaultRange.hottest)};
        }
        if (!model->ecLights.isEmpty()) {
            values << Row{i18n("Lights:"), "all, "_L1 + idList(model->ecLights)};
            values << Row{i18n("Light modes:"), idList(EcLights::modesFor(*model))};
            values << Row{i18n("Light brightness:"), range.arg(EcLights::MinBrightness).arg(model->ecBrightnessLevels)};
        }
    } else {
        QStringList supported;
        for (const ModelProfile &m : supportedModels()) {
            supported << m.displayName();
        }
        text += i18n("This computer is not supported. Supported models: %1", supported.join(", "_L1)) + u'\n';
    }
    return text + table(values);
}

void addOptions(QCommandLineParser &parser, const QStringList &arguments)
{
    parser.addOptions({colourOption(), speedOption(), brightnessOption(), ledOption(), rangeOption(), purgeOption()});
    parser.addPositionalArgument(u"command"_s, i18n("status, fan, light, reset or profile"), u"<command>"_s);
    parser.addPositionalArgument(u"arguments"_s, i18n("Arguments of the command"), u"[arguments...]"_s);
    // The help text reads the hardware: only when it is shown.
    parser.parse(arguments);
    if (parser.isSet(u"help"_s) || parser.isSet(u"help-all"_s) || parser.positionalArguments().isEmpty()) {
        parser.setApplicationDescription(helpText());
    }
}

int run(QCommandLineParser &parser)
{
    if (parser.isSet(purgeOption())) {
        return purgeUserData();
    }

    QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(ExitOk); // doesn't return (main() starts the window when there are no arguments)
    }
    const QString command = args.takeFirst();
    static const QStringList commands{u"status"_s,
                                      u"fan"_s,
                                      u"light"_s,
                                      u"reset"_s,
                                      u"profile"_s};
    if (!commands.contains(command)) {
        return usageError(i18n("Unknown command “%1”.", command));
    }

    // Model lock: no profile, no lighting control. There is no override.
    const DmiInfo dmi = DmiInfo::readSystem();
    const ModelProfile *model = matchModel(dmi);
    if (!model) {
        err() << unsupportedModelMessage(dmi) << Qt::endl;
        return ExitUnsupported;
    }

    if (command == "status"_L1) {
        return args.isEmpty() ? cmdStatus(*model) : usageError(i18n("“status” takes no arguments."));
    }
    if (command == "fan"_L1) {
        return cmdFan(*model, args, parser);
    }
    if (command == "light"_L1) {
        return cmdLight(*model, args, parser);
    }
    if (command == "profile"_L1) {
        return cmdProfile(*model, args);
    }
    return args.isEmpty() ? cmdReset(*model) : usageError(i18n("“reset” takes no arguments."));
}

} // namespace Glowzone::Cli
