// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eclights.h"
#include "core/profiles.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

using namespace Glowzone;
using namespace Glowzone::Ite8297;

static const ModelProfile &legion()
{
    return *matchModel({u"LENOVO"_s, u"Legion T5 26IOB6"_s, u"3733"_s});
}

// Same lighting, ignoring the name.
static bool sameSettings(const Profile &a, const Profile &b)
{
    return a.fans == b.fans && a.links == b.links && a.temperatureRange == b.temperatureRange && a.lights == b.lights;
}

static Profile sample(const QString &name)
{
    Profile p;
    p.name = name;
    p.fans.insert(0x13, {0x13, Mode::Rainbow, 1, 2, Rgb{0x15, 0x8d, 0xdd}});
    p.fans.insert(0x39, {0x39, Mode::Static, 1, 2, Rgb{0xff, 0, 0}});
    p.fans.insert(0x12, {0x12, Mode::Breathing, 3, 3, Rgb{0, 0, 0xff}});
    p.links.insert(u"top"_s, false);
    p.links.insert(u"front"_s, true);
    p.lights.insert(1, {1, 1, 3});
    p.lights.insert(2, {2, 5, 1});
    p.temperatureRange = TemperatureRange{35, 80};
    return p;
}

static int fileCount()
{
    return int(QDir(Profiles::directory()).entryList({u"*.json"_s}, QDir::Files).size());
}

class ProfilesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        QDir(Profiles::directory()).removeRecursively();
    }

    void roundTrip()
    {
        QString error;
        QVERIFY2(Profiles::save(legion(), sample(u"Night"_s), &error), qPrintable(error));
        const auto loaded = Profiles::find(legion(), u"Night"_s);
        QVERIFY(loaded);
        QCOMPARE(loaded->name, u"Night"_s);
        QVERIFY(sameSettings(*loaded, sample(u"x"_s)));
    }

    // A file without a range counts as made with the default one.
    void withoutTemperatureRange()
    {
        Profile p = sample(u"Old"_s);
        p.temperatureRange.reset();
        QString error;
        QVERIFY2(Profiles::save(legion(), p, &error), qPrintable(error));
        const auto loaded = Profiles::find(legion(), u"Old"_s);
        QVERIFY(loaded);
        QCOMPARE(loaded->temperatureRange, std::optional(legion().cpuTemperature->defaultRange));
    }

    // Only whole numbers make a range; anything else gives the default one.
    void temperatureRangeFromJson_data()
    {
        QTest::addColumn<QJsonValue>("range");
        QTest::addColumn<int>("coolest");
        QTest::addColumn<int>("hottest");
        const TemperatureRange d = legion().cpuTemperature->defaultRange;
        auto obj = [](QJsonValue c, QJsonValue h) {
            return QJsonValue(QJsonObject{{u"coolest"_s, c}, {u"hottest"_s, h}});
        };
        QTest::newRow("whole") << obj(40, 85) << 40 << 85;
        QTest::newRow("fraction") << obj(35, 80.5) << d.coolest << d.hottest;
        QTest::newRow("huge") << obj(35, 1e12) << d.coolest << d.hottest;
        QTest::newRow("string") << obj(u"35"_s, 80) << d.coolest << d.hottest;
        QTest::newRow("one key") << QJsonValue(QJsonObject{{u"coolest"_s, 40}}) << d.coolest << d.hottest;
        QTest::newRow("not an object") << QJsonValue(42) << d.coolest << d.hottest;
    }
    void temperatureRangeFromJson()
    {
        QFETCH(QJsonValue, range);
        QFETCH(int, coolest);
        QFETCH(int, hottest);
        QDir().mkpath(Profiles::directory());
        const QJsonObject root{{u"version"_s, 1},
                               {u"name"_s, u"Hand-edited"_s},
                               {u"model"_s, QLatin1String(legion().productVersion)},
                               {u"cpuTemperature"_s, range}};
        QFile f(Profiles::directory() + "/hand-edited.json"_L1);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();
        const auto loaded = Profiles::find(legion(), u"Hand-edited"_s);
        QVERIFY(loaded);
        QCOMPARE(loaded->temperatureRange, std::optional(TemperatureRange{coolest, hottest}));
    }

    void namesAreCaseInsensitiveAndOverwrite()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(u"Night"_s), &error));
        Profile other = sample(u"NIGHT"_s);
        other.lights.clear();
        QVERIFY(Profiles::save(legion(), other, &error));
        QCOMPARE(fileCount(), 1);
        const auto loaded = Profiles::find(legion(), u"night"_s);
        QVERIFY(loaded);
        QVERIFY(loaded->lights.isEmpty());
        QCOMPARE(loaded->name, u"NIGHT"_s);
    }

    void slugCollisions()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(u"Night!"_s), &error));
        QVERIFY(Profiles::save(legion(), sample(u"Night?"_s), &error));
        QVERIFY(Profiles::save(legion(), sample(u"Żółw"_s), &error)); // non-ASCII name
        QCOMPARE(fileCount(), 3);
        const QList<Profile> all = Profiles::loadAll(legion());
        QCOMPARE(all.size(), 3);
        QVERIFY(Profiles::find(legion(), u"Żółw"_s));
    }

    void sortedNaturally()
    {
        QString error;
        for (const char *n : {"b", "Profile 10", "a", "Profile 2"}) {
            QVERIFY(Profiles::save(legion(), sample(QLatin1String(n)), &error));
        }
        QStringList names;
        for (const Profile &p : Profiles::loadAll(legion())) {
            names << p.name;
        }
        QCOMPARE(names, (QStringList{u"a"_s, u"b"_s, u"Profile 2"_s, u"Profile 10"_s}));
    }

    void rejectsBadNames()
    {
        QString error;
        QVERIFY(!Profiles::save(legion(), sample(u"   "_s), &error));
        QVERIFY(!Profiles::save(legion(), sample(QString(Profiles::MaxNameLength + 1, u'x')), &error));
        QCOMPARE(Profiles::cleanName(u"  Night   blue "_s), u"Night blue"_s);
    }

    void skipsOtherModelsAndJunk()
    {
        QDir().mkpath(Profiles::directory());
        QJsonObject other{{u"version"_s, 1},
                          {u"name"_s, u"Foreign"_s},
                          {u"model"_s, u"Legion T7 34IMZ5"_s}};
        QFile f1(Profiles::directory() + "/foreign.json"_L1);
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write(QJsonDocument(other).toJson());
        f1.close();
        QFile f2(Profiles::directory() + "/junk.json"_L1);
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write("not json");
        f2.close();
        QVERIFY(Profiles::loadAll(legion()).isEmpty());
    }

    void rename()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(u"Night"_s), &error));
        QVERIFY(Profiles::save(legion(), sample(u"Day"_s), &error));
        QVERIFY(!Profiles::rename(legion(), u"night"_s, u"DAY"_s, &error)); // taken
        QVERIFY(Profiles::rename(legion(), u"night"_s, u"NIGHT"_s, &error)); // case only
        QVERIFY(Profiles::rename(legion(), u"Night"_s, u"Evening"_s, &error));
        QVERIFY(!Profiles::find(legion(), u"Night"_s));
        const auto p = Profiles::find(legion(), u"evening"_s);
        QVERIFY(p);
        QVERIFY(sameSettings(*p, sample(QString())));
        QCOMPARE(fileCount(), 2);
        QVERIFY(!Profiles::rename(legion(), u"nope"_s, u"x"_s, &error));
    }

    void fanLayoutWarnings()
    {
        QVERIFY(EcLights::layoutWarning(legion(), 0x0b).isEmpty());
        const QString top = EcLights::layoutWarning(legion(), 0x0c);
        QVERIFY2(top.contains(u"Top fans"_s), qPrintable(top));
        QVERIFY(!top.contains(u"Front fans"_s));
        QVERIFY(EcLights::layoutWarning(legion(), 0x0d).contains(u"Front fans"_s));
        const QString unknown = EcLights::layoutWarning(legion(), 0);
        QVERIFY(!unknown.isEmpty());
        QVERIFY(unknown.contains(u"0x00"_s));
    }

    void remove()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(u"Night"_s), &error));
        QVERIFY(Profiles::remove(legion(), u"night"_s, &error));
        QCOMPARE(fileCount(), 0);
        QVERIFY(!Profiles::remove(legion(), u"night"_s, &error));
    }
};

QTEST_GUILESS_MAIN(ProfilesTest)
#include "profilestest.moc"
