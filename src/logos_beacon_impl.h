#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <logos_module_context.h>
#include <logos_result.h>

/**
 * @brief logos_beacon — universal (LogosModuleContext) form of the Beacon module.
 *
 * v0.2 migration of the legacy Qt BeaconPlugin (QObject/PluginInterface). The
 * public API is Qt-free (StdLogosResult / std::string); the battle-tested Qt
 * internals (QSettings config, QJson inscription log, QNetworkAccessManager node
 * queries) are preserved verbatim behind a pimpl in the .cpp — universal core
 * modules may use Qt internally (Qt6::Core+Network link via logos_module()).
 *
 * Adds the inscription bridge the QML used to do directly: seqDeriveChannel /
 * seqPublish forward to the Qt-free universal zone_sequencer via
 * modules().zone_sequencer (no getClient), so the whole publish path runs C++-side.
 */
class LogosBeaconImpl : public LogosModuleContext
{
public:
    LogosBeaconImpl();
    ~LogosBeaconImpl();

    // ── Config ────────────────────────────────────────────────────────────────
    StdLogosResult getBeaconConfig();
    StdLogosResult setNodeUrl(const std::string& url);
    StdLogosResult setWatchedSources(const std::string& newlineSeparated);
    StdLogosResult getWatchedSources();

    // ── State ─────────────────────────────────────────────────────────────────
    StdLogosResult getStatus();
    StdLogosResult getInscriptionLog();
    StdLogosResult clearInscriptionLog();

    // ── Node info ─────────────────────────────────────────────────────────────
    StdLogosResult getNodeInfo();

    // ── Inscription bookkeeping ────────────────────────────────────────────────
    StdLogosResult pinCid(const std::string& cid, const std::string& label,
                          const std::string& source, int64_t slotFrom, int64_t libAtSubmit);
    StdLogosResult deriveModuleSigningKey(const std::string& moduleName);
    StdLogosResult getModules();
    StdLogosResult ensureCheckpointsDir();
    StdLogosResult confirmInscription(int64_t entryIndex, const std::string& inscriptionId,
                                      const std::string& status);

    // ── Key management ─────────────────────────────────────────────────────────
    StdLogosResult setSigningKey(const std::string& hexKey);
    StdLogosResult clearSigningKey();

    // ── Manifest log ───────────────────────────────────────────────────────────
    StdLogosResult getManifestLog();
    StdLogosResult recordManifest(const std::string& moduleName);

    // ── Finalization / explorer lookups ────────────────────────────────────────
    // v0.2 confirmation via /channel/{id} → {found, tipSlot, tipMessage} (beacon#43)
    StdLogosResult getChannelState(const std::string& channelId);

    // ── zone_sequencer bridge (v0.2 — replaces QML's direct callModule) ─────────
    /// Derive the 64-hex channel id for a signing key via modules().zone_sequencer.
    StdLogosResult seqDeriveChannel(const std::string& signingKeyHex);
    // The channel a source (e.g. "keeper") inscribes to = derive_channel_id(SHA256(masterKey+source)).
    // Lets consumers (keeper log 'copy URL') build explorer.logos.live/#<channel> without re-deriving.
    StdLogosResult getSourceChannel(const std::string& source);
    /// Stateless publish to a channel via modules().zone_sequencer.publish_to.
    /// Returns the inscription/tx id string. Runs the whole cross-module hop in C++.
    /// `token` correlates the async caller's request with the publishCompleted event
    /// (the sync reply is unreliable on a long-running publish — beacon#50). The
    /// return value is retained for callers that can use it, but the authoritative
    /// result delivery is the publishCompleted event emitted when publish_to returns.
    StdLogosResult seqPublish(int64_t token, const std::string& nodeUrl, const std::string& channelId,
                              const std::string& signingKeyHex, const std::string& checkpointPath,
                              const std::string& payload);

    std::string name() const { return "logos_beacon"; }
    std::string version() const { return "2.0.0"; }

private:
    struct Impl;                     // Qt state (QSettings/QJson/QNetworkAccessManager)
    std::unique_ptr<Impl> d;
};
