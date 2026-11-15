// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "cputemperature.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <numeric>

namespace Glowzone
{

static QString readLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(f.readLine(64)).trimmed();
}

TemperatureSensor::TemperatureSensor(const CpuTemperatureDef &def, const QString &hwmonRoot)
    : m_def(def)
    , m_root(hwmonRoot)
{
}

bool TemperatureSensor::find(QString *error)
{
    const QDir root(m_root);
    const QStringList devices = root.entryList({QStringLiteral("hwmon*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    bool driverFound = false;
    for (const QString &device : devices) {
        const QDir dir(root.filePath(device));
        if (readLine(dir.filePath(QStringLiteral("name"))) != QLatin1String(m_def.hwmonName)) {
            continue;
        }
        driverFound = true;
        const QStringList labels = dir.entryList({QStringLiteral("temp*_label")}, QDir::Files | QDir::System);
        for (const QString &label : labels) {
            if (readLine(dir.filePath(label)) == QLatin1String(m_def.sensorLabel)) {
                QString input = label;
                input.replace(QLatin1String("_label"), QLatin1String("_input"));
                m_input = dir.filePath(input);
                return true;
            }
        }
    }
    *error = driverFound ? i18n("The CPU sensor “%1” of %2 was not found.", QLatin1String(m_def.sensorLabel), QLatin1String(m_def.hwmonName))
                         : i18n("The CPU temperature sensor (%1) was not found. Is its kernel module loaded?", QLatin1String(m_def.hwmonName));
    return false;
}

std::optional<double> TemperatureSensor::read(QString *error)
{
    // A sensor found earlier that fails gets one new search at once: the hwmon
    // numbers change when a driver is loaded again.
    for (bool searched = false; !searched;) {
        searched = m_input.isEmpty();
        if (searched && !find(error)) {
            return std::nullopt;
        }
        bool ok = false;
        const int milli = readLine(m_input).toInt(&ok); // millidegrees Celsius
        // A plausible CPU temperature only: a broken sensor reads 0 or nonsense.
        if (ok && milli > 0 && milli <= 150000) {
            return milli / 1000.0;
        }
        *error = i18n("The CPU temperature could not be read from %1.", m_input);
        m_input.clear();
    }
    return std::nullopt;
}

TemperatureSmoother::TemperatureSmoother(const CpuTemperatureDef &def)
    : m_def(def)
{
}

double TemperatureSmoother::add(double reading, double secondsSinceLast)
{
    m_readings.append(reading);
    while (m_readings.size() > std::max(1, m_def.averageSamples)) {
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

namespace TemperatureColours
{

static int plainStep(const CpuTemperatureDef &def, double celsius)
{
    const int last = int(def.colours.size()) - 1;
    if (last <= 0 || celsius < def.firstStep) {
        return 0;
    }
    return std::min(last, int(std::floor((celsius - def.firstStep) / def.stepSize)) + 1);
}

// The temperature at which a step starts (step 0: none).
static double stepStart(const CpuTemperatureDef &def, int step)
{
    return def.firstStep + (step - 1) * def.stepSize;
}

int step(const CpuTemperatureDef &def, double celsius, std::optional<int> current)
{
    const int plain = plainStep(def, celsius);
    if (!current || plain >= *current) {
        return plain;
    }
    // Going down: only once clearly below the current step.
    if (celsius >= stepStart(def, *current) - def.hysteresis) {
        return *current;
    }
    return plainStep(def, celsius + def.hysteresis);
}

Ite8297::Rgb colour(const CpuTemperatureDef &def, int step)
{
    if (def.colours.isEmpty()) {
        return {};
    }
    const quint32 c = def.colours.at(std::clamp(step, 0, int(def.colours.size()) - 1));
    return {quint8(c >> 16), quint8(c >> 8), quint8(c)};
}

} // namespace TemperatureColours

// Unlike the monotonic clock behind QElapsedTimer, this one goes on during
// suspend, so a pause in the readings shows as one.
static qint64 bootTimeMs()
{
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// Well past the readings averaged: after a suspend or a sensor failure.
static constexpr qint64 LongPauseMs = 10000;

CpuTemperature::CpuTemperature(const CpuTemperatureDef &def, QObject *parent, const QString &hwmonRoot)
    : QObject(parent)
    , m_def(def)
    , m_sensor(def, hwmonRoot)
    , m_smoother(def)
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
    m_step.reset(); // the first reading always reports its colour
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
    return TemperatureColours::colour(m_def, m_step.value_or(0));
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
        seconds = double(now - m_lastReadingMs) / 1000.0;
    }
    m_lastReadingMs = now;
    m_temperature = m_smoother.add(*reading, seconds);
    const int step = TemperatureColours::step(m_def, *m_temperature, m_step);
    const bool changed = step != m_step;
    m_step = step;
    Q_EMIT updated();
    if (changed) {
        Q_EMIT colourChanged(colour());
    }
}

std::optional<double> cpuTemperatureNow(const CpuTemperatureDef &def, QString *error)
{
    TemperatureSensor sensor(def);
    const auto reading = sensor.read(error);
    return reading ? std::optional(*reading + def.offset) : std::nullopt;
}

} // namespace Glowzone
