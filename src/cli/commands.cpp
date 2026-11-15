// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "commands.h"

#include "core/eclights.h"
#include "core/fancontroller.h"
#include "core/fanstate.h"
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
    static const QCommandLineOption o({QStringLiteral("c"), QStringLiteral("colour"), QStringLiteral("color")},
                                      i18n("Fan colour as RRGGBB, for modes that use a colour."),
                                      QStringLiteral("RRGGBB"));
    return o;
}

const QCommandLineOption &speedOption()
{
    static const QCommandLineOption o({QStringLiteral("s"), QStringLiteral("speed")},
                                      i18n("Fan effect speed (levels: see below)."),
                                      QStringLiteral("level"));
    return o;
}

const QCommandLineOption &brightnessOption()
{
    static const QCommandLineOption o({QStringLiteral("b"), QStringLiteral("brightness")},
                                      i18n("Brightness of fans or case lights (levels: see below)."),
                                      QStringLiteral("level"));
    return o;
}

const QCommandLineOption &ledOption()
{
    static const QCommandLineOption o({QStringLiteral("l"), QStringLiteral("led")},
                                      i18n("Set only one LED of a double fan (positions: see below)."),
                                      QStringLiteral("position"));
    return o;
}

const QCommandLineOption &purgeOption()
{
    static const QCommandLineOption o(QStringLiteral("purge-user-data"),
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
    return ids.join(QLatin1String(", "));
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
    return parts.join(QLatin1String(", "));
}

QString describe(EcLightState s)
{
    const EcLights::ModeInfo *m = EcLights::modeInfo(s.mode);
    const QString mode = m ? m->name.toString() : i18nc("@info unknown mode value", "mode %1", s.mode);
    return i18nc("@info light mode, brightness", "%1, brightness %2", mode, s.brightness);
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

void printLights(const ModelProfile &model, const QList<EcLightState> &lights, int width)
{
    for (const EcLightState &s : lights) {
        const EcLightDef *def = model.ecLightByIndex(s.index);
        const QString name = def ? def->name.toString() : QString::number(s.index);
        out() << QLatin1String("  ") << name.leftJustified(width) << describe(s) << Qt::endl;
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
        QStringList perLed;
        bool known = true;
        for (const LedDef &led : fan.leds) {
            if (!saved.contains(led.header)) {
                known = false;
                break;
            }
            perLed << describe(saved.value(led.header));
        }
        QString text;
        if (!known) {
            text = i18n("unknown (not set by Glowzone yet)");
        } else if (perLed.size() > 1 && perLed.count(perLed.constFirst()) != perLed.size()) {
            QStringList named;
            for (int i = 0; i < perLed.size(); ++i) {
                named << i18nc("@info fan position: description", "%1: %2", fan.leds.at(i).name.toString(), perLed.at(i));
            }
            text = named.join(QLatin1String("; "));
        } else {
            text = perLed.constFirst();
        }
        out() << QLatin1String("  ") << fan.name.toString().leftJustified(width) << text << Qt::endl;
    }

    out() << Qt::endl << i18n("Lights:") << Qt::endl;
    QString error;
    const auto lights = EcLights::status(&error);
    if (!lights) {
        out() << QLatin1String("  ") << i18n("not available: %1", error) << Qt::endl;
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
        return usageError(i18n("Usage: glowzone fan <fan> <mode> [-c RRGGBB] [-s speed] [-b brightness] [-l position]"));
    }

    QList<const FanDef *> fans;
    if (args.at(0) == QLatin1String("all")) {
        for (const FanDef &f : model.fans) {
            fans << &f;
        }
    } else if (const FanDef *f = model.fan(args.at(0))) {
        fans << f;
    } else {
        return usageError(i18n("Unknown fan “%1”. Choose one of: %2", args.at(0), QLatin1String("all, ") + idList(model.fans)));
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

    // Unspecified values keep what Glowzone last sent to that header.
    const FanState::Headers saved = FanState::load(model);
    QList<HeaderSetting> settings;
    QStringList labels; // one per setting, for the summary
    for (const FanDef *fan : std::as_const(fans)) {
        for (const LedDef &led : fan->leds) {
            if (onlyLed && &led != onlyLed) {
                continue;
            }
            const quint8 h = led.header;
            HeaderSetting s = saved.value(h, initialSetting(h));
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
            labels << (onlyLed ? i18nc("@item fan (position)", "%1 (%2)", fan->name.toString(), led.name.toString()) : fan->name.toString());
            if (!onlyLed && fan->isDouble() && led.header != fan->leds.constLast().header) {
                labels.last().clear(); // a linked pair is reported once, on its last LED
            }
        }
    }

    FanController controller(model);
    QString error;
    if (!controller.apply(settings, &error)) {
        err() << error << Qt::endl;
        return ExitFailed;
    }
    if (!FanState::merge(settings, &error)) {
        err() << i18n("The fans were changed, but the state could not be saved: %1", error) << Qt::endl;
        return ExitFailed;
    }

    int width = nameWidth(model);
    for (const QString &label : std::as_const(labels)) {
        width = std::max(width, int(label.size()) + 2);
    }
    for (int i = 0; i < settings.size(); ++i) {
        if (!labels.at(i).isEmpty()) {
            out() << QLatin1String("  ") << labels.at(i).leftJustified(width) << describe(settings.at(i)) << Qt::endl;
        }
    }
    return ExitOk;
}

int cmdLight(const ModelProfile &model, const QStringList &args, const QCommandLineParser &p)
{
    if (args.isEmpty() || args.size() > 2) {
        return usageError(i18n("Usage: glowzone light <light> [mode] [-b brightness]"));
    }

    QList<const EcLightDef *> lights;
    if (args.at(0) == QLatin1String("all")) {
        for (const EcLightDef &l : model.ecLights) {
            lights << &l;
        }
    } else if (const EcLightDef *l = model.ecLight(args.at(0))) {
        lights << l;
    } else {
        return usageError(i18n("Unknown light “%1”. Choose one of: %2", args.at(0), QLatin1String("all, ") + idList(model.ecLights)));
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
    if (controller.apply(settings, &error)) {
        out() << i18n("Fans: all off.") << Qt::endl;
        if (!FanState::merge(settings, &error)) {
            err() << error << Qt::endl;
            exitCode = ExitFailed;
        }
    } else {
        err() << i18n("Fans: %1", error) << Qt::endl;
        exitCode = ExitFailed;
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
        out() << (isCurrent ? QLatin1String("* ") : QLatin1String("  ")) << p.name << Qt::endl;
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
        if (!controller.apply(settings, &error) || !FanState::merge(settings, &error)) {
            err() << i18n("Fans: %1", error) << Qt::endl;
            exitCode = ExitFailed;
        }
    }
    for (auto it = profile->links.cbegin(); it != profile->links.cend(); ++it) {
        Settings::setLinked(it.key(), it.value());
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
    Settings::setCurrentProfile(profile->name);
    if (exitCode == ExitOk) {
        out() << i18n("Applied profile “%1”.", profile->name) << Qt::endl;
    }
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
    QString error;
    if (!Profiles::rename(model, oldName, newName, &error)) {
        err() << error << Qt::endl;
        return ExitFailed;
    }
    const QString clean = Profiles::cleanName(newName);
    if (Settings::currentProfile().compare(oldName, Qt::CaseInsensitive) == 0) {
        Settings::setCurrentProfile(clean);
    }
    out() << i18n("Renamed profile “%1” to “%2”.", oldName, clean) << Qt::endl;
    return ExitOk;
}

int cmdProfile(const ModelProfile &model, const QStringList &args)
{
    if (args.isEmpty()) {
        return profileUsage();
    }
    const QString &sub = args.constFirst();
    if (sub == QLatin1String("list") && args.size() == 1) {
        return cmdProfileList(model);
    }
    if (sub == QLatin1String("rename") && args.size() == 3) {
        return cmdProfileRename(model, args.at(1), args.at(2));
    }
    if (args.size() != 2) {
        return profileUsage();
    }
    if (sub == QLatin1String("apply")) {
        return cmdProfileApply(model, args.at(1));
    }
    if (sub == QLatin1String("save")) {
        return cmdProfileSave(model, args.at(1));
    }
    if (sub == QLatin1String("delete")) {
        return cmdProfileDelete(model, args.at(1));
    }
    return profileUsage();
}

// Glowzone keeps user data in two directories: settings and profiles, and the
// last fan state.
int purgeUserData()
{
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
        } else if (info.fileName() != QLatin1String("glowzone")) {
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
            lines << QLatin1String("  ") + r.first.leftJustified(width + 3) + r.second;
        }
        return lines.join(QLatin1Char('\n'));
    };

    QString text = i18n("Lighting control for supported Lenovo desktops: fan LEDs and case lights.") + QLatin1String("\n\n")
        + i18n("Commands:") + QLatin1Char('\n')
        + table({
            {QStringLiteral("status"), i18n("Show the current lighting")},
            {QStringLiteral("fan <fan> <mode>"), i18n("Set fan LEDs")},
            {QStringLiteral("light <light> [mode]"), i18n("Set a case light")},
            {QStringLiteral("reset"), i18n("Turn all lighting off")},
            {QStringLiteral("profile list"), i18n("List saved profiles (* = applied last)")},
            {QStringLiteral("profile apply <name>"), i18n("Apply a profile")},
            {QStringLiteral("profile save <name>"), i18n("Save the current lighting as a profile")},
            {QStringLiteral("profile rename <old> <new>"), i18n("Rename a profile")},
            {QStringLiteral("profile delete <name>"), i18n("Delete a profile")},
        })
        + QLatin1Char('\n') + i18n("Run glowzone without arguments to open the window.") + QLatin1String("\n\n");

    QList<Row> values;
    if (const ModelProfile *model = matchModel(DmiInfo::readSystem())) {
        text += i18n("This computer (%1):", model->displayName()) + QLatin1Char('\n');
        values << Row{i18n("Fans:"), QLatin1String("all, ") + idList(model->fans)};
        QStringList positions;
        for (const FanDef &f : model->fans) {
            if (f.isDouble()) {
                positions << QLatin1String(f.id) + QLatin1String(": ") + idList(f.leds);
            }
        }
        if (!positions.isEmpty()) {
            values << Row{i18n("LEDs (--led):"), positions.join(QLatin1String("; "))};
        }
        const QString range = QStringLiteral("%1–%2");
        values << Row{i18n("Fan modes:"), idList(modesFor(*model))};
        values << Row{i18n("Fan speed, brightness:"), range.arg(MinLevel).arg(model->fanLevels)};
        if (!model->ecLights.isEmpty()) {
            values << Row{i18n("Lights:"), QLatin1String("all, ") + idList(model->ecLights)};
            values << Row{i18n("Light modes:"), idList(EcLights::modesFor(*model))};
            values << Row{i18n("Light brightness:"), range.arg(EcLights::MinBrightness).arg(model->ecBrightnessLevels)};
        }
    } else {
        QStringList supported;
        for (const ModelProfile &m : supportedModels()) {
            supported << m.displayName();
        }
        text += i18n("This computer is not supported. Supported models: %1", supported.join(QLatin1String(", "))) + QLatin1Char('\n');
    }
    return text + table(values);
}

void addOptions(QCommandLineParser &parser)
{
    parser.setApplicationDescription(helpText());
    parser.addOptions({colourOption(), speedOption(), brightnessOption(), ledOption(), purgeOption()});
    parser.addPositionalArgument(QStringLiteral("command"), i18n("status, fan, light, reset or profile"), QStringLiteral("<command>"));
    parser.addPositionalArgument(QStringLiteral("arguments"), i18n("Arguments of the command"), QStringLiteral("[arguments...]"));
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
    static const QStringList commands{QStringLiteral("status"),
                                      QStringLiteral("fan"),
                                      QStringLiteral("light"),
                                      QStringLiteral("reset"),
                                      QStringLiteral("profile")};
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

    if (command == QLatin1String("status")) {
        return args.isEmpty() ? cmdStatus(*model) : usageError(i18n("“status” takes no arguments."));
    }
    if (command == QLatin1String("fan")) {
        return cmdFan(*model, args, parser);
    }
    if (command == QLatin1String("light")) {
        return cmdLight(*model, args, parser);
    }
    if (command == QLatin1String("profile")) {
        return cmdProfile(*model, args);
    }
    return args.isEmpty() ? cmdReset(*model) : usageError(i18n("“reset” takes no arguments."));
}

} // namespace Glowzone::Cli
