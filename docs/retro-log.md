# beacon-basecamp — Retro Log

Append entries after completing each Epic or resolving a significant issue.

---

## 2026-04-20 — Initial implementation (Epics 1–5, Issues 1–10)

All issues implemented in a single session:

- Ed25519 key generation via `QRandomGenerator::system()`, stored as 64-char hex, mode 0600
- Inscription log persisted to `instancePersistencePath/inscription-log.json`
- QML zone sequencer wiring: `set_signing_key`, `set_node_url`, `set_checkpoint_path`, `get_channel_id`
- Stash log polling every 10s with `stashSeenCount` pattern and `pollBusy` re-entrancy guard
- Watch toggle persisted via QSettings `beacon/watchStash`
- Config panel: channel ID, signing key backup, node URL, watch toggle
- Inscription log panel: scrollable list with status dots, copy-to-clipboard
- 8 unit tests covering key gen, config, inscription log persistence, duplicate guard, confirm flow
- `--whole-archive` applied to `liblogos_sdk.a` in CMakeLists.txt

---

## 2026-04-20 — Build fixes (first compile)

Two failures hit during initial build, both resolved:

### 1. `logos_api_stub.cpp` — wrong `onEvent` signature

**Symptom:** Compiler error on `logos_api_stub.cpp`:
```
no declaration matches 'void LogosAPIClient::onEvent(QObject*, QObject*, const QString&, std::function<...>)'
candidate is: 'void LogosAPIClient::onEvent(LogosObject*, const QString&, std::function<...>)'
```

**Cause:** The stub was copied from `logos-node-basecamp` which was written against the old
`092zxk8q-logos-liblogos-headers` (no longer present on this machine). The current headers at
`8cgbzy0j-logos-liblogos-headers-0.1.0` changed `onEvent`'s first parameter from
`(QObject*, QObject*)` to `(LogosObject*)`.

**Fix:**
```cpp
// Before (old SDK):
void LogosAPIClient::onEvent(QObject*, QObject*, const QString&, ...)

// After (current SDK):
void LogosAPIClient::onEvent(LogosObject*, const QString&, ...)
```

### 2. `test_beacon_plugin` linker — `undefined reference to LogosAPI::staticMetaObject`

