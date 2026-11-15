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

using namespace Glowzone;
using namespace Glowzone::Ite8297;

static const ModelProfile &legion()
{
    return *matchModel({QStringLiteral("LENOVO"), QStringLiteral("Legion T5 26IOB6"), QStringLiteral("3733")});
}

static Profile sample(const QString &name)
{
    Profile p;
    p.name = name;
    p.fans.insert(0x13, {0x13, Mode::Rainbow, 1, 2, Rgb{0x15, 0x8d, 0xdd}});
    p.fans.insert(0x39, {0x39, Mode::Static, 1, 2, Rgb{0xff, 0, 0}});
    p.fans.insert(0x12, {0x12, Mode::Breathing, 3, 3, Rgb{0, 0, 0xff}});
    p.links.insert(QStringLiteral("top"), false);
    p.links.insert(QStringLiteral("front"), true);
    p.lights.insert(1, {1, 1, 3});
    p.lights.insert(2, {2, 5, 1});
    return p;
}

static int fileCount()
{
    return int(QDir(Profiles::directory()).entryList({QStringLiteral("*.json")}, QDir::Files).size());
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
        QVERIFY2(Profiles::save(legion(), sample(QStringLiteral("Night")), &error), qPrintable(error));
        const auto loaded = Profiles::find(legion(), QStringLiteral("Night"));
        QVERIFY(loaded);
        QCOMPARE(loaded->name, QStringLiteral("Night"));
        QVERIFY(loaded->sameSettings(sample(QStringLiteral("x"))));
    }

    void namesAreCaseInsensitiveAndOverwrite()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Night")), &error));
        Profile other = sample(QStringLiteral("NIGHT"));
        other.lights.clear();
        QVERIFY(Profiles::save(legion(), other, &error));
        QCOMPARE(fileCount(), 1);
        const auto loaded = Profiles::find(legion(), QStringLiteral("night"));
        QVERIFY(loaded);
        QVERIFY(loaded->lights.isEmpty());
        QCOMPARE(loaded->name, QStringLiteral("NIGHT"));
    }

    void slugCollisions()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Night!")), &error));
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Night?")), &error));
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Żółw")), &error)); // non-ASCII name
        QCOMPARE(fileCount(), 3);
        const QList<Profile> all = Profiles::loadAll(legion());
        QCOMPARE(all.size(), 3);
        QVERIFY(Profiles::find(legion(), QStringLiteral("Żółw")));
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
        QCOMPARE(names, (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("Profile 2"), QStringLiteral("Profile 10")}));
    }

    void rejectsBadNames()
    {
        QString error;
        QVERIFY(!Profiles::save(legion(), sample(QStringLiteral("   ")), &error));
        QVERIFY(!Profiles::save(legion(), sample(QString(Profiles::MaxNameLength + 1, QLatin1Char('x'))), &error));
        QCOMPARE(Profiles::cleanName(QStringLiteral("  Night   blue ")), QStringLiteral("Night blue"));
    }

    void skipsOtherModelsAndJunk()
    {
        QDir().mkpath(Profiles::directory());
        QJsonObject other{{QStringLiteral("version"), 1},
                          {QStringLiteral("name"), QStringLiteral("Foreign")},
                          {QStringLiteral("model"), QStringLiteral("Legion T7 34IMZ5")}};
        QFile f1(Profiles::directory() + QLatin1String("/foreign.json"));
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write(QJsonDocument(other).toJson());
        f1.close();
        QFile f2(Profiles::directory() + QLatin1String("/junk.json"));
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write("not json");
        f2.close();
        QVERIFY(Profiles::loadAll(legion()).isEmpty());
    }

    void rename()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Night")), &error));
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Day")), &error));
        QVERIFY(!Profiles::rename(legion(), QStringLiteral("night"), QStringLiteral("DAY"), &error)); // taken
        QVERIFY(Profiles::rename(legion(), QStringLiteral("night"), QStringLiteral("NIGHT"), &error)); // case only
        QVERIFY(Profiles::rename(legion(), QStringLiteral("Night"), QStringLiteral("Evening"), &error));
        QVERIFY(!Profiles::find(legion(), QStringLiteral("Night")));
        const auto p = Profiles::find(legion(), QStringLiteral("evening"));
        QVERIFY(p);
        QVERIFY(p->sameSettings(sample(QString())));
        QCOMPARE(fileCount(), 2);
        QVERIFY(!Profiles::rename(legion(), QStringLiteral("nope"), QStringLiteral("x"), &error));
    }

    void fanLayoutWarnings()
    {
        QVERIFY(EcLights::layoutWarning(legion(), 0x0b).isEmpty());
        const QString top = EcLights::layoutWarning(legion(), 0x0c);
        QVERIFY2(top.contains(QStringLiteral("Top fans")), qPrintable(top));
        QVERIFY(!top.contains(QStringLiteral("Front fans")));
        QVERIFY(EcLights::layoutWarning(legion(), 0x0d).contains(QStringLiteral("Front fans")));
        const QString unknown = EcLights::layoutWarning(legion(), 0);
        QVERIFY(!unknown.isEmpty());
        QVERIFY(unknown.contains(QStringLiteral("0x00")));
    }

    void remove()
    {
        QString error;
        QVERIFY(Profiles::save(legion(), sample(QStringLiteral("Night")), &error));
        QVERIFY(Profiles::remove(legion(), QStringLiteral("night"), &error));
        QCOMPARE(fileCount(), 0);
        QVERIFY(!Profiles::remove(legion(), QStringLiteral("night"), &error));
    }
};

QTEST_GUILESS_MAIN(ProfilesTest)
#include "profilestest.moc"
