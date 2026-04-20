# Plan: logos-node-basecamp

> Beacon plan saved — see beacon-basecamp folder for Epic 1/2 breakdown.

## Context

Running a Logos blockchain node currently requires tmux, manual env var setup, and a separate Python dashboard. This module wraps that into a Basecamp sidebar plugin: start/stop the node, see sync progress, watch live logs — all from the same UI as other modules. Community value: lowers the bar from "know tmux + systemd" to "click Start".

Senty reviews after full implementation.

---

## Architecture

**Pure C++ + Qt — no Rust FFI.** The node is a standalone binary. The module manages it via `QProcess` and polls `GET /cryptarchia/info` via `QNetworkAccessManager`. No zone-sdk, no Rust, no extra linking.

```
logos-node-basecamp/
├── src/plugin/
│   ├── NodePlugin.h
│   └── NodePlugin.cpp
├── modules/logos_node/
│   ├── manifest.json
│   ├── metadata.json
│   ├── plugin_metadata.json
│   └── variant
├── plugins/node_ui/
│   ├── manifest.json
│   ├── metadata.json
│   ├── Main.qml
│   ├── variant
│   └── icons/Node_sidebar.png
├── assets/icons/Node_sidebar.png
├── CMakeLists.txt
├── CLAUDE.md
├── CODEX.md
└── docs/
    ├── retro-log.md
    └── plans/
        └── node-implementation.md  ← this plan, committed
```

---

## NodePlugin Q_INVOKABLEs

```cpp
Q_INVOKABLE void    initLogos(LogosAPI* api);

// Config — binary, circuits, config file, data dir
Q_INVOKABLE QString setNodeConfig(const QString& binaryPath,
                                  const QString& circuitsPath,
                                  const QString& configPath,
                                  const QString& dataDir);
Q_INVOKABLE QString getNodeConfig() const;
// → {"binaryPath":"...","circuitsPath":"...","configPath":"...","dataDir":"...","configured":bool}

// Lifecycle
Q_INVOKABLE QString startNode();   // → {"ok":true} | {"error":"already running"} | {"error":"not configured"}
Q_INVOKABLE QString stopNode();    // → {"ok":true} | {"error":"not running"}
Q_INVOKABLE QString getStatus();
// → {"running":bool,"mode":"Online|Offline","slot":N,"libSlot":N,"height":N,
//    "tip":"hex","lib":"hex","nodeUrl":"http://localhost:8080","startedByUs":bool}

// Log stream
Q_INVOKABLE QString getLog() const;
// → [{"ts":"HH:mm:ss","msg":"...","level":"info|warn|error"}]  (last 200 lines)

// For beacon: get node URL without full config roundtrip
Q_INVOKABLE QString getNodeUrl() const;
// → "http://localhost:8080" (from config or default)
```

**Key design:** `startedByUs` flag tracks whether the module launched the process. `stopNode()` only kills if `startedByUs == true` — never kills an externally-started node.

**QSettings keys:** `nodeNodeBinaryPath`, `nodeCircuitsPath`, `nodeConfigPath`, `nodeDataDir`, `nodeHttpPort` (default 8080).

---

## Node start command (from runbook — exact)

```cpp
QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
env.insert("LOGOS_BLOCKCHAIN_CIRCUITS", circuitsPath);

m_process = new QProcess(this);
m_process->setProcessEnvironment(env);
m_process->setWorkingDirectory(dataDir);
m_process->setProcessChannelMode(QProcess::MergedChannels);  // stdout+stderr together
m_process->start(binaryPath, {configPath});
```

---

## `/cryptarchia/info` response (verified live)

```json
{"lib":"8d2b7740...","lib_slot":258630,"tip":"5321d0...","slot":259368,"height":13326,"mode":"Online"}
```

Poll every 3s via `QNetworkAccessManager` + `QEventLoop`. If request fails → node not running → `running: false`.

---

## Epics and Issues

### Epic 1 — Scaffold & Config (prerequisite)

