# beacon-basecamp

On-chain CID inscription module for [Logos Basecamp](https://github.com/logos-co/logos-app).

Beacon watches [stash-basecamp](https://github.com/xAlisher/stash-basecamp) for newly uploaded files and inscribes each CID into a dedicated LEZ zone channel — creating a permanent, verifiable on-chain index of everything you've stored.

---

## What it does

- **Auto-inscribes** every file you upload to Stash within ~10 seconds
- **Generates** a dedicated Ed25519 signing key on first run — your channel belongs to you
- **Persists** an inscription log locally with status tracking and live explorer links
- **Shows** live activity: pending (amber) → confirmed (green) or failed (red)
- **Manual pinning** — inscribe any CID directly from the UI with a custom label

---

## How it works

```
stash-basecamp          beacon-basecamp                 liblogos_zone_sequencer_module
  getLog() ──10s──► BeaconPlugin (C++)                        LEZ Testnet
                      key management     ─── QML IPC ──►  publish(cid_pin payload)
                      inscription log                           mantle_tx ──► block
                      findExplorerTxHash ◄──────────────── explorer block API
                    beacon_ui (QML)
                      activity log
                      config panel
                      finalization poller
```

1. **Poll** — QML polls `stash.getLog()` every 10s for new uploaded CIDs
2. **Inscribe** — calls `liblogos_zone_sequencer_module.publish(payload)` via QML IPC
3. **Track** — `pendingFinalizations` array monitors in-progress inscriptions
4. **Confirm** — `findExplorerTxHash(channelId)` queries the explorer block API to get the canonical tx hash; updates the log entry with a live link

**Two components:**
- `logos_beacon` — C++ core plugin: key management, inscription log, `findExplorerTxHash`, config storage
- `beacon_ui` — QML UI plugin: activity log, config panel, stash poll loop, finalization tracking

---

## Dependencies

All dependencies must be installed and loaded in Logos Basecamp.

| Module | Installed name | Repo | Role |
|--------|---------------|------|------|
| **beacon** (this) | `logos_beacon` | [beacon-basecamp](https://github.com/xAlisher/beacon-basecamp) | C++ core plugin |
| **beacon-ui** (this) | `beacon_ui` (plugin) | [beacon-basecamp](https://github.com/xAlisher/beacon-basecamp) | QML interface |
| **zone sequencer** | `liblogos_zone_sequencer_module` | shipped with Basecamp AppImage | publishes `cid_pin` payloads to LEZ chain |
| **stash** | `stash` | [stash-basecamp](https://github.com/xAlisher/stash-basecamp) | source of uploaded CIDs to watch |
| **keycard** | `logos_keycard` | [keycard-basecamp](https://github.com/xAlisher/keycard-basecamp) | provides Ed25519 signing key (required for inscription) |

### Runtime environment

- [Logos Basecamp](https://github.com/logos-co/logos-app) AppImage — tested on `v0.2.0+`
- Linux x86-64
- LEZ testnet node accessible (default: Tailscale `100.108.127.3:8080`, configurable in UI)

---

## Build

### Option A — Nix (recommended, portable)

Uses [logos-module-builder](https://github.com/logos-co/logos-module-builder) via Nix flakes.

```bash
git clone https://github.com/xAlisher/beacon-basecamp
cd beacon-basecamp

# Build portable installable (strips Nix RPATH, bundles deps next to .so)
nix build .#packages.x86_64-linux.install-portable
```

Output is at `result/`. Verify RPATH before installing:

```bash
patchelf --print-rpath result/modules/logos_beacon/logos_beacon_plugin.so
# Must show: $ORIGIN/.
# Must NOT contain any /nix/store/*/qtbase* paths
```

### Option B — CMake (local dev)

Requires Qt 6.9.3 at `~/Qt/6.9.3/gcc_64/` and Logos C++ SDK in the Nix store.

```bash
cmake -B build
cmake --build build -j$(nproc)
cmake --install build
```

`cmake --install` copies `logos_beacon_plugin.so` and `plugins/beacon_ui/` to
`~/.local/share/Logos/LogosBasecamp/`.

> **Note:** Always patch source QML before running `cmake --install` — the install
> step overwrites the installed `Main.qml` with the source copy.

---

## Install

### One-shot install (from Nix build output)

```bash
INSTALL_DIR=~/.local/share/Logos/LogosBasecamp/modules/logos_beacon

# Remove stale install
chmod -R u+w "$INSTALL_DIR" 2>/dev/null; rm -rf "$INSTALL_DIR"

# Copy build output
cp -r result/modules/logos_beacon/. "$INSTALL_DIR/"
cp metadata.json "$INSTALL_DIR/"

# Strip hashes — platform rejects dev modules with hashes field
chmod u+w "$INSTALL_DIR/manifest.json"
python3 -c "
import json
with open('$INSTALL_DIR/manifest.json') as f: m = json.load(f)
m.pop('hashes', None)
m['main']['linux-amd64'] = 'logos_beacon_plugin.so'
with open('$INSTALL_DIR/manifest.json', 'w') as f: json.dump(m, f, indent=2)"

echo "linux-amd64" > "$INSTALL_DIR/variant"

# Install QML UI plugin
UI_DIR=~/.local/share/Logos/LogosBasecamp/plugins/beacon_ui
mkdir -p "$UI_DIR/qml"
cp plugins/beacon_ui/Main.qml "$UI_DIR/qml/Main.qml"
cp plugins/beacon_ui/Main.qml "$UI_DIR/Main.qml"
cp plugins/beacon_ui/manifest.json "$UI_DIR/manifest.json"
cp plugins/beacon_ui/metadata.json "$UI_DIR/metadata.json"
cp plugins/beacon_ui/variant "$UI_DIR/variant"
cp -r plugins/beacon_ui/icons "$UI_DIR/"

# Clear QML cache
rm -rf ~/.cache/Logos/LogosBasecamp/qmlcache
```

### Install paths

```
~/.local/share/Logos/LogosBasecamp/
├── modules/logos_beacon/
│   ├── logos_beacon_plugin.so
│   ├── manifest.json
│   ├── metadata.json
│   └── variant
└── plugins/beacon_ui/
    ├── Main.qml
    ├── manifest.json
    ├── metadata.json
    ├── variant
    └── icons/
        └── Beacon_sidebar.png
```

---

## Usage

### First-time setup — set your node URL

Beacon inscribes via a Logos testnet node. **Nothing will work until you point Beacon at a reachable node.**

1. Launch Logos Basecamp — the **Beacon** tab appears in the sidebar
2. Open the **Config** panel → **Node URL**
3. Enter your node's RPC endpoint:
   - Local node: `http://localhost:8080`
   - Remote node: `http://<your-node-ip>:8080`
4. Press **Enter** to save — Beacon confirms connectivity immediately

> The default `http://100.108.127.3:8080` is a private node on Tailscale. It is not reachable without VPN access. Change it before doing anything else.

### UI

1. Launch Logos Basecamp — the **Beacon** tab appears in the sidebar
2. Set your node URL (see above)
3. Insert your Keycard — Beacon picks up the Ed25519 key automatically
4. Enable **Watch Stash** in the config panel to auto-inscribe all Stash uploads
5. Or paste any CID + label in the manual pin panel and click **Inscribe**
6. The activity log shows: `pending → confirming → confirmed` with a live explorer link

### Config panel

| Setting | Description |
|---------|-------------|
| **Watch Stash** | Toggle auto-inscription of all Stash uploads |
| **Node URL** | Logos node RPC endpoint (default: `http://100.108.127.3:8080`) |
| **Channel ID** | Your derived LEZ channel ID (read-only, derived from signing key) |
| **Signing key** | Ed25519 seed hex — copy and back up securely |

### Inscription payload

```json
{ "v": 1, "type": "cid_pin", "cid": "bafyrei...", "label": "notes backup", "source": "logos_notes", "ts": 1748300000 }
```

The `source` field indicates which Basecamp module originated the CID (e.g. `"logos_notes"`, `"keeper"`, `"stash"`).

### Explorer links

Once an inscription is confirmed, the log shows a direct link:

```
https://testnet.blockchain.logos.co/web/explorer/transactions/<txHash>
```

The tx hash is retrieved from the explorer block API (not from `zone_sequencer_publish` return value) — see Architecture notes below.

---

## Persistence

All state lives in `instancePersistencePath` (injected by the platform):

```
<instancePersistencePath>/
├── beacon.key               Ed25519 seed (64-char hex, mode 0600)  [deprecated — now from Keycard]
├── beacon.checkpoint        Zone sequencer chain checkpoint
└── inscription-log.json     Inscription history
```

Node URL and watch toggle are stored in QSettings under the `beacon/` namespace.

---

## Tests

### Unit tests (Qt Test, no network)

```bash
cmake -B build && cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Covers: key generation, config round-trip, inscription log persistence, duplicate guard,
confirm flow. ~25 assertions, runs in <1s.

### Build against current SDK

If CMake fails to find the Logos SDK:

```bash
# Override Nix store paths
LOGOS_CPP_SDK_ROOT=/nix/store/<hash>-logos-cpp-sdk \
LOGOS_LIBLOGOS_HEADERS=/nix/store/<hash>-logos-liblogos-headers-0.1.0/include \
cmake -B build
```

---

## Architecture notes

### Explorer tx hash — 2-step lookup

`zone_sequencer_module.publish()` returns a hash that may differ from what the explorer
indexes. `findExplorerTxHash(channelId)` uses a 2-step approach:

**Step 1** — scan node blocks for the inscription:
```
GET /cryptarchia/blocks?from_slot={libAtSubmit}&to_slot={libAtSubmit+N}
→ find block containing channelId in operations[].content.channel_id
→ extract blockHeaderId
```

**Step 2** — get the real tx hash from the explorer:
```
GET {explorer}/web/explorer/api/v1/fork-choice         → fork ID
GET {explorer}/web/explorer/api/v1/blocks/{blockHeaderId}?fork={forkId}
→ find tx matching channelId → return transactions[].hash
```

This is robust to zone sequencer library version differences and reorgs below LIB.

### pollBusy guard

All QML Timer callbacks that call `logos.callModule` must be guarded against re-entry.
`finalizationTimer` iterates `pendingFinalizations` — without the guard, a slow
`callModule` response lets the timer fire again mid-loop:

```qml
property bool pollBusy: false
function pollFinalizations() {
    if (root.pollBusy) return
    root.pollBusy = true
    // ... callModule loop over pendingFinalizations ...
    root.pollBusy = false
}
```

### Signing key

Beacon originally auto-generated a 32-byte Ed25519 seed on first run, stored at
`instancePersistencePath/beacon.key`. Key derivation now comes from Keycard
(`keycard.getSigningKey()`). The file-based fallback remains for compatibility.

**Back up your key** — losing it means losing write access to your channel.

---

## Known gaps

- Retry queue for failed inscriptions (node down at inscription time)
- Notes activity log integration (Issue #12) — beacon log entries not surfaced in logos-notes
- Icon: placeholder PNG — needs proper 28×28 design
