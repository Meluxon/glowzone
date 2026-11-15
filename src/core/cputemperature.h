// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ite8297.h"
#include "model/modelprofile.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

namespace Glowzone
{

// The CPU sensor named in the model profile, found through hwmon
// (/sys/class/hwmon/hwmon*/name and tempN_label). Readable by any user.
class TemperatureSensor
{
public:
    explicit TemperatureSensor(const CpuTemperatureDef &def, const QString &hwmonRoot = QStringLiteral("/sys/class/hwmon"));

    // °C, or nullopt with a translated error. The sensor is looked up again
    // after a failure (hwmon numbers can change when a driver reloads).
    std::optional<double> read(QString *error);

private:
    bool find(QString *error);

    const CpuTemperatureDef &m_def;
    QString m_root;
    QString m_input; // .../tempN_input
};

// The EC's smoothing, imitated: the average of the last readings plus the
// offset, moving at most maxChangePerSecond towards it. The first reading is
// taken as it is.
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

namespace TemperatureColours
{
// The colour step for a temperature. Going down needs the temperature to drop
// the hysteresis below the current step's start, so a reading wobbling on a
// boundary doesn't make the fans flicker.
int step(const CpuTemperatureDef &def, double celsius, std::optional<int> current = std::nullopt);
Ite8297::Rgb colour(const CpuTemperatureDef &def, int step);
} // namespace TemperatureColours

// Reads the sensor periodically while running and reports the colour for the
// smoothed temperature. Only runs while something needs it.
class CpuTemperature : public QObject
{
    Q_OBJECT

public:
    CpuTemperature(const CpuTemperatureDef &def, QObject *parent, const QString &hwmonRoot = QStringLiteral("/sys/class/hwmon"));

    void start(); // reads right away
    void stop();
    bool isRunning() const { return m_timer.isActive(); }
    void setInterval(int ms) { m_timer.setInterval(ms); } // for tests

    // The smoothed temperature (nullopt before the first good reading).
    std::optional<double> temperature() const { return m_temperature; }
    // The colour for it; the coolest colour while nothing could be read.
    Ite8297::Rgb colour() const;

Q_SIGNALS:
    void updated();                          // after every reading (good or not)
    void colourChanged(Ite8297::Rgb colour); // the step changed
    void failed(const QString &error);       // a reading failed (after it worked, or the first time)
    void recovered();                        // reading works again

private:
    void read();

    const CpuTemperatureDef &m_def;
    TemperatureSensor m_sensor;
    TemperatureSmoother m_smoother;
    QTimer m_timer;
    QElapsedTimer m_sinceLast;
    std::optional<double> m_temperature;
    std::optional<int> m_step;
    bool m_failing = false;
};

// One unsmoothed reading, for the command line: the colour for the CPU
// temperature right now. nullopt with a translated error if it can't be read.
std::optional<Ite8297::Rgb> cpuTemperatureColourNow(const CpuTemperatureDef &def, double *celsius, QString *error);

} // namespace Glowzone
