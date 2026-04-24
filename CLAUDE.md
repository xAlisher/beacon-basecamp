# beacon-basecamp — Claude Code Instructions

> Read `docs/plans/beacon-implementation.md` first. It contains the implementation plan,
> issue breakdown, and verification steps.

## Identity & Protocols

You are **Fergie**. Protocols load via `.claude/rules/`. tmux-bridge labels:
`fergie@beacon-basecamp`, `senty@beacon-basecamp`.

**Alisher sign-off required for:**
- Destructive operations (rm -rf, force push, drop QSettings)
- API contract changes visible to other modules (e.g. `getInscriptionLog` return format)
- Major architectural pivots

Everything else: agents handle autonomously.

---

## Project Context

**beacon-basecamp** — Watches stash for newly uploaded CIDs and inscribes them into a
dedicated LEZ zone channel, creating a permanent on-chain index.

- Auto-generates an Ed25519 signing key on first run (stored in `instancePersistencePath/beacon.key`)
- Channel derived from that key via `liblogos_zone_sequencer_module.get_channel_id`
- Polls stash `getLog()` every 10s for new CIDs (type="uploaded" or e.cid present)
- Inscribes each new CID as `{v:1, type:"cid_pin", cid, label, ts}` via zone sequencer

**Sibling modules this integrates with:**
- `stash-basecamp` — reads getLog() to detect uploaded CIDs
- `liblogos_zone_sequencer_module` — publishes inscription payloads to LEZ chain
- `logos-node-basecamp` — node URL stored in QSettings, not read directly from node module

---

## Code Style & Patterns

### Q_INVOKABLE — always return JSON strings

```cpp
Q_INVOKABLE QString getStatus() {
    QJsonObject o;
    o["configured"] = true;
    o["seenCids"]   = 3;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
```

Never return `bool`, `int`, or QVariant — they don't cross the QML bridge reliably.

### QSettings namespace: `beacon/`

```cpp
static constexpr const char* kNodeUrlKey    = "beacon/nodeUrl";
static constexpr const char* kWatchStashKey = "beacon/watchStash";
```

### Persistence path

Always use `instancePersistencePath` (injected by platform via `setProperty`):

```cpp
QVariant prop = property("instancePersistencePath");
m_persistencePath = prop.isValid() ? prop.toString() : fallback;
```

Files stored there:
- `beacon.key` — Ed25519 seed (64-char hex), mode 0600
- `beacon.checkpoint` — zone sequencer chain checkpoint
- `inscription-log.json` — persisted inscription history

### pollBusy guard (qml-callmodule-reentrancy-guard skill)

All QML Timer callbacks that call `callModule` must be guarded:

```qml
property bool pollBusy: false
function pollStash() {
    if (root.pollBusy) return
    root.pollBusy = true
    // ... do work ...
    root.pollBusy = false
}
```

### callModuleParse — three-layer form (callmoduleparse-canonical-form skill)

```javascript
function callModuleParse(raw) {
    try {
        var tmp = JSON.parse(raw)
        if (typeof tmp === 'string') {
            try { return JSON.parse(tmp) } catch(e) { return tmp }
        }
        return tmp
    } catch(e) { return null }
}
```

---

## Build & Test Workflow

```bash
# Build
cmake -B build && cmake --build build -j$(nproc)

# Test
cd build && ctest --output-on-failure

# Install (LogosApp + mirrors to LogosBasecamp)
cmake --install build

# Kill + relaunch Basecamp
pkill -9 -f "LogosBasecamp.elf"; sleep 1
~/logos-basecamp-current.AppImage &
```

---

## Module Install Paths

```
~/.local/share/Logos/LogosApp/
├── modules/logos_beacon/
│   ├── beacon_plugin.so
│   ├── manifest.json / metadata.json / plugin_metadata.json / variant
└── plugins/beacon_ui/
    ├── Main.qml / manifest.json / metadata.json / variant
    └── icons/Beacon_sidebar.png
```

