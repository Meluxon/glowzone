// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The case-light queue against a fake helper: what gets sent, in which order,
// and what the window is told.

#include "core/appliers.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>

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
            r.error = QStringLiteral("firmware call failed");
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
};

QTEST_GUILESS_MAIN(LightApplierTest)
#include "lightappliertest.moc"
