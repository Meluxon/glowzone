// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ite8297.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Glowzone::Ite8297
{

const QList<ModeInfo> &modes()
{
    // To add a mode: a value in the Mode enum, a row here (fields: see ModeInfo),
    // and the value in the fanModes of every model that offers it.
    // Each row: mode, id, name, uses colour, uses speed, follows the temperature.
    static const QList<ModeInfo> list = {
        {Mode::Off, "off", kli18nc("@item fan LED mode", "Off"), false, false},
        {Mode::Static, "static", kli18nc("@item fan LED mode", "Static"), true, false},
        {Mode::Sparkle, "sparkle", kli18nc("@item fan LED mode", "Sparkle"), true, true},
        {Mode::Breathing, "breathing", kli18nc("@item fan LED mode", "Breathing"), true, true},
        {Mode::Wave, "wave", kli18nc("@item fan LED mode", "Wave"), true, true},
        {Mode::Specter, "specter", kli18nc("@item fan LED mode", "Specter"), false, true},
        {Mode::Rainbow, "rainbow", kli18nc("@item fan LED mode", "Rainbow wave"), false, true},
        {Mode::Random, "random", kli18nc("@item fan LED mode", "Random"), false, true},
        {Mode::CpuTemperature, "cpu-temperature", kli18nc("@item fan LED mode", "CPU temperature"), false, false},
    };
    return list;
}

QList<ModeInfo> modesFor(const ModelProfile &model)
{
    QList<ModeInfo> list;
    for (const ModeInfo &m : modes()) {
        if (model.hasFanMode(quint8(m.mode))) {
            list << m;
        }
    }
    return list;
}

const ModeInfo *modeInfo(Mode mode)
{
    for (const ModeInfo &m : modes()) {
        if (m.mode == mode) {
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

std::optional<Rgb> parseRgb(const QString &text)
{
    QString t = text.trimmed();
    if (t.startsWith(u'#')) {
        t.remove(0, 1);
    }
    // Exactly six ASCII hex digits – no "0x", sign, spaces or other scripts'
    // digits, which toUInt() or QChar::isDigit() would accept.
    const bool hex = std::all_of(t.cbegin(), t.cend(), [](QChar c) {
        const char16_t u = c.unicode();
        return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F');
    });
    if (t.size() != 6 || !hex) {
        return std::nullopt;
    }
    return rgbFromValue(t.toUInt(nullptr, 16));
}

Rgb rgbFromValue(quint32 rrggbb)
{
    constexpr int RedShift = 16;
    constexpr int GreenShift = 8;
    return Rgb{quint8(rrggbb >> RedShift), quint8(rrggbb >> GreenShift), quint8(rrggbb)};
}

QString rgbToString(Rgb c)
{
    return u"#%1%2%3"_s
        .arg(c.r, 2, 16, u'0')
        .arg(c.g, 2, 16, u'0')
        .arg(c.b, 2, 16, u'0');
}

HeaderSetting resolve(HeaderSetting setting, Rgb temperatureColour)
{
    if (followsTemperature(setting.mode)) {
        setting.mode = Mode::Static;
        setting.speed = MinLevel;
        setting.colour = temperatureColour;
    }
    return setting;
}

HeaderSetting initialSetting(quint8 header)
{
    return {header, Mode::Off, MinLevel, 2, Rgb{0xff, 0xff, 0xff}};
}

// Group A: headers 0x10–0x13, commit 0x28 with mask 0x0F (bit n = header 0x10+n).
// Group B: headers 0x38–0x3A, commit 0x2D with mask 0x07 (bit n = header 0x38+n).
// Like Vantage, always commit with the full mask of the group.
std::optional<Group> groupOf(quint8 header)
{
    if (header >= 0x10 && header <= 0x13) {
        return Group::A;
    }
    if (header >= 0x38 && header <= 0x3A) {
        return Group::B;
    }
    return std::nullopt;
}

bool isKnownHeader(quint8 header)
{
    return groupOf(header).has_value();
}

static QByteArray emptyReport()
{
    QByteArray r(ReportSize, '\0');
    r[0] = char(ReportId);
    return r;
}

QByteArray settingsReport(HeaderSetting s)
{
    QByteArray r = emptyReport();
    r[1] = char(s.header);
    r[2] = char(s.mode);
    r[3] = char(s.speed);
    r[4] = char(s.brightness);
    r[5] = char(s.colour.r);
    r[6] = char(s.colour.g);
    r[7] = char(s.colour.b);
    return r;
}

QByteArray commitReport(Group g)
{
    QByteArray r = emptyReport();
    r[1] = char(g == Group::A ? 0x28 : 0x2D);
    r[2] = char(g == Group::A ? 0x0F : 0x07);
    return r;
}

QList<QByteArray> reportsFor(const QList<HeaderSetting> &settings)
{
    QList<QByteArray> out;
    bool a = false, b = false;
    for (const HeaderSetting &s : settings) {
        const auto g = groupOf(s.header);
        if (!g || followsTemperature(s.mode)) {
            continue; // never send to a header or a mode value the controller doesn't know
        }
        out.append(settingsReport(s));
        (*g == Group::A ? a : b) = true;
    }
    if (a) {
        out.append(commitReport(Group::A));
    }
    if (b) {
        out.append(commitReport(Group::B));
    }
    return out;
}

} // namespace Glowzone::Ite8297
