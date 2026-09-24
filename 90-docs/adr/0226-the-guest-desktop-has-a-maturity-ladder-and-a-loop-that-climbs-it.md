# ADR-0226 — The guest desktop has a maturity ladder, and a loop that climbs it

Date: 2026-09-24

## Status

Accepted.

## Goal

**kotoba-lang/browser's surface is the aiueos desktop you can use on
KERNEL.ELF** -- seen on the display, driven by pointer and keyboard in one
running event loop, laid out as browser lays it out -- **and then the browser
component itself runs there.** ADR-0223..0225 are the first five rungs.

## Decision

1. **The ladder is data**: `os/aiueos/contracts/desktop-maturity-floors.edn`,
   one floor per rung, in the order they are taken. A floor is **closed only
   by its gate** -- a `scripts/compositor-guest.cljk` profile that is both
   registered in `src/aiueos/compositor/guest.cljk` and described by a
   "`<gate>` exit 0 means ..." paragraph in README.md on `origin/main`.
   A floor without a gate is never counted as closed. `:needs-a-human`
   floors (a dictionary licence, a physical machine) are reported, not taken.
2. **Maturity is measured, not asserted**: closed floors / all floors, read
   from `origin/main` by the superproject tick `scripts/aiueos-desktop-tick.cljk`
   (ledger `~/.itonami/aiueos-desktop-tick.ledger.edn`).
3. **A loop climbs it one floor per iteration**: LaunchAgent
   `cloud.itonami.bot.aiueos-desktop` runs `scripts/aiueos-desktop-loop.cljk`,
   which asks the tick for the next open floor and only then starts
   `claude -p "/aiueos-desktop"` through `scripts/loop/claude_run.cljk`. The
   skill `aiueos-desktop` is the iteration's runbook: worktree from
   `origin/main`, Kotoba object for the decision and C for mechanism, KIR
   oracle contract whose expected values come from kotoba-lang/browser or
   an independent model, a deliberate break seen red, a QEMU gate, landing
   (kotoba-native row -> amu pin via PR -> aiueos), west pins, cleanup.
4. **The ladder may be corrected** by an iteration (split a floor that is two
   jobs, reorder when one depends on another) -- in the same commit, with the
   reason.

## Consequences

Progress on the desktop is one number with a reproducible source, and each
step lands with its own gate. What the ladder does not contain is not
claimed: P5 and the dictionary stay open until a person acts.

## Measurement

At adoption (2026-09-24, `origin/main` after ADR-0225): the tick counts
5 of 16 floors closed (frame, pointer, text, text-raise, typing), 2 needing a
human, next floor `:preedit`.
