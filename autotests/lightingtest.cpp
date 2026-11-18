// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The state the window shows (Lighting), against the fake fan controller:
// changes from the command line, the controller coming back, a failed save.
// The case lights need the root helper and aren't used here.

#include "gui/lighting.h"

#include "core/fanstate.h"
#include "fakehid.h"
#include "testmodel.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <unistd.h>

using namespace Qt::StringLiterals;

using namespace Glowzone;
using namespace Glowzone::Ite8297;

static constexpr Rgb Red{0xff, 0, 0};
static constexpr Rgb Green{0, 0xff, 0};

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
