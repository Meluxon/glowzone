// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The state the window shows (Lighting), against the fake fan controller:
// changes from the command line, the controller coming back, a failed save.
// The case lights need the root helper and aren't used here.

#include "gui/lighting.h"

#include "core/fanstate.h"
#include "core/settings.h"
#include "fakehid.h"
#include "testmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <sys/inotify.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

using namespace Glowzone;
using namespace Glowzone::Ite8297;

static constexpr Rgb Red{0xff, 0, 0};
static constexpr Rgb Green{0, 0xff, 0};

// Counts the times a file is replaced: QSaveFile and QSettings write a new
// file and rename it. Counted by the temporary names, which are unique – the
// kernel merges repeated identical events for the file itself.
class Replacements
{
public:
    explicit Replacements(const QString &file)
        : m_prefix(QFile::encodeName(QFileInfo(file).fileName()) + '.')
        , m_fd(inotify_init1(IN_NONBLOCK | IN_CLOEXEC))
    {
        inotify_add_watch(m_fd, QFile::encodeName(QFileInfo(file).absolutePath()).constData(), IN_MOVED_FROM);
    }
    ~Replacements()
    {
        ::close(m_fd);
    }
    int count()
    {
        alignas(inotify_event) char buffer[4096];
        for (ssize_t n; (n = ::read(m_fd, buffer, sizeof buffer)) > 0;) {
            for (char *p = buffer; p < buffer + n;) {
                const auto *e = reinterpret_cast<const inotify_event *>(p);
                m_count += e->len && QByteArray(e->name).startsWith(m_prefix);
                p += sizeof(inotify_event) + e->len;
            }
        }
        return m_count;
    }

private:
    QByteArray m_prefix;
    int m_fd;
    int m_count = 0;
};

static HeaderSetting staticColour(quint8 header, Rgb colour)
{
    return {header, Mode::Static, 1, 2, colour};
}

class LightingTest : public QObject
{
    Q_OBJECT

    const ModelProfile &m_model = legionModel();
    const FanDef &m_cpu = *m_model.fan(u"cpu"_s);
    const FanDef &m_rear = *m_model.fan(u"rear"_s);

    static QString stateDir()
    {
        return QFileInfo(FanState::filePath()).absolutePath();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        cleanup(); // also after a run that stopped half-way
        QFile::remove(FanState::filePath());
        FakeHid::reset();
    }

