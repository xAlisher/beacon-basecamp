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
    // ── setSigningKey tests ───────────────────────────────────────────────────
    // Key now comes from Keycard hardware each session — no file generated.

    void testSetSigningKeyValid()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        // Before setSigningKey: key is empty, beacon.key file not created
        auto cfg0 = parseObj(p.getBeaconConfig());
        QVERIFY(cfg0["signingKeyHex"].toString().isEmpty());
        QVERIFY(!QFile::exists(tmp.path() + "/beacon.key"));

        // Set a valid 32-byte key (64 hex chars)
        const QString key =
            "a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1";
        auto r = parseObj(p.setSigningKey(key));
        QVERIFY(!r.contains("error"));
        QVERIFY(r["ok"].toBool());

        auto cfg = parseObj(p.getBeaconConfig());
        QCOMPARE(cfg["signingKeyHex"].toString(), key);
    }

    void testSetSigningKeyInvalid()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        // Too short
        auto r1 = parseObj(p.setSigningKey("abc123"));
        QVERIFY(r1.contains("error"));

        // 64 chars but non-hex → fromHex returns partial/empty → size != 32
        auto r2 = parseObj(p.setSigningKey(QString(64, 'z')));
        QVERIFY(r2.contains("error"));

        // Key must remain empty after failed attempts
        auto cfg = parseObj(p.getBeaconConfig());
        QVERIFY(cfg["signingKeyHex"].toString().isEmpty());
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
        // Key is empty on init — delivered by Keycard hardware at runtime
        QVERIFY(cfg["signingKeyHex"].toString().isEmpty());
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

    // ── Channel label tests ───────────────────────────────────────────────────

    void testSetChannelLabelPersists()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QSettings s;
        s.remove("beacon");

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.setChannelLabel("Alice's Notes"));
        QVERIFY(r["ok"].toBool());

        auto cfg = parseObj(p.getBeaconConfig());
        QCOMPARE(cfg["channelLabel"].toString(), QString("Alice's Notes"));
    }

    void testGetBeaconConfigDefaultLabel()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QSettings s;
        s.remove("beacon");

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto cfg = parseObj(p.getBeaconConfig());
        QCOMPARE(cfg["channelLabel"].toString(), QString("My Beacon"));
    }

    void testSetChannelLabelTrimmed()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        p.setChannelLabel("  trimmed  ");
        auto cfg = parseObj(p.getBeaconConfig());
        QCOMPARE(cfg["channelLabel"].toString(), QString("trimmed"));
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

        // Without a signing key, configured = false
        auto st0 = parseObj(p.getStatus());
        QCOMPARE(st0["configured"].toBool(), false);
        QCOMPARE(st0["seenCids"].toInt(), 2);
        QCOMPARE(st0["inscribedCids"].toInt(), 1);

        // After key delivered by Keycard, configured = true
        p.setSigningKey("a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1");
        auto st = parseObj(p.getStatus());
        QCOMPARE(st["configured"].toBool(), true);
    }
};

QTEST_MAIN(TestBeaconPlugin)
#include "test_beacon_plugin.moc"