CMake install also mirrors to `LogosBasecamp/` automatically.

---

## Future Epic: Keycard Integration

When `keycard-basecamp` exposes Ed25519 key derivation:
1. Delete `ensureKey()` and `beacon.key` file
2. Replace key acquisition with:
   `logos.callModule("keycard", "deriveEd25519Key", ["logos:beacon"])`
3. All other flows unchanged

---

## Issue #12 — Add `source` field to `cid_pin` payload

**Goal:** Let subscribers (Cord, future modules) know which Basecamp module a CID originated from.

**Change:** Add a `source` field to the `cid_pin` inscription payload:

```json
{"v":1, "type":"cid_pin", "cid":"baf...", "label":"...", "source":"logos_notes", "ts":1234567890}
```

**Where `source` comes from:**
- Stash calls `pinCid(cid, label)` — extend to `pinCid(cid, label, source)`
- `source` is the calling module name, e.g. `"logos_notes"`, `"stash"`, `"logos_keycard"`
- Default to `""` if not provided (backward compatible)

**C++ change:** `pinCid(cid, label, source = "")` — store `source` in the log entry and include it in the payload passed back to QML.

**QML change:** include `source` in the JSON payload constructed in `inscribeCid()`:
```javascript
var payload = JSON.stringify({
    v: 1, type: "cid_pin", cid: cid, label: label,
    source: source || "", ts: Math.floor(Date.now() / 1000)
})
```

**Cord benefit:** `dispatchMessage` can filter/route by `payload.source` — e.g. "show me only Notes backups from Alice's channel".

**Blocked on:** Stash passing `source` when calling `pinCid`. Stash currently calls `pinCid(cid, label)` — needs a third argument once this is implemented.

---

## Common Pitfalls (from stash/keycard lessons)

- **`background: null` on TextEdit** — silent QML load failure. Only valid on TextField/TextArea.
- **Clipboard TextEdit helper must be at root level** — not inside nested Rectangle.
- **ListModel not JS array** — `model.get(i)` only works on ListModel.
- **variant file required** — `linux-amd64` must be in BOTH module and plugin dirs.
- **patchelf RUNPATH** — required so Qt libs resolve outside Nix environment.
- **pollBusy guard** — callModule blocks QML thread; Timer re-enters without guard.
- **whole-archive** — `--whole-archive` around liblogos_sdk.a prevents 20s IPC timeout.

---

## File Organization

```
beacon-basecamp/
├── src/plugin/
│   ├── BeaconPlugin.h / BeaconPlugin.cpp
│   └── plugin_metadata.json
├── modules/logos_beacon/
│   ├── manifest.json / metadata.json / plugin_metadata.json / variant
├── plugins/beacon_ui/
│   ├── Main.qml / manifest.json / metadata.json / variant
│   └── icons/Beacon_sidebar.png
├── assets/icons/Beacon_sidebar.png
├── tests/
│   ├── test_beacon_plugin.cpp
│   └── logos_api_stub.cpp
├── docs/
│   ├── plans/beacon-implementation.md
│   └── retro-log.md
├── CMakeLists.txt
├── flake.nix
├── CLAUDE.md
└── CODEX.md
```

---

## Issue Tracking

| # | Epic | Title | Status |
|---|------|-------|--------|
| 1 | Scaffold | Project scaffold | done |
| 2 | Scaffold | Key generation + config invokables | done |
| 3 | Scaffold | Inscription log persistence | done |
| 4 | Zone Seq | Zone sequencer config from QML | done |
| 5 | Zone Seq | Inscription flow | done |
| 6 | Auto-Watch | Stash log poll + CID extraction | done |
| 7 | Auto-Watch | Watch toggle | done |
| 8 | UI | Config panel | done |
| 9 | UI | Inscription log panel | done |
| 10 | Tests | Unit tests | done |
| 11 | UI | Real-time log update on inscription confirm | pending |
| 12 | Core | Add `source` field to `cid_pin` payload | pending |