**Issue #1: Project scaffold**
- [ ] Create folder structure (src/, modules/, plugins/, docs/, assets/)
- [ ] CMakeLists.txt — Qt6 Core/Qml/Network, liblogos_sdk.a, install rules
- [ ] modules/logos_node/ manifests (manifestVersion 0.2.0, type: "core")
- [ ] plugins/node_ui/ manifests (manifestVersion 0.2.0, type: "ui_qml", view: "Main.qml")
- [ ] variant files (linux-amd64) in both dirs
- [ ] CLAUDE.md + CODEX.md from fieldcraft templates
- [ ] docs/retro-log.md (empty)
- [ ] Placeholder NodePlugin.h/cpp that compiles clean
- **Success:** `cmake -B build && cmake --build build` green

**Issue #2: Config invokables**
- [ ] `setNodeConfig(binary, circuits, config, dataDir)` — validate paths exist, persist QSettings
- [ ] `getNodeConfig()` — read QSettings, return JSON, `configured: true` only if all 4 paths non-empty
- [ ] `getNodeUrl()` — returns `"http://127.0.0.1:" + port` from QSettings, default port 8080
- [ ] QSettings key namespace: `logos-node/`
- **Success:** set → get round-trips correctly; missing path returns `{"error":"..."}`

---

### Epic 2 — Process Lifecycle

**Issue #3: QProcess start/stop + detection**
- [ ] `startNode()`: check if already running via `GET /cryptarchia/info` first (if responds → already running externally, set `startedByUs = false`, return `{"ok":true,"alreadyRunning":true}`)
- [ ] If not running: launch via QProcess with env vars (circuits path)
- [ ] Connect `QProcess::readyReadStandardOutput` → append to circular log buffer (200 lines max)
- [ ] Connect `QProcess::finished` → log "process exited: code N", set `m_process = nullptr`
- [ ] `stopNode()`: only kill if `startedByUs == true`; send `SIGTERM` first, SIGKILL after 5s if still alive
- [ ] `getLog()`: serialize circular buffer as JSON array `[{ts, msg, level}]`
- [ ] Level heuristic: line contains "ERROR" → "error", "WARN" → "warn", else "info"
- **Success:** start → getLog shows output lines; stop works; restarting externally-started node returns alreadyRunning

**Issue #4: HTTP status poll**
- [ ] `getStatus()`: fire `GET http://127.0.0.1:<port>/cryptarchia/info` synchronously (QNetworkAccessManager + QEventLoop, 2s timeout)
- [ ] Parse JSON: slot, lib_slot, height, tip, lib, mode → build response object
- [ ] Merge with process state: add `running`, `startedByUs`, `nodeUrl` fields
- [ ] If HTTP fails → `{"running":false,...}` (process may be starting up)
- **Success:** node running → getStatus returns valid JSON with slot > 0; node stopped → running: false

---

### Epic 3 — QML UI

