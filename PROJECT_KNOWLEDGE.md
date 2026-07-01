# beacon-basecamp — Project Knowledge

Accumulated lessons specific to this codebase. See `docs/retro-log.md` for raw captures.
Platform-wide patterns live in `~/basecamp/basecamp-skills/skills/`.

---

## Explorer Hash Lookup

### findExplorerTxHash — 2-step approach (confirmed working 2026-06-07)

The `BeaconPlugin::findExplorerTxHash(channelId)` C++ method uses a 2-step approach
instead of relying on the `zone_sequencer_module.publish()` return value:

**Step 1** — Scan node blocks for the inscription block:
```
GET /cryptarchia/blocks?from_slot={libAtSubmit}&to_slot={libAtSubmit+1000}
→ find block where operations[].content.channel_id == channelId
→ extract blockHeaderId
```

**Step 2** — Get real tx hash from explorer block API:
```
GET {explorerBaseUrl}/web/explorer/api/v1/fork-choice  → get fork ID
GET {explorerBaseUrl}/web/explorer/api/v1/blocks/{blockHeaderId}?fork={forkId}
→ find tx where operations[].content.channel_id == channelId
→ return transactions[].hash
```

This is robust regardless of what `zone_sequencer_publish()` returns — the explorer
hash is what the explorer actually indexes.

### Explorer "HTTP 500" is a frontend bug

The explorer frontend shows "Error: HTTP 500" when the `block_id` field in
`/transactions/{hash}` response is null. The underlying API returns HTTP 200 with
valid data. Use the raw API endpoints to verify inscriptions, not the frontend UI.

---

## Zone Sequencer

### publish() return value = mantle_tx.hash (via Logos module)

`liblogos_zone_sequencer_module.publish()` called from QML returns `mantle_tx.hash`
from the Logos platform module — this IS the correct explorer hash for URLs built
from `setProperty("basecamp_data_uri", ...)` patterns. This matches the explorer's
indexed hash.

The Python C FFI `zone_sequencer_publish()` behaves differently — see
`zone-publish-hash-type-mismatch` in basecamp-skills for details.

### Sequencer checkpoint

The sequencer saves state to `instancePersistencePath/beacon.checkpoint`.
On startup with a valid checkpoint, `configureZoneSeq()` resumes from the last slot.
With an empty or missing checkpoint, it bootstraps to current LIB (~5s).

For one-shot scripts: always pass empty checkpoint to avoid backfill deadlock.
For the long-running beacon service: the checkpoint is correct behavior.

---

## Inscription Lifecycle (feat/inscription-lifecycle)

### pendingFinalizations tracking

Inscriptions in-progress are tracked in `pendingFinalizations` array in QML:
```qml
property var pendingFinalizations: []
// Each entry: {cid, label, source, slotFrom, libAtSubmit, ts}
```

`finalizationTimer` polls `getInscriptionLog()` for each pending CID until
`txHash` is non-empty, then updates the logModel in-place.

### useCheckpoint flag

`pinCid()` accepts an optional `useCheckpoint` boolean. When `true`, the sequencer
resumes from the saved checkpoint. When `false`, it bootstraps fresh. Pass `true`
for the normal auto-watch path (keeps checkpoint continuity). The stored QSettings
value `beacon/useCheckpoint` defaults to `true`.

### pollBusy guard is mandatory

`finalizationTimer` calls `callModule` in a loop over `pendingFinalizations`. Without
the `pollBusy` guard, the timer re-enters while a `callModule` is still blocking:

```qml
property bool pollBusy: false
function pollFinalizations() {
    if (root.pollBusy) return
    root.pollBusy = true
    // ... callModule loop ...
    root.pollBusy = false
}
```

---

## Build

### logos_api_stub.cpp — correct onEvent signature

The stub must use `LogosObject*` (current SDK), not the old `QObject*, QObject*` form:
```cpp
void LogosAPIClient::onEvent(LogosObject*, const QString&, std::function<...>)
```

### logos_api.h as test executable source

Add `"${LOGOS_CPP_SDK}/include/cpp/logos_api.h"` as a source in `CMakeLists.txt`
for the test target to provide `LogosAPI::staticMetaObject` via AUTOMOC without
linking the full SDK library.

---

## Key Generation

Ed25519 signing key stored at `instancePersistencePath/beacon.key` as 64-char hex,
mode 0600. Generated via `QRandomGenerator::system()` on first run.

Channel ID derived via `get_channel_id(signingKey)` from `liblogos_zone_sequencer_module`.
`set_channel_id()` must be called after `get_channel_id()` before `publish()` works.

---

## Known Pitfalls

- **`set_channel_id` required before publish**: `get_channel_id` derives the channel
  but does NOT initialize the sequencer. Call `set_channel_id(derivedId)` after.
- **configureZoneSeq() blocks in Component.onCompleted**: 4 sync callModule calls
  each timeout for ~40s if zone_seq isn't ready. Mitigated by the keycardConnected
  guard (only configure after keycard auth).
- **cmake --install overwrites QML**: Always patch source QML first; `cmake --install`
  copies source to installed path, overwriting any hand-edits to the installed file.
- **pinCid arg count mismatch**: Installed .so must match QML call signature.
  After adding the `source` arg to `pinCid`, always rebuild + reinstall.

## v0.2 universal architecture (2026-07-01)

The whole chain is now Qt-free universal on typed `modules()` — no getClient, no legacy callModule:
`beacon_ui (ui_qml QtRO backend) → modules().logos_beacon (universal, Qt behind a pimpl) → modules().zone_sequencer (universal, Rust FFI) → node`. Proven end-to-end (inscription_id e49c7a37…).

- **beacon_ui** is a `ui_qml` module with a C++ QtRO backend (`.rep` + `LogosUiPluginContext`), deps `[logos_beacon]`. QML calls `logos.module("beacon_ui")` + `logos.watch(backend.M(args), ok, err)`. `keycard` stays on `logos.callModule` (separate legacy Qt module, reachable). See skill `qml-to-universal-module-qtro-backend`.
- **logos_beacon** is `LogosBeaconImpl : LogosModuleContext` (was Qt BeaconPlugin). Methods return `StdLogosResult` whose `value` is a JSON object/string; the backend serializes it back to a JSON string (`resultToJson`) so the QML keeps parsing with `callModuleParse`. `seqDeriveChannel`/`seqPublish` forward to `modules().zone_sequencer` — the inscription runs C++-side. See skill `universal-core-module-qt-behind-pimpl`.
- **Node URL trap:** beacon's stored `nodeUrl` was Sneg's tailnet IP `100.108.127.3:8080` — the node is **localhost-only**, so that's unreachable. Must be `http://127.0.0.1:8080` (the SSH tunnel, `-L 8080` NOT 18080). A wrong URL makes the sequencer log `Failed to fetch time info … / timeout waiting for sequencer ready`.
- **Silent-failure bug (#32, fixed):** the seqPublish handler's legacy ">18s error → assume submitted, defer to finalization" heuristic is WRONG under `logos.watch` (which returns the real result). An error is now surfaced as `publish failed: <reason>`.
- **Copy-log button (#33, fixed):** was an `Image { source:"icons/Copy.svg" }` — that asset isn't bundled in the `.lgx` → invisible. Now a text "Copy" button. See skill `lgx-bundles-only-view-and-metadata-icon`.
