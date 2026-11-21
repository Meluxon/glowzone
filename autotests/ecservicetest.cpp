// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The helper's gatekeeping: model check, BIOS presence check, whitelist.
// Uses a fake acpi_call that records every command.

#include "helper/ecservice.h"
#include "helper/procacpicall.h"

#include <QTest>
#include <functional>

using namespace Qt::StringLiterals;

using namespace Glowzone;
namespace HP = Glowzone::HelperProtocol;

class FakeAcpi : public AcpiBackend
{
public:
    bool ready = true;
    QByteArray layoutReply = "{0x0b, 0x00, 0x00, 0x00}";
    QByteArray presence = "0x3";
    QMap<int, QByteArray> led{{1, "{0x01, 0x01, 0x03, 0x00}"}, {2, "{0x02, 0x02, 0x01, 0x00}"}};
    QByteArray setReply = "0x0";
    QList<QByteArray> commands;

    bool ensureReady(QString *detail) override
    {
        if (!ready) {
            *detail = u"no module"_s;
        }
        return ready;
    }

    std::optional<QByteArray> call(const QByteArray &command, QString *) override
    {
        commands << command;
        if (command == "\\WMI2.WMG0 0 0x0e 0") {
            return presence;
        }
        if (command.startsWith("\\WMI2.WMG0 0 0x0f ")) {
            return led.value(command.right(1).toInt(), "Error: AE_BAD_PARAMETER");
        }
        if (command == "\\WMI2.WMG0 0 0x0a 0") {
            return layoutReply;
        }
        if (command.startsWith("\\WMI2.WMG0 0 0x10 ")) {
            if (setReply == "0x0") {
                set(command.mid(command.indexOf(" b") + 2));
            }
            return setReply;
        }
        return QByteArray("Error: unexpected command");
    }

    // As the EC does: GetLED reports what SetLED set; 0 leaves a value as it is.
    void set(const QByteArray &buffer)
    {
        const int index = buffer.mid(0, 2).toInt(nullptr, 16);
        const QList<QByteArray> now = led.value(index).mid(1, led.value(index).size() - 2).split(',');
        const int mode = buffer.mid(2, 2).toInt(nullptr, 16);
        const int brightness = buffer.mid(4, 2).toInt(nullptr, 16);
        led[index] = "{0x0" + QByteArray::number(index) + ", " + (mode ? "0x0" + QByteArray::number(mode) : now.at(1).trimmed()) + ", "
            + (brightness ? "0x0" + QByteArray::number(brightness) : now.at(2).trimmed()) + ", 0x00}";
    }

    int setCalls() const
    {
        return int(std::count_if(commands.cbegin(), commands.cend(), [](const QByteArray &c) {
            return c.startsWith("\\WMI2.WMG0 0 0x10 ");
        }));
    }
};

static const DmiInfo legion{u"LENOVO"_s, u"Legion T5 26IOB6"_s, u"3733"_s};

static QVariantMap request(const QVariantList &lights)
{
    return {{QLatin1String(HP::KeyLights), lights}};
}

static QVariantMap light(uint index, uint mode, uint brightness)
{
    return {{QLatin1String(HP::KeyIndex), index}, {QLatin1String(HP::KeyMode), mode}, {QLatin1String(HP::KeyBrightness), brightness}};
}

class EcServiceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unsupportedModelMakesNoCalls()
    {
        FakeAcpi acpi;
        const DmiInfo other{u"LENOVO"_s, u"Legion T5 26AMR5"_s, u"3733"_s};
        EcService service(other, acpi);
        QCOMPARE(service.status().error, int(HP::UnsupportedModel));
        QCOMPARE(service.set(request({light(1, 2, 1)})).error, int(HP::UnsupportedModel));
        QVERIFY(acpi.commands.isEmpty());
    }

    void missingModule()
    {
        FakeAcpi acpi;
        acpi.ready = false;
        EcService service(legion, acpi);
        QCOMPARE(service.status().error, int(HP::AcpiCallUnavailable));
        QVERIFY(acpi.commands.isEmpty());
    }

    void biosWithoutLights()
    {
        FakeAcpi acpi;
        acpi.presence = "0x1";
        EcService service(legion, acpi);
        QCOMPARE(service.set(request({light(1, 2, 1)})).error, int(HP::BiosLedMissing));
        QCOMPARE(acpi.setCalls(), 0);
    }

    void status()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        const EcService::Result r = service.status();
        QCOMPARE(r.error, 0);
        QCOMPARE(r.lights.size(), 2);
        QCOMPARE(r.lights.at(0).index, quint8(1));
        QCOMPARE(r.lights.at(0).mode, quint8(1));
        QCOMPARE(r.lights.at(0).brightness, quint8(3));
        QCOMPARE(r.lights.at(1).mode, quint8(2));
        QCOMPARE(acpi.commands.first(), QByteArray("\\WMI2.WMG0 0 0x0e 0"));
    }

    void setSendsExactBuffer()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        const EcService::Result r = service.set(request({light(1, 2, 3), light(2, 5, 1)}));
        QCOMPARE(r.error, 0);
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b01020300"));
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b02050100"));
        QCOMPARE(r.lights.size(), 2); // state read back afterwards
    }

    // SetLED pauses the machine: a request for what a light already shows
    // makes none, and nothing is read twice.
    void setSkipsWhatIsAlreadyTrue()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        const EcService::Result r = service.set(request({light(1, 1, 3), light(2, 2, 0)}));
        QCOMPARE(r.error, 0);
        QCOMPARE(acpi.setCalls(), 0);
        QCOMPARE(r.lights, (QList<EcLightState>{{1, 1, 3}, {2, 2, 1}}));
        QCOMPARE(acpi.commands.count("\\WMI2.WMG0 0 0x0f 1"), 1);
    }

    void setSendsOnlyWhatChanges()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        const EcService::Result r = service.set(request({light(1, 1, 3), light(2, 5, 0)}));
        QCOMPARE(r.error, 0);
        QCOMPARE(acpi.setCalls(), 1);
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b02050000"));
        QCOMPARE(r.lights, (QList<EcLightState>{{1, 1, 3}, {2, 5, 1}})); // read back afterwards
    }

    void rejectsAnythingOutsideTheWhitelist_data()
    {
        QTest::addColumn<QVariantMap>("args");
        QTest::newRow("no list") << QVariantMap{};
        QTest::newRow("empty list") << request({});
        QTest::newRow("unknown index") << request({light(3, 2, 1)});
        QTest::newRow("index 0") << request({light(0, 2, 1)});
        QTest::newRow("mode 6") << request({light(1, 6, 1)});
        QTest::newRow("brightness 5") << request({light(1, 2, 5)});
        QTest::newRow("nothing to change") << request({light(1, 0, 0)});
        QTest::newRow("duplicate") << request({light(1, 2, 1), light(1, 1, 1)});
        QTest::newRow("too many") << request({light(1, 2, 1), light(2, 2, 1), light(1, 1, 1)});
        QTest::newRow("not a map") << request({u"b01020300"_s});
        auto typed = [](const QVariant &mode) {
            return request({QVariantMap{{QLatin1String(HP::KeyIndex), 1u}, {QLatin1String(HP::KeyMode), mode}, {QLatin1String(HP::KeyBrightness), 1u}}});
        };
        QTest::newRow("text mode") << typed(u"0x10"_s);
        QTest::newRow("numeric text") << typed(u"2"_s);
        QTest::newRow("fraction") << typed(2.0);
        QTest::newRow("boolean") << typed(true);
        QTest::newRow("negative") << typed(-1);
        QTest::newRow("huge") << typed(qulonglong(1) << 40);
    }

    void rejectsAnythingOutsideTheWhitelist()
    {
        QFETCH(QVariantMap, args);
        FakeAcpi acpi;
        EcService service(legion, acpi);
        QCOMPARE(service.set(args).error, int(HP::InvalidArguments));
        QVERIFY(acpi.commands.isEmpty()); // rejected before any ACPI call
    }

    void acceptsAnyIntegerType()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        const QVariantMap entry{{QLatin1String(HP::KeyIndex), 1}, {QLatin1String(HP::KeyMode), qlonglong(2)}, {QLatin1String(HP::KeyBrightness), qulonglong(3)}};
        QCOMPARE(service.set(request({entry})).error, 0);
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b01020300"));
    }

    void zeroMeansUnchanged()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        QCOMPARE(service.set(request({light(2, 0, 4)})).error, 0);
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b02000400"));
    }

    void firmwareError()
    {
        FakeAcpi acpi;
        acpi.setReply = "Error: AE_AML_BUFFER_LIMIT";
        EcService service(legion, acpi);
        QCOMPARE(service.set(request({light(1, 2, 1)})).error, int(HP::AcpiCallFailed));
    }

    void garbledGetLed()
    {
        FakeAcpi acpi;
        acpi.led[2] = "{0x01, 0x02, 0x03}"; // wrong index echoed back
        EcService service(legion, acpi);
        QCOMPARE(service.status().error, int(HP::UnexpectedReply));
    }

    void unreadablePresenceIsNotMissingLights()
    {
        FakeAcpi acpi;
        acpi.presence = "0x3called";
        EcService service(legion, acpi);
        QCOMPARE(service.status().error, int(HP::UnexpectedReply));
    }

    void cleanReply_data()
    {
        QTest::addColumn<QByteArray>("raw");
        QTest::addColumn<QByteArray>("clean");
        // Real reply from the Legion: "0x3" written over "not called".
        QTest::newRow("stale tail") << QByteArray("0x3\0called", 10) << QByteArray("0x3");
        QTest::newRow("buffer") << QByteArray("{0x01, 0x01, 0x03, 0x00}\0ed\n", 28) << QByteArray("{0x01, 0x01, 0x03, 0x00}");
        QTest::newRow("no nul") << QByteArray("0x0\n") << QByteArray("0x0");
        QTest::newRow("not called") << QByteArray("not called") << QByteArray("not called");
    }

    void cleanReply()
    {
        QFETCH(QByteArray, raw);
        QFETCH(QByteArray, clean);
        QCOMPARE(ProcAcpiCall::cleanReply(raw), clean);
    }

    void fanLayout_data()
    {
        QTest::addColumn<QByteArray>("reply");
        QTest::addColumn<int>("layout");
        QTest::addColumn<int>("error");
        QTest::newRow("normal (all fans present)") << QByteArray("{0x0b, 0x00, 0x00, 0x00}") << 0x0b << 0;
        QTest::newRow("top missing") << QByteArray("{0x0c, 0x00, 0x00, 0x00}") << 0x0c << 0;
        QTest::newRow("no match: plain zero") << QByteArray("0x0") << 0 << 0;
        QTest::newRow("garbage") << QByteArray("whatever") << -1 << int(HP::UnexpectedReply);
        QTest::newRow("acpi error") << QByteArray("Error: AE_NOT_FOUND") << -1 << int(HP::AcpiCallFailed);
    }

    void fanLayout()
    {
        QFETCH(QByteArray, reply);
        QFETCH(int, layout);
        QFETCH(int, error);
        FakeAcpi acpi;
        acpi.layoutReply = reply;
        EcService service(legion, acpi);
        const EcService::LayoutResult r = service.layout();
        QCOMPARE(r.error, error);
        QCOMPARE(r.layout, layout);
        QCOMPARE(acpi.setCalls(), 0); // read-only
    }

    // The fan layout is read on its own: one call, whatever the lights report.
    void fanLayoutDoesNotNeedTheLights()
    {
        FakeAcpi acpi;
        acpi.presence = "0x0";
        EcService service(legion, acpi);
        const EcService::LayoutResult r = service.layout();
        QCOMPARE(r.error, 0);
        QCOMPARE(r.layout, 0x0b);
        QCOMPARE(acpi.commands, QList<QByteArray>{"\\WMI2.WMG0 0 0x0a 0"});
    }

    void variantRoundTrip()
    {
        const QList<EcLightState> in{{1, 2, 3}, {2, 5, 1}};
        QCOMPARE(HP::lightsFromVariant(HP::lightsToVariant(in)), in);
    }

    // The contract: what the app sends is what the helper accepts.
    void appRequestsReachTheHelper()
    {
        FakeAcpi acpi;
        EcService service(legion, acpi);
        const EcService::Result r = service.set(HP::lightsToVariant({{1, 2, 0}, {2, 0, 4}}));
        QCOMPARE(r.error, 0);
        QCOMPARE(acpi.setCalls(), 2);
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b01020000"));
        QVERIFY(acpi.commands.contains("\\WMI2.WMG0 0 0x10 b02000400"));
    }
};

QTEST_GUILESS_MAIN(EcServiceTest)
#include "ecservicetest.moc"
