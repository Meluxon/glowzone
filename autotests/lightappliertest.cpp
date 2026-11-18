// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The case-light queue against a fake helper: what gets sent, in which order,
// and what the window is told.

#include "core/appliers.h"
#include "core/eclights.h"
#include "testmodel.h"

#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

using namespace Glowzone;

// A helper call the test finishes by hand.
class FakeCall : public HelperCall
{
public:
    using HelperCall::HelperCall;
    Kind kind = Status;
    QList<EcRequest> requests;
    bool started = false;

    void start() override
    {
        started = true;
    }
};

// The fake helper: remembers every call and the lights' state.
class FakeHelper : public HelperCalls
{
public:
    QList<QPointer<FakeCall>> calls;
    QMap<quint8, EcLightState> lights{{1, {1, 1, 3}}, {2, {2, 1, 3}}}; // both off, brightness 3

    HelperCall *create(HelperCall::Kind kind, const QList<EcRequest> &requests, QObject *parent) override
    {
        auto *call = new FakeCall(parent);
        call->kind = kind;
        call->requests = requests;
        calls << call;
        return call;
    }

    // The call currently waiting for an answer, if any.
    FakeCall *running() const
    {
        for (const auto &c : calls) {
            if (c && c->started) {
                return c;
            }
        }
        return nullptr;
    }

    // Answers the running call as the real helper would.
    void answer(bool ok = true)
    {
        FakeCall *call = running();
        QVERIFY(call);
        HelperResult r;
        if (!ok) {
            r.error = u"firmware call failed"_s;
        } else if (call->kind == HelperCall::Layout) {
            r.layout = 0x0b;
        } else {
            for (const EcRequest &req : std::as_const(call->requests)) {
                EcLightState &l = lights[req.index];
                l.mode = req.mode ? req.mode : l.mode;
                l.brightness = req.brightness ? req.brightness : l.brightness;
            }
            r.lights = lights.values();
        }
        call->started = false;
        Q_EMIT call->finished(r);
        call->deleteLater();
    }
};

class LightApplierTest : public QObject
{
    Q_OBJECT

private:
    // A queue that has read the lights once, as the window does at start.
    struct Fixture {
        FakeHelper helper;
        LightApplier applier{helper, nullptr};
        Fixture()
        {
            applier.setDelay(0);
            applier.refresh();
            helper.answer();
        }
    };

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void refreshReportsState()
    {
        FakeHelper helper;
        LightApplier applier(helper, nullptr);
        QSignalSpy spy(&applier, &LightApplier::stateChanged);
        applier.refresh();
        QCOMPARE(helper.running()->kind, HelperCall::Status);
        helper.answer();
        QCOMPARE(spy.count(), 1);
    }

    void changesAreSent()
    {
        Fixture f;
        f.applier.queue({1, 2, 3});
        QTRY_VERIFY(f.helper.running());
        QCOMPARE(f.helper.running()->kind, HelperCall::Set);
        QCOMPARE(f.helper.running()->requests.size(), 1);
        f.helper.answer();
        QCOMPARE(f.helper.lights.value(1).mode, quint8(2));
    }

    // Each change costs a BIOS SMI: one that changes nothing isn't sent.
    void noOpIsNotSent()
    {
        Fixture f;
        f.applier.queue({1, 1, 3}); // already off at 3
        QTest::qWait(20);
        QVERIFY(!f.helper.running());
        QVERIFY(!f.applier.isBusy());
    }

    // Off → On → Off quickly: the second Off must not be taken for "nothing to
    // do" just because the lights were off before the On was sent.
    void changeBackWhileInFlightIsSent()
    {
        Fixture f;
        f.applier.queue({1, 2, 3}); // on
        QTRY_VERIFY(f.helper.running());
        f.applier.queue({1, 1, 3}); // and off again, while "on" is on its way
        f.helper.answer();
        QTRY_VERIFY(f.helper.running());
        f.helper.answer();
        QCOMPARE(f.helper.lights.value(1).mode, quint8(1));
    }