**Issue #5: Plugin skeleton + settings panel**
- [ ] Main.qml base: root Rectangle, dark theme (#0A0A0A bg), callModuleParse helper
- [ ] Settings panel (collapsible): binary path, circuits path, config file path, data dir — 4 text fields
- [ ] Save button → `logos.callModule("logos_node", "setNodeConfig", [...])`
- [ ] On open: load existing config via `getNodeConfig()`
- [ ] Status dot in toolbar: green=Online, amber=syncing, red=offline — updates from poll timer
- [ ] `variant` file in plugins/node_ui/ + icon placeholder
- **Success:** module opens in Basecamp, settings save persists across restart

**Issue #6: Status display**
- [ ] Header row: mode label (Online/Offline/Starting), slot number, height, LIB slot
- [ ] Sync progress bar: `lib_slot / slot` (fill %) — hidden when `mode == "Online"` and lag < 100 slots
- [ ] Tip hash display (first 8 chars + "...")
- [ ] 3s poll timer with `pollBusy` re-entrancy guard → calls `getStatus()`
- [ ] Subtle "●" status dot: green (#22C55E) = Online, amber (#F59E0B) = < 95% synced, red (#EF4444) = not running
- **Success:** status updates live; syncing node shows progress bar; offline shows red

**Issue #7: Activity log + start/stop button**
- [ ] Activity log: ListModel + TextEdit delegate (same pattern as stash/keycard)
- [ ] Log polling: 2s timer → `getLog()` → append new lines by seen-count (same `logSeenCount` pattern)
- [ ] Start/Stop button: Start when `!status.running`, Stop when `status.running && status.startedByUs`; grey/disabled when `running && !startedByUs`
- [ ] Copy button: clipboard via root-level hidden TextEdit (qml-patterns.md rule)
- [ ] Tooltip on Stop button when disabled: "Node started externally — stop it via systemd/tmux"
- **Success:** Start launches node, log fills with output; Stop terminates it; external node → Stop greyed

---

### Epic 4 — Tests

**Issue #8: Unit tests — config**
- [ ] `testSetNodeConfigAllPaths` — all 4 paths set → `configured: true`
- [ ] `testSetNodeConfigMissingBinary` — empty binary → `configured: false` (or error)
- [ ] `testGetNodeConfigRoundTrip` — set then get → same values
- [ ] `testGetNodeUrlDefault` — no port set → returns `http://127.0.0.1:8080`
- **Success:** `ctest --output-on-failure` green

**Issue #9: Integration test — HTTP status**
- [ ] `FakeNodeServer` (minimal QTcpServer in test file) returns hardcoded `/cryptarchia/info` JSON
- [ ] `testGetStatusOnline` — server returns `mode:Online, slot:1000` → getStatus running:true, slot:1000
- [ ] `testGetStatusOffline` — no server → getStatus running:false
- [ ] `testGetStatusSyncing` — lib_slot 500, slot 1000 → verify fields present
- **Success:** all tests green without real node running

---

## GitHub Issue Plan (private repo)

```
Epic 1 — Scaffold & Config
  #1  Project scaffold (CMakeLists, manifests, templates)
  #2  Config invokables (setNodeConfig, getNodeConfig, getNodeUrl)

Epic 2 — Process Lifecycle
  #3  QProcess start/stop + log capture
  #4  HTTP status poll (/cryptarchia/info)

Epic 3 — QML UI
  #5  Plugin skeleton + settings panel
  #6  Status display (mode, slot, height, sync bar)
  #7  Activity log + start/stop button

Epic 4 — Tests
  #8  Unit tests — config
  #9  Integration test — HTTP status (FakeNodeServer)
```

Execution order: 1→2→3→4→5→6→7→8→9. Issues 8+9 can start after 4.

---

## Files to Create

All new — no existing files modified except `stash-basecamp` and `logos-notes` for beacon later.

| File | Purpose |
|------|---------|
| `src/plugin/NodePlugin.h` | Q_INVOKABLEs, QProcess member, log buffer |
| `src/plugin/NodePlugin.cpp` | All implementations |
| `CMakeLists.txt` | Build + install |
| `modules/logos_node/{manifest,metadata,plugin_metadata}.json` | Core plugin manifests |
| `modules/logos_node/variant` | `linux-amd64` |
| `plugins/node_ui/{manifest,metadata}.json` | UI plugin manifests |
| `plugins/node_ui/Main.qml` | Full QML UI |
| `plugins/node_ui/variant` | `linux-amd64` |
| `assets/icons/Node_sidebar.png` | Sidebar icon |
| `CLAUDE.md` | From fieldcraft template |
| `CODEX.md` | From fieldcraft template |
| `docs/retro-log.md` | Empty, captures wins/fails during work |
| `docs/plans/node-implementation.md` | This plan, committed |
| `tests/test_node_plugin.cpp` | Issues #8+#9 |

---

## Verification

```bash
# 1. Build
cd logos-node-basecamp && cmake -B build -DLOGOS_CPP_SDK=... && cmake --build build -j$(nproc)

# 2. Tests
cd build && ctest --output-on-failure

# 3. Install
cmake --install build

# 4. Kill + relaunch Basecamp
pkill -9 -f "LogosBasecamp.elf"; sleep 1
~/logos-basecamp-current.AppImage &

# 5. Node icon appears in sidebar
# 6. Settings: paste real paths from runbook → Save
# 7. Status dot: red (node stopped)
# 8. Click Start → dot turns amber (syncing) → green (Online)
# 9. Log tab fills with node stdout
# 10. Click Stop → dot turns red, process gone
# 11. Start node manually in tmux → open module → Stop button greyed ("started externally")
```
