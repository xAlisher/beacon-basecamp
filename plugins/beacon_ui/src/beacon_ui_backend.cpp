#include "beacon_ui_backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

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

QString BeaconUiBackend::setChannelLabel(QString label)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.setChannelLabel(label));
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
QString BeaconUiBackend::findExplorerTxHash(QString channelId, int slotFrom, int slotTo)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.findExplorerTxHash(
        channelId, slotFrom, slotTo));
}

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

QString BeaconUiBackend::seqPublish(QString nodeUrl, QString channelId, QString signingKeyHex,
                                    QString checkpointPath, QString payload)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.seqPublish(
        nodeUrl, channelId, signingKeyHex,
        checkpointPath, payload));
}

QString BeaconUiBackend::ping()
{
    return QStringLiteral("{\"ok\":true,\"module\":\"beacon_ui\",\"ctxReady\":%1}")
        .arg(isContextReady() ? QStringLiteral("true") : QStringLiteral("false"));
}