    // While a change is on its way, a request for that same target is a no-op.
    void repeatOfInFlightChangeIsNotSent()
    {
        Fixture f;
        f.applier.queue({1, 2, 3});
        QTRY_VERIFY(f.helper.running());
        f.applier.queue({1, 2, 3});
        f.helper.answer();
        QTest::qWait(20);
        QVERIFY(!f.helper.running());
    }

    // One call at a time: the fan-layout read waits for a running change.
    void layoutReadWaitsItsTurn()
    {
        Fixture f;
        QSignalSpy layout(&f.applier, &LightApplier::layoutRead);
        f.applier.queue({1, 2, 3});
        QTRY_VERIFY(f.helper.running());
        f.applier.readLayout();
        QCOMPARE(f.helper.running()->kind, HelperCall::Set); // still the change
        f.helper.answer();
        QVERIFY(f.helper.running());
        QCOMPARE(f.helper.running()->kind, HelperCall::Layout);
        f.helper.answer();
        QCOMPARE(layout.count(), 1);
        QCOMPARE(layout.constFirst().constFirst().toInt(), 0x0b);
    }

    // A failed layout read is not shown as an error.
    void failedLayoutReadIsSilent()
    {
        Fixture f;
        QSignalSpy failed(&f.applier, &LightApplier::failed);
        f.applier.readLayout();
        f.helper.answer(false);
        QCOMPARE(failed.count(), 0);
    }

    void failedChangeRereadsTheLights()
    {
        Fixture f;
        QSignalSpy failed(&f.applier, &LightApplier::failed);
        f.applier.queue({1, 2, 3});
        QTRY_VERIFY(f.helper.running());
        f.helper.answer(false);
        QCOMPARE(failed.count(), 1);
        QVERIFY(f.helper.running());
        QCOMPARE(f.helper.running()->kind, HelperCall::Status);
    }

    // Quitting: what is queued goes out, nothing is read back.
    void finishReadsNothingBack()
    {
        FakeHelper helper;
        LightApplier applier(helper, nullptr);
        applier.refresh();      // the first read, running
        applier.readLayout();   // waiting
        applier.queue({1, 2, 3});
        QSignalSpy idle(&applier, &LightApplier::idle);
        applier.finish();
        helper.answer();        // the read
        QCOMPARE(helper.running()->kind, HelperCall::Set);
        helper.answer(false);   // a failed change would be read back otherwise
        QVERIFY(!helper.running());
        QCOMPARE(idle.count(), 1);
    }

    // Lights the command line set: a request for that state is dropped.
    void knownStateIsNotSentAgain()
    {
        Fixture f;
        f.applier.setKnown({{1, 2, 3}});
        f.applier.queue({1, 2, 3});
        QTest::qWait(50);
        QVERIFY(!f.helper.running());
        f.applier.forget();
        f.applier.queue({1, 2, 3});
        QTRY_VERIFY(f.helper.running());
    }

    // What the command line records for a running window.
    void commandLineStateRoundTrip()
    {
        EcLights::saveState({{1, 2, 3}, {2, 5, 1}});
        QCOMPARE(EcLights::loadState(legionModel()), (QList<EcLightState>{{1, 2, 3}, {2, 5, 1}}));

        // Only lights, modes and levels the model has.
        QFile f(EcLights::stateFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"version": 1, "lights": {"1": {"mode": 258, "brightness": 3}, "2": {"mode": 2, "brightness": 1}, "7": {"mode": 2, "brightness": 1}}})");
        f.close();
        QCOMPARE(EcLights::loadState(legionModel()), (QList<EcLightState>{{2, 2, 1}}));

        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"version": 2, "lights": {"1": {"mode": 2, "brightness": 1}}})");
        f.close();
        QVERIFY(!EcLights::loadState(legionModel()));
        QFile::remove(EcLights::stateFilePath());
    }
};

QTEST_GUILESS_MAIN(LightApplierTest)
#include "lightappliertest.moc"
