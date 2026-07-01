# Halt — 2026-06-07

## Where we stopped

Implemented the full inscription lifecycle (issues #17, #18, #19, #20) across beacon and keeper.
Both modules built, installed, Basecamp launched (PID 542269). Waiting for Alisher to do manual
test: keep an IA item in Keeper, watch the progress bar, confirm the "copy URL" button works
after ~8 min finalization. Issues are NOT closed — user said to wait until after manual test.

## Current state

### beacon-basecamp
- Branch: `feat/inscription-lifecycle`
- Last commit: `1ca899b feat: resume in-flight finalizations after keycard re-auth`
- Build status: 34/34 tests passing
- Open review: Senty review done (manual, Codex sandbox blocked filesystem). Two fixes applied
  (dead `pendingResolutions` removed, `findExplorerTxHash` simplified to read `mantle_tx.hash`
  directly from node block scan instead of 3-step explorer round-trip).
- Installed: `~/.local/share/Logos/LogosBasecamp/modules/logos_beacon/logos_beacon_plugin.so`
             `~/.local/share/Logos/LogosBasecamp/plugins/beacon_ui/Main.qml`

### keeper-basecamp
- Branch: `feat/inscription-lifecycle`
- Last commit: `7dbfc6b feat: inscription lifecycle UI — progress bar, ~M:SS, copy URL`
- Build status: nix build clean; keeper_plugin.so installed via lgpm; Main.qml copied directly
- Installed: `~/.local/share/Logos/LogosBasecamp/modules/keeper/keeper_plugin.so`
             `~/.local/share/Logos/LogosBasecamp/plugins/keeper_ui/Main.qml`

## Next steps (in order)

1. **Alisher tests manually** — keep an IA item, watch progress bar (~8 min), copy URL, confirm explorer link works
2. **Close resolved issues** once test passes:
   - beacon #17 (wrong inscriptionId hash), #18 (stale checkpoint), #19 (slotFrom param), #20 (UX lifecycle), #15 (copy button)
   - keeper #19 (explorer URL 404), #25 (lifecycle UI), #17 (stale checkpoint inherited fix)
3. **Push both branches** to GitHub and open PRs if not already done
4. **Log wins** in keeper retro-log for the lifecycle work
5. **Update CODEX.md** in both repos with new log entry schema fields (`slotFrom`, `libAtSubmit`, `inscriptionStatus`)

## Blockers

- Manual test must pass before closing issues (user decision)
- keeper nix build uses git tree — next time, commit before `nix build` or manually copy QML post-install (as done this session)

## Context that's hard to re-derive

- `mantle_tx.hash` (from node block API) == the explorer hash. `zone_sequencer_publish` return value is Poseidon2 TxHash — different value, NEVER use as explorer URL. Confirmed in retro log (win 2026-06-07).
- `findExplorerTxHash` was originally a 3-step (node scan → fork-choice API → explorer blocks API). Simplified to just read `mantle_tx.hash` from node scan directly (Senty review finding). The 3-step is dead code.
- Keeper's `pollForTxHash` was timing out at 24 × 5s = 2 min, well before beacon's ~8-min finalization. Increased to 120 × 5s = 10 min.
- Beacon restart recovery: `pendingFinalizations` is in-memory only. After keycard re-auth, the keycardAuthPollTimer handler now scans `logModel` for in-flight entries and re-populates `pendingFinalizations` with the correct channelId (derived from source if needed).
- keeper nix build ignores dirty working tree changes for cached derivations — always `cp` the QML manually after `nix build` or commit first.
- Progress bar formula: `(currentLibSlot - libAtSubmit) / (slotFrom - libAtSubmit)`. slotFrom = node slot at inscription time, libAtSubmit = lib_slot at inscription time. Bar fills as lib advances toward slotFrom (~485 slots = ~8 min).
- Issues #14 (inscription not landing on testnet) and beacon #1 (keycard connection) are older open issues that were NOT part of this session — don't conflate them with the lifecycle work.
