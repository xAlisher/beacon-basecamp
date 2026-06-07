#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QJsonArray>
class QNetworkAccessManager;

#include "interface.h"

class BeaconPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.BeaconModuleInterface" FILE "plugin_metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit BeaconPlugin(QObject* parent = nullptr);

    QString name()    const override { return QStringLiteral("logos_beacon"); }
    QString version() const override { return QStringLiteral("1.0.0"); }

    Q_INVOKABLE void initLogos(LogosAPI* api);

    // ── Config ───────────────────────────────────────────────────────────────
    // Returns {signingKeyHex, nodeUrl, watchStash:bool, persistencePath, channelLabel}
    Q_INVOKABLE QString getBeaconConfig() const;

    // Persists beacon/nodeUrl. Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString setNodeUrl(const QString& url);

    // Persists beacon/watchStash. Returns {"ok":true}
    Q_INVOKABLE QString setWatchStash(bool enabled);

    // Per-source whitelist for stash-watch auto-inscription.
    // newlineSeparated: "notes\nkeycard" — only these sources are auto-inscribed.
    // Default: ["notes"]. Returns {"ok":true}.
    Q_INVOKABLE QString setWatchedSources(const QString& newlineSeparated);
    // Returns {sources:["notes","keycard"]}
    Q_INVOKABLE QString getWatchedSources() const;

    // Persists beacon/channelLabel. Returns {"ok":true}
    Q_INVOKABLE QString setChannelLabel(const QString& label);

    // ── State ────────────────────────────────────────────────────────────────
    // Returns {configured:bool, seenCids:N, inscribedCids:N}
    Q_INVOKABLE QString getStatus() const;

    // Returns JSON array [{cid, inscriptionId, label, source, ts, status,
    //   slotFrom, libAtSubmit}], last 100 entries.
    Q_INVOKABLE QString getInscriptionLog() const;

    // ── Node info ────────────────────────────────────────────────────────────
    // Queries GET {nodeUrl}/cryptarchia/info.
    // Returns {"slot":N,"lib_slot":N,"mode":"Online"} or {"error":"..."}
    Q_INVOKABLE QString getNodeInfo();

    // ── Inscription ──────────────────────────────────────────────────────────
    // Checks for duplicate, appends pending entry, saves.
    // slotFrom: node slot at time of inscription (for finalization progress).
    // libAtSubmit: lib_slot at time of inscription (progress bar start).
    // Returns {"ok":true,"entryIndex":N} | {"ok":true,"duplicate":true} | {"error":"..."}
    Q_INVOKABLE QString pinCid(const QString& cid, const QString& label,
                               const QString& source = "",
                               int slotFrom = 0, int libAtSubmit = 0);

    // Derives a per-module signing key via SHA256(master_key || module_name).
    // Returns {"signingKey":"<64-char-hex>"} or {"error":"key not set"}
    Q_INVOKABLE QString deriveModuleSigningKey(const QString& moduleName);

    // Scans inscription log, groups by source, returns [{name, cidCount, lastTs}].
    Q_INVOKABLE QString getModules();

    // Creates {persistencePath}/checkpoints/ directory. Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString ensureCheckpointsDir();

    // Called from QML to update entry status at any lifecycle stage.
    // status: "submitted" | "finalizing" | "confirmed" | "failed" | "error"
    // inscriptionId: explorer TX hash when status="confirmed", empty otherwise.
    // Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString confirmInscription(int entryIndex,
                                           const QString& inscriptionId,
                                           const QString& status);

    // Called from QML after keycardAuthComplete — sets m_signingKeyHex (32-byte hex).
    // Returns {"ok":true} or {"error":"..."}
    Q_INVOKABLE QString setSigningKey(const QString& hexKey);

    // Called from QML on card removal or auth restart — clears m_signingKeyHex.
    // Returns {"ok":true}
    Q_INVOKABLE QString clearSigningKey();

    // Clears the in-memory inscription log and removes inscription-log.json. Returns {"ok":true}.
    Q_INVOKABLE QString clearInscriptionLog();

    // Debug: appends msg to /tmp/beacon_plugin.diag with timestamp. Returns {"ok":true}.
    Q_INVOKABLE QString diagLog(const QString& msg);

    // Returns JSON array of module names already inscribed to the manifest channel.
    Q_INVOKABLE QString getManifestLog() const;

    // Appends moduleName to manifest-log.json. Returns {"ok":true} or {"error":"..."}.
    Q_INVOKABLE QString recordManifest(const QString& moduleName);

    // Searches recent finalized blocks for a ChannelInscribe tx targeting channelId.
    // Returns {"txHash":"<hex>"} if found within [slotFrom, slotTo], {"txHash":""} if not.
    Q_INVOKABLE QString findAnchorTx(const QString& nodeUrl,
                                     const QString& channelId,
                                     int slotFrom,
                                     int slotTo);

    // Scans node blocks [slotFrom, lib_slot] for a ChannelInscribe tx matching channelId,
    // then queries explorer /blocks/{blockId}?fork=N to get the real explorer TX hash.
    // slotFrom: lower bound for block scan — pass the slot recorded at inscription time.
    // Returns {"txHash":"<hex>","blockHash":"<hex>","found":true} or {"found":false,...}
    Q_INVOKABLE QString findExplorerTxHash(const QString& channelId,
                                            int slotFrom, int slotTo);

    // Searches finalized blocks [slotFrom, lib_slot] for a tx whose mantle_tx.hash equals txHash.
    // slotFrom: slot recorded at inscription time — prevents scanning the entire chain.
    // Returns {"blockHash":"<header.id>","found":true} or {"blockHash":"","found":false}.
    Q_INVOKABLE QString getBlockForTx(const QString& txHash, int slotFrom = 0);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);
    void inscriptionConfirmed(int entryIndex, const QString& inscriptionId,
                              const QString& status);

private:
    void     loadLog();
    void     saveLog();
    QString  nodeUrl() const;
    QString  explorerBaseUrl() const;

    static QString errorJson(const QString& msg);
    static QString okJson();

    QString     m_persistencePath;
    QString     m_signingKeyHex;
    QJsonArray  m_log;             // in-memory inscription log
    QStringList m_watchedSources;  // sources auto-inscribed from stash events

    QNetworkAccessManager* m_nam = nullptr;
};
