// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The fan queue and the LEDs following the CPU temperature, against a fake
// hidapi (defined here, so it takes the place of the real library) and a fake
// hwmon tree: what reaches the controller, and when.

#include "core/appliers.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <hidapi.h>

using namespace Glowzone;
using namespace Ite8297;

// --- fake hidapi: one ITE 8297; every feature report sent is recorded ----------

static QList<QByteArray> s_reports;
static int s_failures; // the next ones fail
static int s_device;

extern "C" {
int hid_init(void)
{
    return 0;
}
int hid_exit(void)
{
    return 0;
}
hid_device_info *hid_enumerate(unsigned short, unsigned short)
{
    auto *info = static_cast<hid_device_info *>(calloc(1, sizeof(hid_device_info)));
    info->path = strdup("/dev/hidraw-test");
    info->vendor_id = 0x17ef;
    info->product_id = 0xc955;
    info->usage_page = UsagePage;
    info->usage = Usage;
    return info;
}
void hid_free_enumeration(hid_device_info *info)
{
    while (info) {
        hid_device_info *next = info->next;
        free(info->path);
        free(info);
        info = next;
    }
}
hid_device *hid_open_path(const char *)
{
    return reinterpret_cast<hid_device *>(&s_device);
}
void hid_close(hid_device *)
{
}
const wchar_t *hid_error(hid_device *)
{
    return L"fake";
}
int hid_send_feature_report(hid_device *, const unsigned char *data, size_t length)
{
    if (s_failures > 0) {
        --s_failures;
        return -1;
    }
    s_reports << QByteArray(reinterpret_cast<const char *>(data), int(length));
    return int(length);
}
}

// -------------------------------------------------------------------------------

static const ModelProfile &model()
{
    const ModelProfile *m = matchModel({QStringLiteral("LENOVO"), QStringLiteral("Legion T5 26IOB6"), QStringLiteral("3733")});
    Q_ASSERT(m);
    return *m;
}

// Reports for one header, as "rrggbb".
static QStringList coloursFor(const QStringList &reports, const QString &header)
{
    QStringList out;
    for (const QString &r : reports) {
        if (r.startsWith(header + QLatin1Char(':'))) {
            out << r.mid(header.size() + 1);
        }
    }
    return out;
}

// "13:00ff50" per settings report, "commit" per commit – easy to compare.
static QStringList sent()
{
    QStringList out;
    for (const QByteArray &r : std::as_const(s_reports)) {
        if (r.at(1) == char(0x28) || r.at(1) == char(0x2D)) {
            out << QStringLiteral("commit");
        } else {
            out << QStringLiteral("%1:%2").arg(quint8(r.at(1)), 2, 16, QLatin1Char('0')).arg(QString::fromLatin1(r.mid(5, 3).toHex()));
        }
    }
    s_reports.clear();
    return out;
}

class FanApplierTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_hwmon;

    void setTemperature(int celsius)
    {
        QFile f(m_hwmon.filePath(QStringLiteral("hwmon1/temp1_input")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray::number(celsius * 1000) + '\n');
    }

    static HeaderSetting follows(quint8 header)
    {
        return {header, Mode::CpuTemperature, 1, 2, Rgb{1, 2, 3}};
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir().mkpath(m_hwmon.filePath(QStringLiteral("hwmon1")));
        QFile name(m_hwmon.filePath(QStringLiteral("hwmon1/name")));
        QVERIFY(name.open(QIODevice::WriteOnly));
        name.write("coretemp\n");
        QFile label(m_hwmon.filePath(QStringLiteral("hwmon1/temp1_label")));
        QVERIFY(label.open(QIODevice::WriteOnly));
        label.write("Package id 0\n");
    }

    void init()
    {
        QFile::remove(FanState::filePath());
        s_reports.clear();
        s_failures = 0;
        setTemperature(59); // + 1 °C offset: 60, colour 00ff50
    }

    // The first LED to follow starts the reading and goes out once, already in
    // the current colour – no flash of an old one.
    void followingStartsWithTheCurrentColour()
    {
        FanApplier fans(model(), nullptr, m_hwmon.path());
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13)});
        fans.flushNow();
        QCOMPARE(sent(), (QStringList{QStringLiteral("13:00ff50"), QStringLiteral("commit")}));
        QVERIFY(fans.followsTemperature());
        QVERIFY(fans.temperature()->isRunning());
        // Saved as the mode, not as the colour of the moment.
        QCOMPARE(FanState::load(model()).value(0x13).mode, Mode::CpuTemperature);
    }

    // A new step: only the following LEDs, nothing saved.
    void newStepSendsOnlyFollowers()
    {
        FanApplier fans(model(), nullptr, m_hwmon.path());
        fans.setTemperatureInterval(10);
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13), {0x11, Mode::Static, 1, 2, Rgb{0xff, 0, 0}}});
        fans.flushNow();
        sent();
        const QByteArray saved = [] {
            QFile f(FanState::filePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();

        setTemperature(65); // 66: 00ff00
        QVERIFY(QTest::qWaitFor([] { return !s_reports.isEmpty(); }));
        QCOMPARE(sent(), (QStringList{QStringLiteral("13:00ff00"), QStringLiteral("commit")}));
        QFile f(FanState::filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), saved);
    }

    // The command line switched an LED away from the temperature a moment ago,
    // before the window read the saved state again: the next step must not
    // overwrite it.
    void commandLineChangeIsNotOverwritten()
    {
        FanApplier fans(model(), nullptr, m_hwmon.path());
        fans.setTemperatureInterval(10);
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13), follows(0x11)});
        fans.flushNow();
        sent();

        QVERIFY(FanState::merge(FanState::Lock(), {{0x13, Mode::Static, 1, 2, Rgb{0xff, 0, 0xff}}}, &error)); // as the CLI does
        setTemperature(75); // 76: a0ff00, reached in steps (the smoothing)
        QVERIFY(QTest::qWaitFor([&] { return fans.temperature()->colour() == Rgb{0xa0, 0xff, 0x00}; }));
        const QStringList reports = sent();
        QVERIFY(coloursFor(reports, QStringLiteral("13")).isEmpty());
        QCOMPARE(coloursFor(reports, QStringLiteral("11")).constLast(), QStringLiteral("a0ff00"));

        QVERIFY(FanState::merge(FanState::Lock(), {{0x11, Mode::Off, 1, 2, Rgb{}}}, &error)); // nothing follows any more
        setTemperature(45);
        QVERIFY(QTest::qWaitFor([&] { return !fans.temperature()->isRunning(); }));
        QVERIFY(s_reports.isEmpty());
    }

    // An LED that starts following elsewhere (the CLI, with its own one-off
    // reading) gets the window's colour as soon as the window reads the state.
    void newFollowerGetsTheColourAtOnce()
    {
        FanApplier fans(model(), nullptr, m_hwmon.path());
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13)});
        fans.flushNow();
        sent();

        FanState::Headers state = FanState::load(model());
        state.insert(0x39, follows(0x39));
        fans.setCurrent(state);
        QCOMPARE(sent(), (QStringList{QStringLiteral("39:00ff50"), QStringLiteral("commit")}));
        fans.setCurrent(state); // nothing new
        QVERIFY(s_reports.isEmpty());
    }

    // A queued change sends its own colour; the reading leaves that LED alone.
    void queuedChangeWins()
    {
        FanApplier fans(model(), nullptr, m_hwmon.path());
        fans.setTemperatureInterval(10);
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13), follows(0x11)});
        fans.flushNow();
        sent();

        // Leaves the temperature while the reading moves on: from then on 0x13
        // only ever gets its own colour, whichever goes out first.
        setTemperature(75);
        QVERIFY(QTest::qWaitFor([&] { return fans.temperature()->colour() != Rgb{0x00, 0xff, 0x50}; }));
        fans.queue({{0x13, Mode::Static, 1, 2, Rgb{0, 0, 0xff}}});
        sent();
        QVERIFY(QTest::qWaitFor([&] { return fans.temperature()->colour() == Rgb{0xa0, 0xff, 0x00}; }));
        fans.flushNow();
        const QStringList reports = sent();
        QCOMPARE(coloursFor(reports, QStringLiteral("13")), QStringList{QStringLiteral("0000ff")});
        QCOMPARE(coloursFor(reports, QStringLiteral("11")).constLast(), QStringLiteral("a0ff00"));
        QVERIFY(fans.followsTemperature()); // 0x11 still
    }

    // A failed send is tried once more (apply() opens the controller again). A
    // step that still can't go out is reported; the next one that does tells
    // that the controller works again, so the error can go.
    void followerSendIsRetried()
    {
        FanApplier fans(model(), nullptr, m_hwmon.path());
        fans.setTemperatureInterval(10);
        QSignalSpy failed(&fans, &FanApplier::failed);
        QSignalSpy applied(&fans, &FanApplier::applied);
        QSignalSpy saved(&fans, &FanApplier::saved);
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13)});
        fans.flushNow();
        sent();
        QCOMPARE(saved.size(), 1);
        applied.clear();

        s_failures = 1;
        setTemperature(65); // 66: 00ff00
        QVERIFY(QTest::qWaitFor([] { return !s_reports.isEmpty(); }));
        QCOMPARE(sent(), (QStringList{QStringLiteral("13:00ff00"), QStringLiteral("commit")}));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(applied.size(), 1);

        s_failures = 2;
        setTemperature(70); // 71: 50ff00
        QVERIFY(QTest::qWaitFor([&] { return !failed.isEmpty(); }));
        QVERIFY(s_reports.isEmpty());

        setTemperature(75); // 76: a0ff00
        QVERIFY(QTest::qWaitFor([&] { return applied.size() == 2; }));
        QCOMPARE(coloursFor(sent(), QStringLiteral("13")).constLast(), QStringLiteral("a0ff00"));
        QCOMPARE(saved.size(), 1); // following saves nothing
    }
};

QTEST_GUILESS_MAIN(FanApplierTest)
#include "fanappliertest.moc"
