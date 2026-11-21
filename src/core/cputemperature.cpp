// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "cputemperature.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ctime>
#include <numeric>

using namespace Qt::StringLiterals;

namespace Glowzone
{

// hwmon gives millidegrees Celsius.
static constexpr int MilliPerDegree = 1000;
// A plausible CPU temperature reading at most; a broken sensor reads 0 or nonsense.
static constexpr int MaxPlausibleMilli = 150 * MilliPerDegree;
// A critical temperature below this can't be a CPU's Tjmax: ignored.
static constexpr int MinPlausibleLimitMilli = 60 * MilliPerDegree;
// The eye sees brightness roughly as the light raised to 1/2.2 (the sRGB curve).
static constexpr double PerceivedGamma = 2.2;
// Longer than any line a sensor file holds.
static constexpr qint64 MaxLineLength = 64;
static constexpr qint64 MsPerSecond = 1000;
static constexpr qint64 NsPerMs = 1000000;

static QString readLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(f.readLine(MaxLineLength)).trimmed();
}

TemperatureSensor::TemperatureSensor(const CpuTemperatureDef &def, const QString &hwmonRoot)
    : m_def(def)
    , m_root(hwmonRoot)
{
}

bool TemperatureSensor::find(QString *error)
{
    const QDir root(m_root);
    const QStringList devices = root.entryList({u"hwmon*"_s}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    bool driverFound = false;
    for (const QString &device : devices) {
        const QDir dir(root.filePath(device));
        if (readLine(dir.filePath(u"name"_s)) != m_def.hwmonName) {
            continue;
        }
        driverFound = true;
        const QStringList labels = dir.entryList({u"temp*_label"_s}, QDir::Files | QDir::System);
        for (const QString &label : labels) {
            if (readLine(dir.filePath(label)) == m_def.sensorLabel) {
                QString input = label;
                input.replace("_label"_L1, "_input"_L1);
                m_input = dir.filePath(input);
                QString crit = label;
                crit.replace("_label"_L1, "_crit"_L1);
                bool ok = false;
                const int milli = readLine(dir.filePath(crit)).toInt(&ok);
                const bool plausible = ok && milli >= MinPlausibleLimitMilli && milli <= MaxPlausibleMilli;
                m_limit = plausible ? std::optional(milli / MilliPerDegree) : std::nullopt;
                return true;
            }
        }
    }
    *error = driverFound ? i18n("The CPU sensor “%1” of %2 was not found.", m_def.sensorLabel, m_def.hwmonName)
                         : i18n("The CPU temperature sensor (%1) was not found. Is its kernel module loaded?", m_def.hwmonName);
    return false;
}

// The value of a temperature input in °C, if it reads as a plausible one.
static std::optional<double> readCelsius(const QString &input)
{
    bool ok = false;
    const int milli = readLine(input).toInt(&ok);
    return ok && milli > 0 && milli <= MaxPlausibleMilli ? std::optional(milli / double(MilliPerDegree)) : std::nullopt;
}

std::optional<double> TemperatureSensor::read(QString *error)
{
    if (!m_input.isEmpty()) {
        if (const auto celsius = readCelsius(m_input)) {
            return celsius;
        }
        m_input.clear(); // the hwmon numbers change when a driver is loaded again: search anew
    }
    if (!find(error)) {
        return std::nullopt;
    }
    if (const auto celsius = readCelsius(m_input)) {
        return celsius;
    }
    *error = i18n("The CPU temperature could not be read from %1.", m_input);
    m_input.clear();
    return std::nullopt;
}

TemperatureSmoother::TemperatureSmoother(const CpuTemperatureDef &def)
    : m_def(def)
{
}

double TemperatureSmoother::add(double reading, double secondsSinceLast)
{
    m_readings.append(reading);
    while (m_readings.size() > m_def.averageSamples) {
        m_readings.removeFirst();
    }
    const double sum = std::accumulate(m_readings.cbegin(), m_readings.cend(), 0.0);
    const double target = sum / double(m_readings.size()) + m_def.offset;
    if (!m_value) {
        m_value = target;
    } else {
        const double limit = m_def.maxChangePerSecond * std::max(0.0, secondsSinceLast);
        m_value = *m_value + std::clamp(target - *m_value, -limit, limit);
    }
    return *m_value;
}

void TemperatureSmoother::reset()
{
    m_readings.clear();
    m_value.reset();
}

int minimumRangeWidth(const CpuTemperatureDef &def)
{
    return int(def.colours.size());
}

TemperatureRange usableRange(const CpuTemperatureDef &def, TemperatureRange range, int top)
{
    const int width = minimumRangeWidth(def);
    top = std::max(top, def.darkAt + width);
    const int hottest = std::clamp(range.hottest, def.darkAt + width, top);
    const int coolest = std::clamp(range.coolest, def.darkAt, hottest - width);
    return {coolest, hottest};
}

Ite8297::Rgb unknownTemperatureColour(const CpuTemperatureDef &def)
{
    return Ite8297::rgbFromValue(def.colours.value(0));
}

// At a perceived brightness (0–1). LEDs give light in proportion to the value,
// the eye doesn't: see PerceivedGamma.
static Ite8297::Rgb dimmed(Ite8297::Rgb c, double brightness)
{
    const double k = std::pow(std::clamp(brightness, 0.0, 1.0), PerceivedGamma);
    auto scale = [k](quint8 v) {
        return quint8(std::lround(v * k));
    };
    return {scale(c.r), scale(c.g), scale(c.b)};
}

// Part of the way (0–1) to white.
static Ite8297::Rgb whitened(Ite8297::Rgb c, double amount)
{
    const double a = std::clamp(amount, 0.0, 1.0);
    auto mix = [a](quint8 v) {
        return quint8(std::lround(v + (std::numeric_limits<quint8>::max() - v) * a));
    };
    return {mix(c.r), mix(c.g), mix(c.b)};
}

TemperatureScale::TemperatureScale(const CpuTemperatureDef &def, TemperatureRange range, int top)
    : m_hysteresis(def.hysteresis)
    , m_range(usableRange(def, range, top))
    , m_top(std::max(top, m_range.hottest))
{
    const auto add = [this](double start, Ite8297::Rgb colour) {
        if (!m_colours.isEmpty() && colour == m_colours.constLast()) {
            return; // a fade step that rounds to the same colour: no step
        }
        m_starts << start;
        m_colours << colour;
    };
    const Ite8297::Rgb first = Ite8297::rgbFromValue(def.colours.constFirst());
    const Ite8297::Rgb last = Ite8297::rgbFromValue(def.colours.constLast());
    const int coolest = m_range.coolest;
    const int hottest = m_range.hottest;

    // Below the range: from darkest at darkAt up to (almost) full at coolest.
    for (int t = def.darkAt; t < coolest; ++t) {
        add(t, dimmed(first, def.darkest + (1.0 - def.darkest) * double(t - def.darkAt) / double(coolest - def.darkAt)));
    }
    // The colours, spread evenly.
    const double width = double(hottest - coolest) / double(def.colours.size());
    for (int i = 0; i < def.colours.size(); ++i) {
        add(coolest + i * width, Ite8297::rgbFromValue(def.colours.at(i)));
    }
    // Above the range: the last colour up to hottest, white at the top.
    for (int t = hottest + 1; t <= m_top; ++t) {
        add(t, whitened(last, double(t - hottest) / double(m_top - hottest)));
    }
}

int TemperatureScale::plainStep(double celsius) const
{
    // The first step also covers everything below its start.
    const auto it = std::upper_bound(m_starts.cbegin() + 1, m_starts.cend(), celsius);
    return int(it - m_starts.cbegin()) - 1;
}

int TemperatureScale::step(double celsius, std::optional<int> current) const
{
    const int plain = plainStep(celsius);
    if (!current || *current < 0 || *current >= stepCount() || plain >= *current) {
        return plain;
    }
    // Going down: only once clearly below the current step.
    if (celsius >= m_starts.at(*current) - m_hysteresis) {
        return *current;
    }
    return plainStep(celsius + m_hysteresis);
}

Ite8297::Rgb TemperatureScale::colour(int step) const
{
    return m_colours.at(std::clamp(step, 0, stepCount() - 1));
}

// Unlike the monotonic clock behind QElapsedTimer, this one goes on during
// suspend, so a pause in the readings shows as one.
static qint64 bootTimeMs()
{
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return qint64(ts.tv_sec) * MsPerSecond + ts.tv_nsec / NsPerMs;
}

// Well past the readings averaged: after a suspend or a sensor failure.
static constexpr qint64 LongPauseMs = 10000;

CpuTemperature::CpuTemperature(const CpuTemperatureDef &def, QObject *parent, const QString &hwmonRoot)
    : QObject(parent)
    , m_def(def)
    , m_sensor(def, hwmonRoot)
    , m_smoother(def)
    , m_range(def.defaultRange)
    , m_scale(def, m_range, def.hottestLimit)
{
    m_timer.setInterval(def.intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &CpuTemperature::read);
}

void CpuTemperature::start()
{
    if (m_timer.isActive()) {
        return;
    }
    m_smoother.reset();
    m_temperature.reset();
    m_step.reset();
    m_reported.reset(); // the first reading always reports its colour
    m_lastReadingMs = -1;
    m_timer.start();
    read();
}

void CpuTemperature::stop()
{
    m_timer.stop();
}

Ite8297::Rgb CpuTemperature::colour() const
{
    return m_step ? m_scale.colour(*m_step) : unknownTemperatureColour(m_def);
}

void CpuTemperature::setRange(TemperatureRange range)
{
    m_range = range;
    TemperatureScale scale(m_def, range, m_scale.top());
    if (scale.range() == m_scale.range()) {
        return; // the same colours: a new scale would only lose the hysteresis of the current step
    }
    m_scale = std::move(scale);
    if (m_temperature) {
        m_step = m_scale.step(*m_temperature); // steps of another scale: no hysteresis to keep
        report();
    }
}

// colourChanged() while running, when the colour isn't the one reported last.
void CpuTemperature::report()
{
    const Ite8297::Rgb c = colour();
    if (m_timer.isActive() && c != m_reported) {
        m_reported = c;
        Q_EMIT colourChanged(c);
    }
}

void CpuTemperature::read()
{
    QString error;
    const auto reading = m_sensor.read(&error);
    if (!reading) {
        if (!m_failing) {
            m_failing = true;
            Q_EMIT failed(error);
        }
        Q_EMIT updated();
        return;
    }
    if (m_failing) {
        m_failing = false;
        Q_EMIT recovered();
    }
    const qint64 now = bootTimeMs();
    double seconds = 0;
    if (m_lastReadingMs >= 0) {
        if (now - m_lastReadingMs > LongPauseMs) {
            m_smoother.reset(); // the readings it holds are stale: start over
        }
        seconds = double(now - m_lastReadingMs) / double(MsPerSecond);
    }
    m_lastReadingMs = now;
    if (const int top = m_sensor.limit().value_or(m_def.hottestLimit); top != m_scale.top()) {
        m_scale = TemperatureScale(m_def, m_range, top); // the sensor's limit, once it is found
        m_step.reset();
        Q_EMIT scaleChanged();
    }
    m_temperature = m_smoother.add(*reading, seconds);
    m_step = m_scale.step(*m_temperature, m_step);
    Q_EMIT updated();
    report();
}

std::optional<CpuTemperatureReading> cpuTemperatureNow(const CpuTemperatureDef &def, QString *error)
{
    TemperatureSensor sensor(def);
    const auto reading = sensor.read(error);
    if (!reading) {
        return std::nullopt;
    }
    return CpuTemperatureReading{*reading + def.offset, sensor.limit().value_or(def.hottestLimit)};
}

} // namespace Glowzone
