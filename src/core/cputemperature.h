// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ite8297.h"
#include "model/modelprofile.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

namespace Glowzone
{

inline constexpr const char *HwmonRoot = "/sys/class/hwmon"; // tests pass a fake tree

// The CPU sensor named in the model profile, found through hwmon
// (/sys/class/hwmon/hwmon*/name and tempN_label). Readable by any user.
class TemperatureSensor
{
public:
    explicit TemperatureSensor(const CpuTemperatureDef &def, const QString &hwmonRoot = QLatin1String(HwmonRoot));

    // °C, or nullopt with a translated error. The sensor is looked up again
    // after a failure (hwmon numbers can change when a driver reloads).
    std::optional<double> read(QString *error);
    // The sensor's critical temperature (tempN_crit: the CPU's Tjmax), once
    // found; nullopt if it gives none.
    std::optional<int> limit() const { return m_limit; }

private:
    bool find(QString *error);

    const CpuTemperatureDef &m_def;
    QString m_root;
    QString m_input; // .../tempN_input
    std::optional<int> m_limit;
};

// The EC's smoothing, imitated: the average of the last readings plus the
// offset, moving at most maxChangePerSecond towards it. The first reading
// counts in full, without a ramp.
class TemperatureSmoother
{
public:
    explicit TemperatureSmoother(const CpuTemperatureDef &def);

    double add(double reading, double secondsSinceLast);
    void reset();

private:
    const CpuTemperatureDef &m_def;
    QList<double> m_readings;
    std::optional<double> m_value;
};

// The fewest °C a range may span: one per colour.
int minimumRangeWidth(const CpuTemperatureDef &def);
// The range as it can be used: between darkAt and the top, and at least
// minimumRangeWidth() wide. Moves hottest first, then coolest.
TemperatureRange usableRange(const CpuTemperatureDef &def, TemperatureRange range, int top);

// The colour while the temperature is unknown: the first colour.
Ite8297::Rgb unknownTemperatureColour(const CpuTemperatureDef &def);

// The colour for every temperature, for one range. Steps, coolest first:
// - below the range, one per °C: the first colour fading out, darkest at darkAt
//   (perceived brightness, so the fade looks even)
// - the colour list, spread evenly over the range
// - above the range, one per °C: the last colour fading to white at the top
//   (the CPU's critical temperature)
class TemperatureScale
{
public:
    TemperatureScale(const CpuTemperatureDef &def, TemperatureRange range, int top);

    TemperatureRange range() const { return m_range; } // usable, see usableRange()
    int top() const { return m_top; }
    int stepCount() const { return int(m_colours.size()); }

    // Going down needs the temperature to drop the hysteresis below the current
    // step's start, so a reading wobbling on a boundary doesn't make the fans
    // flicker.
    int step(double celsius, std::optional<int> current = std::nullopt) const;
    Ite8297::Rgb colour(int step) const;
    Ite8297::Rgb colourAt(double celsius) const { return colour(step(celsius)); }

private:
    int plainStep(double celsius) const;

    double m_hysteresis;
    TemperatureRange m_range;
    int m_top;
    QList<double> m_starts; // where each step starts (the first: everything below)
    QList<Ite8297::Rgb> m_colours;
};

// Reads the sensor periodically while running and reports the colour for the
// smoothed temperature. Only runs while something needs it.
class CpuTemperature : public QObject
{
    Q_OBJECT

public:
    CpuTemperature(const CpuTemperatureDef &def, QObject *parent, const QString &hwmonRoot = QLatin1String(HwmonRoot));

    void start(); // reads right away
    void stop();
    bool isRunning() const { return m_timer.isActive(); }
    void setInterval(int ms) { m_timer.setInterval(ms); } // for tests

    // The smoothed temperature (nullopt before the first good reading).
    std::optional<double> temperature() const { return m_temperature; }
    // The colour for it; unknownTemperatureColour() while nothing could be read.
    Ite8297::Rgb colour() const;

    // The range the colours are spread over; a change takes effect at once
    // (colourChanged() if the colour is now another). The same range again
    // changes nothing.
    void setRange(TemperatureRange range);
    const TemperatureScale &scale() const { return m_scale; }

Q_SIGNALS:
    void updated();                                    // after every reading (good or not)
    void colourChanged(Glowzone::Ite8297::Rgb colour); // the colour changed
    void failed(const QString &error);                 // a reading failed (after it worked, or the first time)
    void recovered();                                  // reading works again

private:
    void read();
    void report();

    const CpuTemperatureDef &m_def;
    TemperatureSensor m_sensor;
    TemperatureSmoother m_smoother;
    TemperatureRange m_range; // as asked for; the scale holds the usable one
    TemperatureScale m_scale;
    QTimer m_timer;
    qint64 m_lastReadingMs = -1; // boot time of the last good reading
    std::optional<double> m_temperature;
    std::optional<int> m_step;
    std::optional<Ite8297::Rgb> m_reported; // the colour colourChanged() gave last
    bool m_failing = false;
};

struct CpuTemperatureReading {
    double celsius; // unsmoothed, plus the offset
    int top;        // the sensor's limit, or the model's if it gives none
};
// One reading, for the command line. nullopt with a translated error if it
// can't be read.
std::optional<CpuTemperatureReading> cpuTemperatureNow(const CpuTemperatureDef &def, QString *error);

} // namespace Glowzone
