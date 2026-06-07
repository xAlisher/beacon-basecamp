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

        // getStatus must report configured=false (guard stays closed)
        auto st = parseObj(p.getStatus());
        QCOMPARE(st["configured"].toBool(), false);
    }

    void testClearSigningKey()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        // Set a valid key
        p.setSigningKey("a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1");
        QVERIFY(parseObj(p.getStatus())["configured"].toBool());

        // Clear (card removal / auth restart)
        auto r = parseObj(p.clearSigningKey());
        QVERIFY(r["ok"].toBool());

        // Backend state reset: key empty, configured=false
        QVERIFY(parseObj(p.getBeaconConfig())["signingKeyHex"].toString().isEmpty());
        QCOMPARE(parseObj(p.getStatus())["configured"].toBool(), false);
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
        QCOMPARE(log[0].toObject()["status"].toString(), QString("queued"));
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
        auto first = parseObj(p.pinCid(cid, "first"));
        int idx = first["entryIndex"].toInt();

        // Confirm with real inscriptionId — marks entry as on-chain
        p.confirmInscription(idx,
            "aabbcc1234567890aabbcc1234567890aabbcc1234567890aabbcc1234567890",
            "confirmed");

        // Second pinCid for same confirmed CID must return duplicate
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

    // ── source field tests ────────────────────────────────────────────────────

    void testPinCidStoresSource()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.pinCid("QmTestCid1111111111111111111111111111111111111111",
                                   "test", "stash"));
        QVERIFY(r["ok"].toBool());

        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log.size(), 1);
        QCOMPARE(log[0].toObject()["source"].toString(), QString("stash"));
    }

    void testPinCidDefaultSourceEmpty()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        // No source arg — defaults to ""
        p.pinCid("QmTestCid1111111111111111111111111111111111111111", "test");

        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log[0].toObject()["source"].toString(), QString(""));
    }

    // ── deriveModuleSigningKey tests ──────────────────────────────────────────

    void testDeriveModuleSigningKeyNoKey()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.deriveModuleSigningKey("stash"));
        QVERIFY(r.contains("error"));
    }

    void testDeriveModuleSigningKeyReturnsHex()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);
        p.setSigningKey("a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1");

        auto r = parseObj(p.deriveModuleSigningKey("stash"));
        QVERIFY(!r.contains("error"));
        QVERIFY(r.contains("signingKey"));
        // SHA256 produces 32 bytes = 64 hex chars
        QCOMPARE(r["signingKey"].toString().length(), 64);
    }

    void testDeriveModuleSigningKeyDeterministic()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);
        p.setSigningKey("a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1c2d3e4f5a0b1");

        auto r1 = parseObj(p.deriveModuleSigningKey("stash"));
        auto r2 = parseObj(p.deriveModuleSigningKey("stash"));
        auto r3 = parseObj(p.deriveModuleSigningKey("notes"));

        // Same module → same key
        QCOMPARE(r1["signingKey"].toString(), r2["signingKey"].toString());
        // Different module → different key
        QVERIFY(r1["signingKey"].toString() != r3["signingKey"].toString());
    }

    // ── getModules tests ──────────────────────────────────────────────────────

    void testGetModulesEmpty()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseArr(p.getModules());
        QCOMPARE(r.size(), 0);
    }

    void testGetModulesGroupsBySource()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        p.pinCid("QmTestCid1111111111111111111111111111111111111111", "a", "stash");
        p.pinCid("QmTestCid2222222222222222222222222222222222222222", "b", "stash");
        p.pinCid("QmTestCid3333333333333333333333333333333333333333", "c", "notes");
        p.pinCid("QmTestCid4444444444444444444444444444444444444444", "d", "");

        auto r = parseArr(p.getModules());
        // 3 groups: "stash", "notes", ""
        QCOMPARE(r.size(), 3);

        // Find stash group
        QJsonObject stashGroup;
        for (int i = 0; i < r.size(); ++i) {
            if (r[i].toObject()["name"].toString() == "stash")
                stashGroup = r[i].toObject();
        }
        QVERIFY(!stashGroup.isEmpty());
        QCOMPARE(stashGroup["cidCount"].toInt(), 2);
    }

    // ── ensureCheckpointsDir tests ────────────────────────────────────────────

    void testEnsureCheckpointsDirCreates()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.ensureCheckpointsDir());
        QVERIFY(r["ok"].toBool());
        QVERIFY(QDir(tmp.path() + "/checkpoints").exists());
    }
    // ── inscription lifecycle: slotFrom / libAtSubmit ────────────────────────

    void testPinCidStoresSlotFrom()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.pinCid("QmSlotCid111111111111111111111111111111111111111",
                                   "label", "keeper", 4711799, 4711345));
        QVERIFY(r["ok"].toBool());
        QVERIFY(!r.contains("duplicate"));

        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log.size(), 1);
        QJsonObject e = log[0].toObject();
        QCOMPARE(e["slotFrom"].toInt(),    4711799);
        QCOMPARE(e["libAtSubmit"].toInt(), 4711345);
        QCOMPARE(e["status"].toString(),   QString("queued"));
    }

    void testPinCidZeroSlotFromIsValid()
    {
        // slotFrom=0 is valid (node info unavailable at time of pinCid)
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto r = parseObj(p.pinCid("QmSlotCid222222222222222222222222222222222222222",
                                   "label", "keeper", 0, 0));
        QVERIFY(r["ok"].toBool());
        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log[0].toObject()["slotFrom"].toInt(), 0);
    }

    void testConfirmInscriptionNewStatuses()
    {
        // Verify all lifecycle statuses persist correctly
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto pin = parseObj(p.pinCid("QmStateCid11111111111111111111111111111111111111", "x"));
        int idx = pin["entryIndex"].toInt();

        const QStringList states = {"queued","submitted","finalizing","confirmed","failed"};
        for (const QString& st : states) {
            QString id = (st == "confirmed") ? "aabbcc1234567890aabbcc1234567890aabbcc1234567890aabbcc1234567890" : "";
            auto r = parseObj(p.confirmInscription(idx, id, st));
            QVERIFY2(r["ok"].toBool(), qPrintable("status=" + st));
            auto log = parseArr(p.getInscriptionLog());
            QCOMPARE(log[idx].toObject()["status"].toString(), st);
        }
    }

    void testGetStatusCountsConfirmedAndOk()
    {
        // Both "confirmed" and legacy "ok" count toward inscribedCids
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        auto p1 = parseObj(p.pinCid("QmCountCid1111111111111111111111111111111111111", "a"));
        auto p2 = parseObj(p.pinCid("QmCountCid2222222222222222222222222222222222222", "b"));
        auto p3 = parseObj(p.pinCid("QmCountCid3333333333333333333333333333333333333", "c"));

        p.confirmInscription(p1["entryIndex"].toInt(), "aabb1234567890aabb1234567890aabb1234567890aabb1234567890aabb1234", "confirmed");
        p.confirmInscription(p2["entryIndex"].toInt(), "ccdd1234567890ccdd1234567890ccdd1234567890ccdd1234567890ccdd1234", "ok");
        p.confirmInscription(p3["entryIndex"].toInt(), "", "failed");

        auto st = parseObj(p.getStatus());
        QCOMPARE(st["inscribedCids"].toInt(), 2);  // confirmed + ok both count
        QCOMPARE(st["seenCids"].toInt(), 3);
    }

    void testGetNodeInfoReturnsErrorOnBadUrl()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);
        // Point at a definitely-unreachable URL
        p.setNodeUrl("http://127.0.0.1:19999");
        auto r = parseObj(p.getNodeInfo());
        QVERIFY(r.contains("error"));
        QVERIFY(!r["error"].toString().isEmpty());
    }

    void testFindExplorerTxHashRejectsEmptyChannelId()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);
        auto r = parseObj(p.findExplorerTxHash("", 4711799, 4711900));
        QCOMPARE(r["found"].toBool(), false);
        QCOMPARE(r["txHash"].toString(), QString());
    }

    void testFindExplorerTxHashRejectsZeroSlotFrom()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);
        auto r = parseObj(p.findExplorerTxHash("8edab686b441eac68b194445a5052b65812ed25d68abe582824cadab99d5bf31", 0, 100));
        QCOMPARE(r["found"].toBool(), false);
    }

    void testGetBlockForTxReturnsNotFoundForEmptyHash()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);
        auto r = parseObj(p.getBlockForTx("", 4711799));
        QCOMPARE(r["found"].toBool(), false);
        QCOMPARE(r["blockHash"].toString(), QString());
    }

    void testPinCidLogIncludesAllFields()
    {
        // Verify all expected fields are present in log entries
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        BeaconPlugin p;
        p.setProperty("instancePersistencePath", tmp.path());
        p.initLogos(nullptr);

        p.pinCid("QmFieldCid11111111111111111111111111111111111111", "my label", "keeper", 1234, 1000);
        auto log = parseArr(p.getInscriptionLog());
        QCOMPARE(log.size(), 1);
        QJsonObject e = log[0].toObject();

        QVERIFY(e.contains("cid"));
        QVERIFY(e.contains("label"));
        QVERIFY(e.contains("source"));
        QVERIFY(e.contains("ts"));
        QVERIFY(e.contains("status"));
        QVERIFY(e.contains("inscriptionId"));
        QVERIFY(e.contains("slotFrom"));
        QVERIFY(e.contains("libAtSubmit"));

        QCOMPARE(e["slotFrom"].toInt(), 1234);
        QCOMPARE(e["libAtSubmit"].toInt(), 1000);
        QCOMPARE(e["source"].toString(), QString("keeper"));
    }
};

QTEST_MAIN(TestBeaconPlugin)
#include "test_beacon_plugin.moc"
