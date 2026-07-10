# Halt — 2026-07-10 (beacon#50 publish-crash session)

## Where we stopped
Long live-repro session on the fresh-channel publish crash (#50). **The product loop is proven
working end-to-end**; the crash is a separate, deeper native bug that's now correctly diagnosed
(not fixed); the crash→freeze **cascade IS fixed** (#53) and built, not yet installed/committed.

## Branch & build state
- Branch: **fix/beacon-50-logos-publish-segv** (NOT main — halt at session start was stale)
- **Uncommitted** working-tree changes (both modules build clean, qmllint 0 errors):
  - `src/logos_beacon_impl.{h,cpp}` — seqPublish reverted to a plain `return publish_to(...)`
    (removed the in-handler `publishCompleted` event emit — it was a red herring; core ≈ original)
  - `plugins/beacon_ui/src/beacon_ui.rep` — `seqPublish(int token,…)` + `SIGNAL seqPublishResult`
  - `plugins/beacon_ui/src/beacon_ui_backend.{h,cpp}` — async fire-and-forget `seqPublishAsync`,
    result delivered via the **async reply callback** (not an event), `kPublishTimeoutMs = 240000`
  - `plugins/beacon_ui/Main.qml` — token-correlated delivery, honest "submitting" state, #52
    finalising-mislabel fix, **guard-recovery** (pubTimeoutMs 240s, watchdog orphan release,
    pollBusySince backstop in pollModules)
- Latest builds staged in scratchpad: `scratchpad/beacon-core`, `scratchpad/beacon-ui`
  (build beacon_ui with `--override-input logos_beacon path:/home/alisher/basecamp/modules/beacon-basecamp`)

## ✅ Proven working (don't re-litigate)
Keeper auto-preserve → download → Logos Storage (real CIDs) → on-chain inscription →
`confirmed` @ slot 428365 → proof link `explorer.logos.live/#3a9d3849…`, **automatically**.
Node verified: `GET https://logos-testnet.paradox.computer/channel/3a9d3849…` → 200, real tip.
**Crash is post-success → no data loss.** A restart reconciles stuck "submitted" → confirmed.

## 🔴 #50 — the crash (diagnosed, NOT fixed — needs symbolization)
Intermittent native SIGSEGV in `logos_beacon` **at the instant `zone_sequencer.publish_to` returns
a successful inscription_id**. Channel-agnostic (crashes on bootstrapped master too), correlates
with SLOW publishes, pre-existing. **3 disproven theories** (all in the #50 comment): sync-loop-park,
20s-timeout-teardown, in-handler-emit. Next: **symbolize** — `ps -ef|grep logos_host.elf|grep -w
logos_beacon|grep -v bash` (pgrep matches your own shell!), capture `/proc/PID/maps` while alive,
subtract base from the raw backtrace addrs, `addr2line -e logos_beacon_plugin.so` (.so has .symtab).
Likely liblogos/zone_sequencer reply-serialization, not beacon-QML-fixable.

## 🟡 #53 — crash freezes pipeline (FIXED, built, not installed/committed)
Guard-recovery bounds the freeze to ~4min (was 15min/never). See #53 for the 4 changes.

## Next steps (in order)
1. **Install + smoke-test** the guard-recovery build (kill all → copy core+ui → clear qmlcache →
   relaunch to /tmp/basecamp.log). Confirm a keeper item after a crash resumes within ~4min.
2. **Commit** the branch (guard-recovery + async freeze-fix + #52 mislabel). Ask before push.
3. **#50 symbolization** — the real crash fix path (separate, deeper).
4. Optional: explorer deep-link to the specific inscription (`#<channel>/<msgId>`) — small UI issue.

## Context that's hard to re-derive
- logos_beacon & keeper & zone_sequencer each run as SEPARATE `.logos_host.elf --name X` processes;
  beacon_ui is `.ui-host.elf --name beacon_ui`. A core crash does NOT crash beacon_ui (why guards stick).
- GUI-launched Basecamp (gnome-shell parent) logs to the **systemd journal**, not /tmp/basecamp.log.
  Use `journalctl --user`. My own launches (`> /tmp/basecamp.log`) log to the file.
- Ghost-state to clear for a clean slate (backed up in scratchpad/ghost-clean-backup):
  `module_data/logos_beacon/inscription-log.json`, `module_data/keeper/*/keeper-*.json`, `/tmp/keeper-*`.
- ia-basecamp (archiver) issues filed this session: #47 (remove races auto-preserve), #48 (auto-state visibility).
