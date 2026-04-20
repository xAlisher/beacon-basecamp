# beacon-basecamp — Senty Review Rules

> Senty reviews all PRs before merge. These are the rules Senty enforces.

## Hard rules (auto-reject)

- `Q_INVOKABLE` method returns anything other than `QString` → reject
- `beacon.key` written without `chmod 0600` → reject
- `callModule` inside a Timer without a `pollBusy` guard → reject
- `JSON.parse` in QML without the three-layer `callModuleParse` form → reject
- `liblogos_sdk.a` linked without `--whole-archive` / `--no-whole-archive` → reject
- `instancePersistencePath` not used for persistent files (uses home dir instead) → reject
- `manifest.json` uses `"main": "Main.qml"` instead of `"view": "Main.qml"` → reject
- Hardcoded nix store path without env var override (`LOGOS_CPP_SDK_ROOT`, `LOGOS_LIBLOGOS_HEADERS`) → reject

## Review checklist

- [ ] `ensureKey()` creates file only if absent, reads it back on second call (idempotent)
- [ ] `pinCid()` checks for duplicate CID before appending
- [ ] `confirmInscription()` bounds-checks `entryIndex`
- [ ] QML `inscribeCid` calls `pinCid` first, checks for duplicate, then calls zone seq
- [ ] `stashSeenCount` incremented only after processing new entries
- [ ] Zone sequencer error (module not installed) shows clear error banner in UI
- [ ] All tests pass: `ctest --output-on-failure`
- [ ] `variant` file present in both `modules/logos_beacon/` and `plugins/beacon_ui/`
- [ ] Icon file present at `assets/icons/Beacon_sidebar.png` and `plugins/beacon_ui/icons/`

## Style

- Compact JSON: `QJsonDocument::Compact` everywhere
- Error objects: `{"error": "message"}`, success objects: `{"ok": true, ...}`
- QSettings keys: `beacon/` prefix only
- Log entries: `{cid, inscriptionId, label, ts, status}` — no extra fields
