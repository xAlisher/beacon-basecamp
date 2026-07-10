#include "beacon_ui_backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QVariantList>

#include "logos_sdk.h"   // generated: modules().logos_beacon (Qt-typed)

// Map a Qt LogosResult to the QString contract QML parses (callModuleParse):
//  - failure          → {"error": "..."}  (QML checks parsed.error / "error" prefix)
//  - scalar/string    → the raw value string (e.g. seqDeriveChannel's channel id)
//  - object/array     → compact JSON of the value
// Errors resolve through logos.watch's SUCCESS callback (the bridge only rejects on
// QtRO transport failure), so QML inspects the returned string, not the reject path.
static QString resultToJson(const LogosResult& r)
{
    if (!r.success) {
        QJsonObject o;
        o["error"] = r.getError();
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    QVariant v = r.value;                               // QVariant holding the JSON value
    if (v.typeId() == QMetaType::QString)
        return v.toString();                            // scalar/string value
    QJsonDocument doc = QJsonDocument::fromVariant(v);
    return doc.isNull() ? r.getString()
                        : QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

// ── Config ──────────────────────────────────────────────────────────────────────
QString BeaconUiBackend::getBeaconConfig()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getBeaconConfig());
}

QString BeaconUiBackend::setNodeUrl(QString url)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.setNodeUrl(url));
}

QString BeaconUiBackend::setWatchedSources(QString sources)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.setWatchedSources(sources));
}

QString BeaconUiBackend::getWatchedSources()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getWatchedSources());
}

// ── State ─────────────────────────────────────────────────────────────────────
QString BeaconUiBackend::getStatus()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getStatus());
}

QString BeaconUiBackend::getInscriptionLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getInscriptionLog());
}

QString BeaconUiBackend::clearInscriptionLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.clearInscriptionLog());
}

// ── Node info ─────────────────────────────────────────────────────────────────
QString BeaconUiBackend::getNodeInfo()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getNodeInfo());
}

// ── Inscription bookkeeping ──────────────────────────────────────────────────────
QString BeaconUiBackend::pinCid(QString cid, QString label, QString source,
                                int slotFrom, int libAtSubmit)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.pinCid(
        cid, label, source,
        slotFrom, libAtSubmit));
}

QString BeaconUiBackend::deriveModuleSigningKey(QString moduleName)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.deriveModuleSigningKey(moduleName));
}

QString BeaconUiBackend::getModules()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getModules());
}

QString BeaconUiBackend::ensureCheckpointsDir()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.ensureCheckpointsDir());
}

QString BeaconUiBackend::confirmInscription(int entryIndex, QString inscriptionId,
                                            QString status)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.confirmInscription(
        entryIndex, inscriptionId, status));
}

// ── Key management ────────────────────────────────────────────────────────────────
QString BeaconUiBackend::setSigningKey(QString hexKey)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.setSigningKey(hexKey));
}

QString BeaconUiBackend::clearSigningKey()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.clearSigningKey());
}

// ── Manifest log ────────────────────────────────────────────────────────────────
QString BeaconUiBackend::getManifestLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getManifestLog());
}

QString BeaconUiBackend::recordManifest(QString moduleName)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.recordManifest(moduleName));
}

// ── Finalization / explorer lookups ──────────────────────────────────────────────
QString BeaconUiBackend::getChannelState(QString channelId)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getChannelState(channelId));
}

// ── zone_sequencer bridge (forwarded through logos_beacon) ────────────────────────
QString BeaconUiBackend::seqDeriveChannel(QString signingKeyHex)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.seqDeriveChannel(signingKeyHex));
}

QString BeaconUiBackend::seqPublish(int token, QString nodeUrl, QString channelId, QString signingKeyHex,
                                    QString checkpointPath, QString payload)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    // FIRE-AND-FORGET (beacon#50) + LONG TIMEOUT. A fresh-channel publish on a long
    // chain runs for minutes (zone_sequencer's 3×60s "sequencer ready" retry loop, #51).
    //   1. Async so the sync call never parks the single-threaded ui-host loop.
    //   2. Timeout MUST exceed the publish duration: the crash is logos_beacon SIGSEGV
    //      when publish_to RETURNS (e.g. at 93s) and it replies into a request context
    //      the framework already tore down at the DEFAULT 20s timeout (Timeout=20000ms).
    //      logos_beacon's own modules().zone_sequencer.publish_to() sync call tolerates
    //      the full duration — it is specifically this ui-host→core hop's 20s timeout
    //      that frees the context early. A generous timeout keeps it alive until the
    //      reply lands. (The headless zone-sequencer-rs harness never hits this because
    //      it has no such timed IPC hop wrapping the publish — see beacon#50.)
    // The result is delivered by logos_beacon's publishCompleted event (onContextReady)
    // → seqPublishResult, correlated by `token`; the async reply is not relied on.
    // Deliver the result from the ASYNC REPLY CALLBACK (beacon#50). The core must NOT
    // emit a logos_events event to report the result — that SIGSEGVs logos_beacon at
    // publish-return (proven by A/B). Instead we ride the normal reply, which now lands
    // because the request context lives long enough (15 min) for the slow fresh-channel
    // publish to return. The callback runs on the ui-host thread, so emitting the
    // QtRO seqPublishResult signal from here is safe. `token` correlates it in QML.
    // Bounds how long a pending publish request is held. Must exceed the slowest valid
    // publish (fresh-channel 3×60s sequencer-ready retry ≈ 180s) with margin, but stay
    // small enough that a crashed/never-returning publish frees the ui-side guard
    // (pollBusy) promptly instead of freezing the pipeline (beacon#50 guard-recovery).
    static constexpr int kPublishTimeoutMs = 240000;  // 4 min
    modules().logos_beacon.seqPublishAsync(
        token, nodeUrl, channelId, signingKeyHex, checkpointPath, payload,
        [this, token](LogosResult r) { emit seqPublishResult(token, resultToJson(r)); },
        Timeout(kPublishTimeoutMs));
    return QStringLiteral("{\"ok\":true,\"token\":%1}").arg(token);
}

void BeaconUiBackend::onContextReady()
{
    setStatus(QStringLiteral("logos_beacon wired"));
}

QString BeaconUiBackend::ping()
{
    return QStringLiteral("{\"ok\":true,\"module\":\"beacon_ui\",\"ctxReady\":%1}")
        .arg(isContextReady() ? QStringLiteral("true") : QStringLiteral("false"));
}


// keeper is now universal — callModule returns null (beacon#46), so reach it via modules().keeper.
// Universal deps are SYNC-safe from a ui-host backend.
QString BeaconUiBackend::getKeeperInscriptionQueue()
{
    return resultToJson(modules().keeper.getInscriptionQueue());
}

QString BeaconUiBackend::markKeeperInscribed(QString cid)
{
    return resultToJson(modules().keeper.markInscribed(cid));
}