    void cleanup()
    {
        QFile::setPermissions(stateDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    // The command line sets one fan; before the window reads it, the user
    // changes another and the window saves. The command line's change still
    // reaches the window.
    void commandLineChangeNextToOurOwnSave()
    {
        Gui::Lighting lighting(m_model, nullptr);
        lighting.start(nullptr);
        QString error;
        QVERIFY(FanState::merge(FanState::Lock(), {staticColour(0x13, Red)}, &error)); // as the CLI does
        lighting.setLed(m_rear, 0x11, staticColour(0x11, Green));
        QTRY_COMPARE(lighting.led(0x13), staticColour(0x13, Red));
        QCOMPARE(lighting.led(0x11), staticColour(0x11, Green));
    }

    // Started before the controller could be opened: the first change that
    // gets through clears the problem.
    void controllerComingBack()
    {
        FakeHid::missing = true;
        Gui::Lighting lighting(m_model, nullptr);
        lighting.start(nullptr);
        QVERIFY(!lighting.fansAvailable());
        QVERIFY(!lighting.problems().isEmpty());
        FakeHid::missing = false;
        QSignalSpy changed(&lighting, &Gui::Lighting::changed);
        lighting.setLed(m_rear, 0x11, staticColour(0x11, Green));
        QTRY_VERIFY(lighting.fansAvailable());
        QVERIFY(lighting.problems().isEmpty());
        QVERIFY(!changed.isEmpty()); // the pages enable their controls
    }

    // Sent but not saved: the window keeps showing what the LED shows, and
    // the next save takes it along.
    void failedSaveKeepsTheChange()
    {
        if (::geteuid() == 0) {
            QSKIP("root can write the state anyway");
        }
        Gui::Lighting lighting(m_model, nullptr);
        lighting.start(nullptr);
        QSignalSpy errors(&lighting, &Gui::Lighting::error);
        QSignalSpy cleared(&lighting, &Gui::Lighting::errorCleared);
        QVERIFY(QFile::setPermissions(stateDir(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        lighting.setLed(m_rear, 0x11, staticColour(0x11, Green));
        QTRY_COMPARE(errors.size(), 1);
        cleanup();
        QCOMPARE(FakeHid::sent(), (QStringList{u"11:00ff00"_s, u"commit"_s}));
        QCOMPARE(lighting.led(0x11), staticColour(0x11, Green));

        lighting.setLed(m_cpu, 0x13, staticColour(0x13, Red));
        QTRY_COMPARE(cleared.size(), 1);
        const FanState::Headers saved = FanState::load(m_model);
        QCOMPARE(saved.value(0x11), staticColour(0x11, Green));
        QCOMPARE(saved.value(0x13), staticColour(0x13, Red));
    }

    // The state directory deleted while Glowzone runs (by hand): changes
    // the command line makes afterwards still reach the window.
    void deletedDirectoryIsWatchedAgain()
    {
        Gui::Lighting lighting(m_model, nullptr);
        lighting.start(nullptr);
        QVERIFY(QDir(stateDir()).removeRecursively());
        QTest::qWait(500);
        QString error;
        QVERIFY2(FanState::merge(FanState::Lock(), {staticColour(0x13, Red)}, &error), qPrintable(error));
        QTRY_COMPARE(lighting.led(0x13), staticColour(0x13, Red));
    }

    // Applying a profile records the split choices, the range and the name in
    // one write: each write makes another Glowzone read the settings again.
    void applyWritesTheSettingsOnce()
    {
        Gui::Lighting lighting(m_model, nullptr);
        lighting.start(nullptr);
        QFile::remove(Settings::filePath()); // nothing stored by an earlier run
        Settings::setCurrentProfile(u"Day"_s);
        Profile p;
        p.name = u"Night"_s;
        p.fans.insert(0x13, staticColour(0x13, Red));
        p.links = {{u"front"_s, false}, {u"top"_s, false}};
        p.temperatureRange = TemperatureRange{40, 90};
        Replacements writes(Settings::filePath());
        lighting.apply(p);
        QCOMPARE(writes.count(), 1);
        QCOMPARE(Settings::currentProfile(), u"Night"_s);
        QCOMPARE(Settings::linked(u"top"_s), std::optional(false));
        QCOMPARE(Settings::temperatureRange(*m_model.cpuTemperature), (TemperatureRange{40, 90}));
        QTest::qWait(600); // past the range's own delayed save
        QCOMPARE(writes.count(), 1);
    }

    // Choosing what an LED already shows sends and writes nothing.
    void sameSettingSendsNothing()
    {
        Gui::Lighting lighting(m_model, nullptr);
        lighting.start(nullptr);
        lighting.setLed(m_rear, 0x11, staticColour(0x11, Green));
        QTRY_VERIFY(!FakeHid::reports.isEmpty());
        QTRY_VERIFY(QFile::exists(FanState::filePath()));
        FakeHid::sent();
        const QDateTime written = QFileInfo(FanState::filePath()).lastModified();
        QSignalSpy changed(&lighting, &Gui::Lighting::changed);
        lighting.setLed(m_rear, 0x11, staticColour(0x11, Green));
        QTest::qWait(300);
        QVERIFY(FakeHid::reports.isEmpty());
        QCOMPARE(QFileInfo(FanState::filePath()).lastModified(), written);
        QVERIFY(changed.isEmpty());
    }
};

QTEST_GUILESS_MAIN(LightingTest)
#include "lightingtest.moc"
