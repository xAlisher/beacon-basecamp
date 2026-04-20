#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>

#include "plugin/BeaconPlugin.h"

// ── Helpers ───────────────────────────────────────────────────────────────────
static QJsonObject parseObj(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

static QJsonArray parseArr(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).array();
}

// ── Test class ────────────────────────────────────────────────────────────────
class TestBeaconPlugin : public QObject
{
    Q_OBJECT

private:
    // Helper: create a plugin with a temporary persistence path
    BeaconPlugin* makePlugin(const QString& persistencePath)
    {
        auto* p = new BeaconPlugin();
        p->setProperty("instancePersistencePath", persistencePath);
        p->initLogos(nullptr);
        return p;
    }

private slots:
    // ── Key generation tests ──────────────────────────────────────────────────

    void testEnsureKeyCreatesFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        QString keyPath = tmp.path() + "/beacon.key";
        QVERIFY(QFile::exists(keyPath));

        QFile f(keyPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray content = f.readAll().trimmed();
        f.close();

        // 64-char hex
        QCOMPARE(content.length(), 64);
        // All hex chars
        QRegularExpression hexRe("^[0-9a-f]{64}$");
        QVERIFY(hexRe.match(QString::fromLatin1(content)).hasMatch());

        // Mode 0600
        QFileDevice::Permissions perms = QFile::permissions(keyPath);
        QVERIFY(perms & QFileDevice::ReadOwner);
        QVERIFY(perms & QFileDevice::WriteOwner);
        QVERIFY(!(perms & QFileDevice::ReadGroup));
        QVERIFY(!(perms & QFileDevice::WriteGroup));
        QVERIFY(!(perms & QFileDevice::ReadOther));
        QVERIFY(!(perms & QFileDevice::WriteOther));
    }

    void testEnsureKeyIdempotent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        BeaconPlugin p1;
        p1.setProperty("instancePersistencePath", tmp.path());
        p1.initLogos(nullptr);

        auto cfg1 = parseObj(p1.getBeaconConfig());
        QString key1 = cfg1["signingKeyHex"].toString();

        // Second plugin instance, same path
        BeaconPlugin p2;
        p2.setProperty("instancePersistencePath", tmp.path());
        p2.initLogos(nullptr);

        auto cfg2 = parseObj(p2.getBeaconConfig());
        QString key2 = cfg2["signingKeyHex"].toString();

        // Same key — not regenerated
        QCOMPARE(key1, key2);
        QVERIFY(!key1.isEmpty());
    }

    // ── Config tests ──────────────────────────────────────────────────────────

    void testGetBeaconConfigDefaults()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Clean QSettings
        QSettings s;
        s.remove("beacon");

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto cfg = parseObj(p.getBeaconConfig());
        QVERIFY(cfg.contains("signingKeyHex"));
        QVERIFY(cfg["signingKeyHex"].toString().length() == 64);
        QCOMPARE(cfg["nodeUrl"].toString(), QString("http://127.0.0.1:8080"));
        QCOMPARE(cfg["watchStash"].toBool(), true);
        QCOMPARE(cfg["persistencePath"].toString(), tmp.path());
    }

    void testSetNodeUrl()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Clean QSettings
        QSettings s;
        s.remove("beacon");

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.setNodeUrl("http://node.example.com:9090"));
        QVERIFY(!r.contains("error"));
        QVERIFY(r["ok"].toBool());

        auto cfg = parseObj(p.getBeaconConfig());
        QCOMPARE(cfg["nodeUrl"].toString(), QString("http://node.example.com:9090"));
    }

    void testSetNodeUrlEmpty()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.setNodeUrl(""));
        QVERIFY(r.contains("error"));
    }

    // ── Inscription log tests ─────────────────────────────────────────────────

    void testPinCidAddsEntry()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.pinCid("QmTestCid1111111111111111111111111111111111111111",
                                   "test label"));
        QVERIFY(r["ok"].toBool());
        QVERIFY(!r.contains("duplicate"));
        QVERIFY(r.contains("entryIndex"));

        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log.size(), 1);
        QCOMPARE(log[0].toObject()["cid"].toString(),
                 QString("QmTestCid1111111111111111111111111111111111111111"));
        QCOMPARE(log[0].toObject()["status"].toString(), QString("pending"));
        QCOMPARE(log[0].toObject()["label"].toString(), QString("test label"));
    }

    void testPinCidDuplicateSkip()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        const QString cid = "QmTestCid1111111111111111111111111111111111111111";
        p.pinCid(cid, "first");

        auto r = parseObj(p.pinCid(cid, "second attempt"));
        QVERIFY(r["ok"].toBool());
        QVERIFY(r["duplicate"].toBool());

        // Still only 1 entry
        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log.size(), 1);
    }

    void testInscriptionLogPersists()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        {
            BeaconPlugin p;
            p.setProperty("instancePersistencePath", tmp.path());
            p.initLogos(nullptr);

            p.pinCid("QmTestCid1111111111111111111111111111111111111111", "persisted label");
        }

        // New instance, same path — log should reload
        BeaconPlugin p2;
        p2.setProperty("instancePersistencePath", tmp.path());
        p2.initLogos(nullptr);

        auto log = parseArr(p2.getInscriptionLog());
        QCOMPARE(log.size(), 1);
        QCOMPARE(log[0].toObject()["label"].toString(), QString("persisted label"));
    }

    void testConfirmInscription()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto pin = parseObj(p.pinCid("QmTestCid1111111111111111111111111111111111111111",
                                     "confirm test"));
        int idx = pin["entryIndex"].toInt();

        auto r = parseObj(p.confirmInscription(idx, "deadbeef1234", "ok"));
        QVERIFY(r["ok"].toBool());

        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log[0].toObject()["status"].toString(), QString("ok"));
        QCOMPARE(log[0].toObject()["inscriptionId"].toString(), QString("deadbeef1234"));
    }

    void testConfirmInscriptionBadIndex()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.confirmInscription(99, "id", "ok"));
        QVERIFY(r.contains("error"));
    }

    void testGetStatusCounts()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        // Pin two CIDs
        auto pin1 = parseObj(p.pinCid("QmTestCid1111111111111111111111111111111111111111", "a"));
        auto pin2 = parseObj(p.pinCid("QmTestCid2222222222222222222222222222222222222222", "b"));

        // Confirm one as ok, one as error
        p.confirmInscription(pin1["entryIndex"].toInt(), "id1", "ok");
        p.confirmInscription(pin2["entryIndex"].toInt(), "",    "error");

        auto st = parseObj(p.getStatus());
        QCOMPARE(st["configured"].toBool(), true);
        QCOMPARE(st["seenCids"].toInt(), 2);
        QCOMPARE(st["inscribedCids"].toInt(), 1);
    }
};

QTEST_MAIN(TestBeaconPlugin)
#include "test_beacon_plugin.moc"
