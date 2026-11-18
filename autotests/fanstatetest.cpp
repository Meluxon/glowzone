// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

// The saved fan state is the only record of what the fans show – it must
// round-trip exactly and ignore anything it doesn't recognise.

#include "core/fanstate.h"
#include "testmodel.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

#include <unistd.h>

using namespace Qt::StringLiterals;

using namespace Glowzone;
using namespace Glowzone::Ite8297;

static QJsonObject entry(const char *mode, int speed)
{
    return {{u"mode"_s, QLatin1String(mode)},
            {u"speed"_s, speed},
            {u"brightness"_s, 2},
            {u"colour"_s, u"#ff0000"_s}};
}

static void writeState(int version, const QJsonObject &headers)
{
    QFile f(FanState::filePath());
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(QJsonObject{{u"version"_s, version}, {u"headers"_s, headers}}).toJson());
}

class FanStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        QFile::remove(FanState::filePath());
    }

    void emptyWhenNothingSaved()
    {
        QVERIFY(FanState::load(legionModel()).isEmpty());
    }

    void roundTrip()
    {
        QString error;
        const HeaderSetting cpu{0x13, Mode::Rainbow, 1, 2, Rgb{0x15, 0x8d, 0xdd}};
        const HeaderSetting front{0x3a, Mode::Static, 1, 3, Rgb{0xff, 0x3d, 0x9c}};
        QVERIFY2(FanState::merge(FanState::Lock(), {cpu, front}, &error), qPrintable(error));
        const FanState::Headers loaded = FanState::load(legionModel());
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.value(0x13), cpu);
        QCOMPARE(loaded.value(0x3a), front);
    }

    // The state keeps the mode, never the colour of the moment.
    void cpuTemperatureRoundTrip()
    {
        QString error;
        const HeaderSetting cpu{0x13, Mode::CpuTemperature, 1, 3, Rgb{0x15, 0x8d, 0xdd}};
        QVERIFY2(FanState::merge(FanState::Lock(), {cpu}, &error), qPrintable(error));
        QCOMPARE(FanState::load(legionModel()).value(0x13), cpu);
        QCOMPARE(FanState::headersToJson({{0x13, cpu}}).value("0x13"_L1).toObject().value("mode"_L1).toString(),
                 u"cpu-temperature"_s);
        QVERIFY(FanState::fitsModel(legionModel(), cpu));
    }

    void mergeKeepsOtherHeaders()
    {
        QString error;
        QVERIFY(FanState::merge(FanState::Lock(), {{0x13, Mode::Rainbow, 1, 2, Rgb{}}}, &error));
        QVERIFY(FanState::merge(FanState::Lock(), {{0x11, Mode::Off, 1, 2, Rgb{}}}, &error));
        QCOMPARE(FanState::load(legionModel()).size(), 2);
    }

    void ignoresJunk()
    {
        writeState(1,
                   {{u"0x13"_s, entry("static", 1)},   // fine
                    {u"0x20"_s, entry("static", 1)},   // unknown header
                    {u"0x11"_s, entry("laser", 1)},    // unknown mode
                    {u"0x12"_s, entry("static", 9)}}); // speed out of range
        const FanState::Headers loaded = FanState::load(legionModel());
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded.contains(0x13));
    }

    // A model offers a subset of what the controller can do: anything else in
    // the file is not shown or applied – but stays in the file.
    void dropsWhatTheModelDoesNotOffer()
    {
        ModelProfile small = legionModel();
        small.fans = {small.fans.constFirst()}; // the CPU fan (0x13) only
        small.fanModes = {0x00, 0x01, 0x09};    // off, static, rainbow
        small.fanLevels = 2;
        writeState(1,
                   {{u"0x13"_s, entry("static", 2)},   // fine
                    {u"0x11"_s, entry("static", 1)}}); // not this model's fan
        QCOMPARE(FanState::load(small).size(), 1);

        writeState(1, {{u"0x13"_s, entry("random", 1)}}); // mode not offered
        QVERIFY(FanState::load(small).isEmpty());
        writeState(1, {{u"0x13"_s, entry("static", 3)}}); // level not offered
        QVERIFY(FanState::load(small).isEmpty());
        QCOMPARE(FanState::load(legionModel()).size(), 1);                   // fine for the full model

        QString error;
        QVERIFY(FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 1, Rgb{}}, {0x11, Mode::Off, 1, 1, Rgb{}}}, &error));
        QVERIFY(FanState::merge(FanState::Lock(), {{0x13, Mode::Static, 1, 1, Rgb{}}}, &error));
        QCOMPARE(FanState::load(legionModel()).size(), 2); // 0x11 kept by merge
    }

    // Entries this version doesn't understand stay in the file untouched.
    void mergeKeepsEntriesItCannotRead()
    {
        writeState(1, {{u"0x11"_s, entry("laser", 1)}});
        QString error;
        QVERIFY(FanState::merge(FanState::Lock(), {{0x13, Mode::Static, 1, 2, Rgb{}}}, &error));
        QFile f(FanState::filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject headers = QJsonDocument::fromJson(f.readAll()).object().value(u"headers"_s).toObject();
        QCOMPARE(headers.value(u"0x11"_s).toObject().value(u"mode"_s).toString(), u"laser"_s);
        QVERIFY(headers.contains(u"0x13"_s));
    }

    void mergeLeavesANewerFormatAlone()
    {
        writeState(2, {{u"0x13"_s, entry("static", 1)}});
        QFile f(FanState::filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray before = f.readAll();
        f.close();
        QString error;
        QVERIFY(!FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }

    void mergeKeepsACopyOfAnUnreadableFile()
    {
        {
            QFile f(FanState::filePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ not json");
        }
        QString error;
        QVERIFY(FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error));
        QVERIFY(QFile::exists(FanState::filePath() + u".broken"_s));
        QCOMPARE(FanState::load(legionModel()).size(), 1);
        QFile::remove(FanState::filePath() + u".broken"_s);
    }

    void mergeKeepsACopyOfAFileWithoutAVersion()
    {
        {
            QFile f(FanState::filePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"headers": {"0x11": {"mode": "static"}}})");
        }
        QString error;
        QVERIFY2(FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error), qPrintable(error));
        QVERIFY(QFile::exists(FanState::filePath() + u".broken"_s));
        QCOMPARE(FanState::load(legionModel()).keys(), QList<quint8>{0x13});
        QFile::remove(FanState::filePath() + u".broken"_s);
    }

    // A current version without a "headers" object isn't a state file either.
    void mergeKeepsACopyOfAFileWithoutHeaders_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("missing") << QByteArray(R"({"version": 1})");
        QTest::newRow("string") << QByteArray(R"({"version": 1, "headers": "0x11"})");
        QTest::newRow("array") << QByteArray(R"({"version": 1, "headers": [{"mode": "static"}]})");
    }

    void mergeKeepsACopyOfAFileWithoutHeaders()
    {
        QFETCH(QByteArray, content);
        const QString broken = FanState::filePath() + u".broken"_s;
        {
            QFile f(FanState::filePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content);
        }
        QString error;
        QVERIFY2(FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error), qPrintable(error));
        QFile copy(broken);
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), content);
        copy.close();
        QCOMPARE(FanState::load(legionModel()).keys(), QList<quint8>{0x13});
        QFile::remove(broken);
    }

    // An older copy is replaced by the new one, and kept if moving fails
    // (here the directory refuses both, so this covers the error path).
    void mergeReplacesAnOlderCopyOnlyWhenMoving()
    {
        if (::geteuid() == 0) {
            QSKIP("root can move the file anyway");
        }
        const QString path = FanState::filePath();
        const QString broken = path + u".broken"_s;
        const auto write = [](const QString &name, const QByteArray &data) {
            QFile f(name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write(broken, "old copy");
        write(path, "{ not json");
        const QString dir = QFileInfo(path).absolutePath();
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner)); // no renames in it
        QString error;
        const bool merged = FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error);
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QVERIFY(!merged);
        QVERIFY(!error.isEmpty());
        QFile copy(broken);
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), QByteArray("old copy"));
        copy.close();

        QVERIFY2(FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error), qPrintable(error));
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), QByteArray("{ not json"));
        copy.close();
        QFile::remove(broken);
    }

    // A file that exists but can't be read isn't treated as missing:
    // replacing it would lose every entry not being changed. (Write-only, as
    // QSaveFile itself refuses to replace a file it can't write.)
    void mergeLeavesAFileItCannotReadAlone()
    {
        if (::geteuid() == 0) {
            QSKIP("root can open the file anyway");
        }
        writeState(1, {{u"0x11"_s, entry("static", 1)}});
        QFile f(FanState::filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray before = f.readAll();
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::WriteOwner));
        QString error;
        const bool merged = FanState::merge(FanState::Lock(), {{0x13, Mode::Off, 1, 2, Rgb{}}}, &error);
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(!merged);
        QVERIFY(!error.isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }

    void ignoresOtherVersions()
    {
        writeState(2, {{u"0x13"_s, entry("static", 1)}});
        QVERIFY(FanState::load(legionModel()).isEmpty());
    }

    // Two holders take turns (the second gives up after its short wait). The
    // lock file stays as it is: taking the lock writes nothing.
    void lockTakesTurns()
    {
        const QString path = QFileInfo(FanState::filePath()).absolutePath() + "/fans.lock"_L1;
        QDateTime stamp;
        QElapsedTimer waited;
        {
            const FanState::Lock first;
            QVERIFY(QFileInfo::exists(path));
            stamp = QFileInfo(path).lastModified();
            waited.start();
            const FanState::Lock second;
            QVERIFY(waited.elapsed() >= 1900);
        }
        waited.restart();
        {
            const FanState::Lock again;
            QVERIFY(waited.elapsed() < 500);
        }
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(QFileInfo(path).lastModified(), stamp);
    }
};

QTEST_GUILESS_MAIN(FanStateTest)
#include "fanstatetest.moc"
