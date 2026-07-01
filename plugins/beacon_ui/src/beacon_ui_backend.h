#pragma once

#include "rep_beacon_ui_source.h"     // generated from src/beacon_ui.rep (repc)
#include "logos_ui_plugin_context.h"  // modules() + onContextReady()

/**
 * @brief beacon_ui backend — bridges QML to the universal logos_beacon module.
 *
 * QML (logos.module("beacon_ui") + logos.watch) → this backend → the Qt-free
 * universal logos_beacon via modules().logos_beacon.*. Replaces beacon's legacy
 * logos.callModule("logos_beacon"/"liblogos_zone_sequencer_module", ...) path,
 * which returns "null" for universal modules (beacon#30). Every SLOT forwards to
 * the same-named logos_beacon method and returns its result as a JSON string.
 * Pattern proven by logos-zone-sequencer-module/doctests/ui-backend-fixture.
 */
class BeaconUiBackend : public BeaconUiSimpleSource,
                        public LogosUiPluginContext
{
public:
    // ── Config ──────────────────────────────────────────────────────────────
    QString getBeaconConfig() override;
    QString setNodeUrl(QString url) override;
    QString setWatchedSources(QString sources) override;
    QString getWatchedSources() override;
    QString setChannelLabel(QString label) override;

    // ── State ───────────────────────────────────────────────────────────────
    QString getStatus() override;
    QString getInscriptionLog() override;
    QString clearInscriptionLog() override;

    // ── Node info ─────────────────────────────────────────────────────────────
    QString getNodeInfo() override;

    // ── Inscription bookkeeping ───────────────────────────────────────────────
    QString pinCid(QString cid, QString label, QString source,
                   int slotFrom, int libAtSubmit) override;
    QString deriveModuleSigningKey(QString moduleName) override;
    QString getModules() override;
    QString ensureCheckpointsDir() override;
    QString confirmInscription(int entryIndex, QString inscriptionId,
                               QString status) override;

    // ── Key management ────────────────────────────────────────────────────────
    QString setSigningKey(QString hexKey) override;
    QString clearSigningKey() override;

    // ── Manifest log ──────────────────────────────────────────────────────────
    QString getManifestLog() override;
    QString recordManifest(QString moduleName) override;

    // ── Finalization / explorer lookups ───────────────────────────────────────
    QString findExplorerTxHash(QString channelId, int slotFrom, int slotTo) override;

    // ── zone_sequencer bridge (forwarded through logos_beacon) ────────────────
    QString seqDeriveChannel(QString signingKeyHex) override;
    QString seqPublish(QString nodeUrl, QString channelId, QString signingKeyHex,
                       QString checkpointPath, QString payload) override;

    QString ping() override;

protected:
    void onContextReady() override { setStatus(QStringLiteral("logos_beacon wired")); }
};