**Symptom:** Linker error from AUTOMOC-generated code:
```
mocs_compilation.cpp: undefined reference to `LogosAPI::staticMetaObject'
```

**Cause:** `BeaconPlugin.h` has `Q_INVOKABLE void initLogos(LogosAPI* api)`. Qt6's AUTOMOC
generates `MetaObjectForType<LogosAPI*>` code that references `LogosAPI::staticMetaObject`.
Without `liblogos_sdk.a` linked in the test, this symbol is absent.

**Fix:** Add `"${LOGOS_CPP_SDK}/include/cpp/logos_api.h"` as a source file to the test
executable in `CMakeLists.txt`. AUTOMOC processes it and generates `moc_logos_api.cpp`
which provides `staticMetaObject` without needing the full SDK library.

```cmake
add_executable(test_beacon_plugin
    tests/test_beacon_plugin.cpp
    tests/logos_api_stub.cpp
    src/plugin/BeaconPlugin.h
    src/plugin/BeaconPlugin.cpp
    "${LOGOS_CPP_SDK}/include/cpp/logos_api.h"   # ← provides LogosAPI::staticMetaObject
)
```

Both fixes also saved to `~/basecamp-skills/` and `~/.claude/projects/.../memory/`.

---

---

## 2026-04-20 — Runtime fix: logosAPI base class member

**Symptom:** `logos_beacon` logos_host spawned but module never received a capability token;
platform crash-loop when Beacon sidebar tab was opened.

**Cause:** `BeaconPlugin` declared `LogosAPI* m_api = nullptr;` and stored `m_api = api` in
`initLogos`. The base class `PluginInterface::logosAPI` was never populated. `ModuleProxy`
checks `pluginInterface->logosAPI` directly — null pointer → IPC crash.

Per `initlogos-no-override` and `logosapi-member-no-redeclare` skills:
- Remove private `m_api` member entirely
- In `initLogos`, write `logosAPI = api` (base class public member)

**Fix (BeaconPlugin.h):**
```cpp
// REMOVED:
// LogosAPI* m_api = nullptr;
```

**Fix (BeaconPlugin.cpp):**
```cpp
// Before:
m_api = api;
// After:
logosAPI = api;
```

---

## fail 2026-04-20 — beacon_ui slow load: blocking zone_seq calls in Component.onCompleted

**Symptom:** Clicking the Beacon sidebar tab showed a loading spinner for ~3 minutes before the UI appeared.

**Cause:** `configureZoneSeq()` is called from `Component.onCompleted`. It makes 4 synchronous `logos.callModule("liblogos_zone_sequencer_module", ...)` calls. When zone_seq is not running, each call blocks for ~40s (20s timeout × retry). Also `pollStash()` fires on the 10s timer and blocks 20s waiting for stash. Total: up to 3 minutes of blocked QML main thread before the loading spinner clears.

**Platform context:** `PluginLoader::loadUiQmlModule` calls `finishUiQmlLoad` after async QML precompile. `finishUiQmlLoad` calls `qmlWidget->setSource()` which runs `Component.onCompleted` synchronously. Blocking calls in `onCompleted` stall `setSource()` until they return.

**Not yet fixed** — UI eventually loads after timeouts expire. Fix would be to check if zone_seq is available before calling (or defer via `Qt.callLater`).

---

## fail 2026-04-20 — pkill pattern with `\|` not working as OR operator

**Symptom:** `pkill -9 -f "logos\|Logos\|basecamp"` reported exit code 1 (no matches) even though Logos processes were running.

**Cause:** pkill uses Extended Regular Expressions (ERE). In ERE, `\|` is a literal pipe character, not alternation. Alternation requires bare `|` without backslash.

**Fix:** Use `pkill -9 -f "logos|Logos|basecamp"` (no backslash) or kill PIDs directly.

---

## win 2026-04-20 — set_channel_id missing from configureZoneSeq()

`get_channel_id` derives the channel ID from the signing key but does NOT initialize the sequencer for publishing. `publish()` requires a separate `set_channel_id(derivedId)` call first — otherwise returns "Error: sequencer not initialized (call set_channel_id first)". Added `set_channel_id` call after `get_channel_id` in `configureZoneSeq()`. First real inscription confirmed: `01e7db8c...`

---

## win 2026-04-20 — full decentralised backup loop closed

Notes → Stash → Beacon (LEZ inscription). Zone-seq init required `set_channel_id` after `get_channel_id`; C++ signals don't bridge to QML across IPC; ghost `logos_host` instances steal IPC calls from live keycard.

---

**Known gaps for follow-up:**
- Retry queue for failed inscriptions (node down at inscription time)
- Keycard integration once Ed25519 derivation lands in keycard-basecamp
- Icon: placeholder 1×1 PNG — replace with proper 28×28 design
- configureZoneSeq() slow load: defer blocking zone_seq calls out of Component.onCompleted
- Issue #11: Real-time log update on inscription confirm — implemented via direct logModel.setProperty in QML (signal bridge approach abandoned)
- Issue #12 (logos-notes): Show beacon inscription events in notes activity log — when a note backup is inscribed, notes should append an activity entry: "beacon backup {name} with CID {cid} successfully inscribed to {channel} on LEZ, status: Confirmed". Requires beacon to either emit a cross-module event or notes to poll beacon's getInscriptionLog(). — currently the log row goes straight from absent → confirmed (pending state never shown in UI). Options: (a) append a pending row to logModel immediately in QML before calling publish, then update in-place on confirm; (b) emit a C++ `inscriptionConfirmed(int entryIndex, QString inscriptionId, QString status)` signal that QML listens to for targeted row updates without full refresh. Option (b) is cleaner — avoids full re-read on every inscription and allows showing the pending→ok transition live.

## win 2026-04-24
Beacon keycard integration (PR #2) fully merged. keycardConnected guard prevents premature stash polling, clearSigningKey() ensures backend state resets on card removal, cardCheckTimer detects removal via keycard.getState(). Senty found 2 HIGHs + 1 MEDIUM, all addressed in 2 fix commits.

## win 2026-04-24
Builder-auditor loop worked well despite Codex 403 on GitHub comments — Senty findings delivered inline, fixes applied same session, re-review same session, LGTM round 2.

## fail 2026-04-24
Senty 403 on GitHub comment: beacon-basecamp PR #2 review could not be posted automatically (Resource not accessible by integration). Findings were returned inline instead.

## fail 2026-04-24
Senty 403 repeated on beacon-basecamp PR #2 round 2 comment. GitHub integration token lacks write access to xAlisher/beacon-basecamp. Findings delivered inline again.

## win 2026-04-28
Keycard reinsertion auth fixed end-to-end. Root cause: CommandSet::select() had an m_appInfo.installed cache that was never cleared on card removal. onTargetLost() only called resetSecureChannel() — not clearStaleCardSessionState() — so m_appInfo.installed stayed true. On reinsertion, select() returned cached appInfo without sending SELECT APDU, then OPEN_SECURE_CHANNEL fired against unselected card → SW=6D00. Fix: clear m_appInfo and m_cardInstanceUID in both clearStaleCardSessionState() and onTargetLost().

## win 2026-04-28
Fixed EDEADLK crash in CommunicationManager::executeCommandSync(). The if/else wait branches held m_syncMutex via QMutexLocker, then fell through to a shared "check final result" block that tried to lock m_syncMutex again → std::system_error "Resource deadlock avoided". Fix: moved result extraction and m_pendingSync.remove() inside each branch while the lock is already held.

---

## win 2026-04-30 — issue #12: source field in cid_pin payload + full demo polish

PR #6 merged. `pinCid(cid, label, source)` 3-arg C++ invokable live; source stored in log entry and included in payload. Source routing: stash entries without explicit source map to "notes" channel. No-channel fallback to primary instead of hard error. Log format rewritten to show full provenance chain `[notes → stash → Logos Storage]`. Removed `●` noise from all activity/log entries. Updated description. All fixes committed to source so `cmake --install` no longer clobbers them.

## fail 2026-04-30 — cmake --install silently overwrote hand-edited installed QML

Applied four QML fixes directly to installed `Main.qml`, then ran `cmake --install` to deploy the rebuilt beacon .so. Install overwrote all QML fixes. Had to re-apply all four patches. Fix: always patch source QML first; if patching installed file during live demo, immediately copy back to source.

## win 2026-04-30 — issue #7: defer inscribeManifest past sequencer async init window

PR #8 merged. Root cause: `tryCreateSequencer()` in zone_seq uses `QtConcurrent::run()` —
`m_sequencerHandle` is null for hundreds of ms after `configureZoneSeq()` returns. Moving
`inscribeManifest` from `setupModuleChannel` (immediately post-config) to `inscribeCid`
(after first successful `publish()`) fixed the timing. Sequencer proved live by publish
success; re-derive guard `!manifestedModules[source]` keeps it idempotent. Extracted to
`zone-seq-async-init-defer-publish` skill.

## fail 2026-04-30 — pinCid "Invalid response" from installed .so with 2-arg signature

QML calling `pinCid(cid, label, source)` (3 args) against installed .so that only registered 2-arg `pinCid(cid, label)`. Qt bridge silently rejected the call. Symptom: `callModuleParse` returned null → logged "error: pinCid Invalid response". Fix: rebuild + reinstall .so. Root cause: issue #12 was implemented in source but the installed .so was never updated in the previous session.

## win 2026-05-21
fix: beacon aligned with logos-module-builder b3f1d658 + RC3 SDK

nixpkgs.follows required `...` in outputs destructuring (was erroring "unexpected argument 'nixpkgs'"). CMakeLists.txt hardcoded Nix store paths (logos-cpp-sdk + liblogos-headers) go stale after module-builder update — updated to new hashes. logos_api_stub.cpp: added logos_object.h include; dropped onEvent stub (BeaconPlugin doesn't use it, avoids SDK signature drift). 25/25 unit tests pass on Qt 6.9.3. Verified live — modules load, inscription flow works.

## fail 2026-06-10
repeated double-instance: kill sequence not terminating all AppImage children before relaunch; need to fix kill approach per module-kill-and-relaunch skill

## win 2026-06-10
testnet explorer lag root cause found: ~54h behind chain. Fixed findExplorerTxHash to fall back to blockHeaderId when explorer returns 404. Cypherpunk Manifesto confirmed. Links will show proper tx hash once explorer catches up (~June 12).
