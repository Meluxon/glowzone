// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The fan queue and the LEDs following the CPU temperature, against a fake
// hidapi and a fake hwmon tree: what reaches the controller, and when.

#include "core/appliers.h"
#include "fakehid.h"
#include "testmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace Qt::StringLiterals;

using namespace Glowzone;
using namespace Ite8297;
using FakeHid::sent;


// Reports for one header, as "rrggbb".
static QStringList coloursFor(const QStringList &reports, const QString &header)
{
    QStringList out;
    for (const QString &r : reports) {
        if (r.startsWith(header + u':')) {
            out << r.mid(header.size() + 1);
        }
    }
    return out;
}

class FanApplierTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_hwmon;

    void setTemperature(int celsius)
    {
        QFile f(m_hwmon.filePath(u"hwmon1/temp1_input"_s));
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
        QDir().mkpath(m_hwmon.filePath(u"hwmon1"_s));
        QFile name(m_hwmon.filePath(u"hwmon1/name"_s));
        QVERIFY(name.open(QIODevice::WriteOnly));
        name.write("coretemp\n");
        QFile label(m_hwmon.filePath(u"hwmon1/temp1_label"_s));
        QVERIFY(label.open(QIODevice::WriteOnly));
        label.write("Package id 0\n");
    }

    void init()
    {
        QFile::remove(FanState::filePath());
        FakeHid::reset();
        setTemperature(59); // + 1 °C offset: 60, colour 00ff50
    }

    // The first LED to follow starts the reading and goes out once, already in
    // the current colour – no flash of an old one.
    void followingStartsWithTheCurrentColour()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13)});
        fans.flushNow();
        QCOMPARE(sent(), (QStringList{u"13:00ff50"_s, u"commit"_s}));
        QVERIFY(fans.followsTemperature());
        QVERIFY(fans.temperature()->isRunning());
        // Saved as the mode, not as the colour of the moment.
        QCOMPARE(FanState::load(legionModel()).value(0x13).mode, Mode::CpuTemperature);
    }

    // A new range sends to the following LEDs only if their colour changes,
    // a moment later: dragging a slider over several ranges sends once.
    void rangeSendsOnlyANewColour()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13)});
        fans.flushNow();
        FakeHid::reports.clear();
        fans.setTemperatureRange({34, 100}); // 60 °C keeps 00ff50
        QTest::qWait(200);
        QCOMPARE(sent(), QStringList());
        fans.setTemperatureRange({35, 75});
        fans.setTemperatureRange({35, 60}); // 60 °C is the top of it: the last colour
        QCOMPARE(sent(), QStringList());
        QTRY_VERIFY(!FakeHid::reports.isEmpty());
        QCOMPARE(sent(), (QStringList{u"13:ff0000"_s, u"commit"_s}));
        QTest::qWait(200);
        QCOMPARE(sent(), QStringList());
    }

    // A new step: only the following LEDs, nothing saved.
    void newStepSendsOnlyFollowers()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
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
        QVERIFY(QTest::qWaitFor([] { return !FakeHid::reports.isEmpty(); }));
        QCOMPARE(sent(), (QStringList{u"13:00ff00"_s, u"commit"_s}));
        QFile f(FanState::filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), saved);
    }

    // Sent but not saved (the state can't be written): the next step leaves
    // that LED alone, and the next save takes it along.
    void unsavedChangeIsNotOverwritten()
    {
        if (::geteuid() == 0) {
            QSKIP("root can write the state anyway");
        }
        const QString dir = QFileInfo(FanState::filePath()).absolutePath();
        const auto writable = [&dir](bool on) {
            return QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner | (on ? QFileDevice::WriteOwner : QFileDevice::Permissions()));
        };
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
        fans.setTemperatureInterval(10);
        QSignalSpy saveFailed(&fans, &FanApplier::saveFailed);
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13), follows(0x11)});
        fans.flushNow();
        sent();

        QVERIFY(writable(false));
        fans.queue({{0x13, Mode::Static, 1, 2, Rgb{0xff, 0, 0}}});
        fans.flushNow();
        QVERIFY(writable(true));
        QCOMPARE(saveFailed.count(), 1);
        QCOMPARE(sent(), (QStringList{u"13:ff0000"_s, u"commit"_s}));
        QCOMPARE(fans.unsaved().value(0x13).mode, Mode::Static);

        setTemperature(65); // 66: 00ff00
        QVERIFY(QTest::qWaitFor([] { return !FakeHid::reports.isEmpty(); }));
        QCOMPARE(sent(), (QStringList{u"11:00ff00"_s, u"commit"_s}));

        fans.queue({{0x12, Mode::Off, 1, 2, Rgb{}}});
        fans.flushNow();
        QVERIFY(fans.unsaved().isEmpty());
        QCOMPARE(FanState::load(legionModel()).value(0x13).mode, Mode::Static);
    }

    // The command line switched an LED away from the temperature a moment ago,
    // before the window read the saved state again: the next step must not
    // overwrite it.
    void commandLineChangeIsNotOverwritten()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
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
        QVERIFY(coloursFor(reports, u"13"_s).isEmpty());
        QCOMPARE(coloursFor(reports, u"11"_s).constLast(), u"a0ff00"_s);

        QVERIFY(FanState::merge(FanState::Lock(), {{0x11, Mode::Off, 1, 2, Rgb{}}}, &error)); // nothing follows any more
        setTemperature(45);
        QVERIFY(QTest::qWaitFor([&] { return !fans.temperature()->isRunning(); }));
        QVERIFY(FakeHid::reports.isEmpty());
    }

    // An LED that starts following elsewhere (the CLI, with its own one-off
    // reading) gets the window's colour as soon as the window reads the state.
    void newFollowerGetsTheColourAtOnce()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
        QString error;
        QVERIFY(fans.open(&error));
        fans.queue({follows(0x13)});
        fans.flushNow();
        sent();

        QVERIFY(FanState::merge(FanState::Lock(), {follows(0x39)}, &error)); // as the CLI does
        const FanState::Headers state = FanState::load(legionModel());
        fans.setCurrent(state);
        QCOMPARE(sent(), (QStringList{u"39:00ff50"_s, u"commit"_s}));
        fans.setCurrent(state); // nothing new
        QVERIFY(FakeHid::reports.isEmpty());
    }

    // A queued change sends its own colour; the reading leaves that LED alone.
    void queuedChangeWins()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
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
        QCOMPARE(coloursFor(reports, u"13"_s), QStringList{u"0000ff"_s});
        QCOMPARE(coloursFor(reports, u"11"_s).constLast(), u"a0ff00"_s);
        QVERIFY(fans.followsTemperature()); // 0x11 still
    }

    // A failed send is tried once more (apply() opens the controller again). A
    // step that still can't go out is reported; the next one that does tells
    // that the controller works again, so the error can go.
    void followerSendIsRetried()
    {
        FanApplier fans(legionModel(), nullptr, m_hwmon.path());
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

        FakeHid::failures = 1;
        setTemperature(65); // 66: 00ff00
        QVERIFY(QTest::qWaitFor([] { return !FakeHid::reports.isEmpty(); }));
        QCOMPARE(sent(), (QStringList{u"13:00ff00"_s, u"commit"_s}));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(applied.size(), 1);

        FakeHid::failures = 2;
        setTemperature(70); // 71: 50ff00
        QVERIFY(QTest::qWaitFor([&] { return !failed.isEmpty(); }));
        QVERIFY(FakeHid::reports.isEmpty());

        setTemperature(75); // 76: a0ff00
        QVERIFY(QTest::qWaitFor([&] { return applied.size() == 2; }));
        QCOMPARE(coloursFor(sent(), u"13"_s).constLast(), u"a0ff00"_s);
        QCOMPARE(saved.size(), 1); // following saves nothing
    }
};

QTEST_GUILESS_MAIN(FanApplierTest)
#include "fanappliertest.moc"
