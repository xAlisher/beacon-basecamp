# beacon-basecamp

On-chain CID inscription module for [Logos Basecamp](https://github.com/xAlisher/basecamp-template).

Beacon watches [stash-basecamp](https://github.com/xAlisher/stash-basecamp) for newly uploaded files and inscribes each CID into a dedicated LEZ zone channel — creating a permanent, verifiable on-chain index of everything you've stored.

## What it does

- **Auto-inscribes** every file you upload to Stash within 10 seconds
- **Generates** a dedicated Ed25519 signing key on first run — your channel belongs to you
- **Persists** an inscription log locally (`inscription-log.json`) with status tracking
- **Shows** live activity: pending (amber) → confirmed (green) or failed (red)
- **Manual pinning** — inscribe any CID directly from the UI

## Architecture

```
stash-basecamp          beacon-basecamp              liblogos_zone_sequencer_module
  getLog() ──10s──► BeaconPlugin (C++)  ─── QML IPC ──►  publish(cid_pin payload)
                      key gen                               inscribe to LEZ chain
                      inscription log                       checkpoint file
                    beacon_ui (QML)
                      activity log
                      config panel
```

**Two components:**
- `logos_beacon` — C++ core plugin: key management, inscription log, config storage
- `beacon_ui` — QML UI plugin: activity log, config panel, stash poll loop

## Dependencies

| Module | Role |
|--------|------|
| `liblogos_zone_sequencer_module` | Publishes CID payloads to LEZ chain |
| `stash-basecamp` (`stash`) | Source of uploaded CIDs to watch |

## Inscription payload

```json
{ "v": 1, "type": "cid_pin", "cid": "baf...", "label": "notes backup", "ts": 1745100000 }
```

## Signing key

Beacon auto-generates a 32-byte Ed25519 seed on first run using `QRandomGenerator::system()`, stored as 64-char hex at `instancePersistencePath/beacon.key` (mode 0600).

**Back it up** — losing the key means losing write access to your channel. Read access remains open.

Future: key derivation via `keycard-basecamp` Ed25519 path (`logos:beacon`) will replace the file-based key entirely.

## Build

Requires Qt 6.x, CMake, and the Logos C++ SDK (`liblogos_sdk.a`).

```bash
cmake -B build
cmake --build build -j$(nproc)
cmake --install build
```

Install copies the module and plugin to `~/.local/share/Logos/LogosApp/` and mirrors to `LogosBasecamp/`.

## Tests

```bash
cd build && ctest --output-on-failure
```

Covers: key generation, config round-trip, inscription log persistence, duplicate guard, confirm flow.

## Persistence

All state lives in `instancePersistencePath` (injected by the platform):

```
beacon.key               Ed25519 seed (64-char hex, mode 0600)
beacon.checkpoint        Zone sequencer chain checkpoint
inscription-log.json     Inscription history (last 100 entries shown in UI)
```

Node URL and watch toggle are stored in QSettings under `beacon/`.

## Known gaps

- Retry queue for failed inscriptions (node down at inscription time)
- Keycard Ed25519 integration — blocked on `keycard-basecamp` exposing `deriveEd25519Key`
- Icon: placeholder — needs proper 28×28 design
- Notes activity log integration (Issue #12)
