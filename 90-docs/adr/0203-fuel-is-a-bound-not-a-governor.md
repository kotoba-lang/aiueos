# ADR 0203: fuel is a bound, not a governor — the kernel tier

Status: accepted. Date: 2026-09-08. Extends: ADR-0195. Adjacent: ADR-0033, ADR-0034.

## Context

ADR-0195 settled the mechanism: the context word is a `uint64_t`, the charge is
`dec qword [r9+8]`, the ceiling is 2^53−1, and 2,200,000,005 fuel has actually
been spent on a CPU with the guard still firing on a control. It also left the
rule that matters: **a ceiling that has been raised and never walked past is a
larger number, not a larger bound**, and per-object tiers stay *measured by
execution with a stated margin*.

This ADR is about a tier ADR-0195 did not cover and ADR-0033/0034 predate: the
**aiueos native kernel image itself**.

It differs from every per-object tier in three ways that change the answer.

**It is per boot, not per invocation.** ADR-0034 made the replenish universal,
so an object's budget bounds work *inside one call*. The kernel image carries
`{"fuel": {"initial": N, "replenishable": false}}` — one budget for the life of
the machine.

**Its guard is not recoverable.** An object that exhausts fuel traps into a
kernel that is still running. The kernel image exhausting fuel executes `ud2`
with no vector-6 handler installed for it, the CPU triple-faults, and the board
**halts with nothing to reset it**. The deliberate end of a run writes 0x06 to
port 0xCF9 and the machine comes back through PXE; the guard firing means a
person walks over and presses a button. This rig exists so that nobody has to.

**Its budget was never chosen.** The build script has said `--fuel 1048576`
since it was written. 1048576 is 2^20, and 2^20 is **exactly the value of
`max-native-fuel` that stood until 2026-09-03** — the compiler admitted no
more, so no more could be written. `max-native-fuel` is now `ir/max-fuel`
= 2^53−1 (kotoba-kir ADR 0268; the fixture
`amu/test/nbb/fixtures/excessive-native-fuel-policy.edn` records the move in
its own first line). The number outlived the reason for it by five days.

## What that cost, measured

`debug-run-cycles` was 4, chosen to fit inside 2^20. Measured 2026-09-08 from
the bus2 netlog, timing every `D8` (run start) to its `DE` (deliberate end):

| | fuel | cycles | run ends | up per run | period | duty cycle |
|---|---|---|---|---|---|---|
| before | 2^20 | 4 | `DE` every boot | **12 ms** | 19.7 s | **0.1 %** |
| after | 2^26 | 64 | `DE` every boot | **185 ms** | 39.8 s | **0.91 %** |

**The board was alive for twelve milliseconds out of every twenty seconds.**

That is not an abstract inefficiency. The K16 is a murakumo node: a job is
dispatched when it announces itself, and it announces itself with ~12 ms left
to live. Three dispatches in a row returned `k16-result-timeout` while the
*other* wire showed `D9 70` — the board had computed an answer, under a later
boot nonce, for a job whose dispatcher had already given up. A node reachable
0.1 % of the time is not a node.

## What fuel is for here, and what it is not

**For: termination of a program whose termination is not otherwise provable.**
The estimator (`kotoba.compiler.fuel-estimate`) says so about itself — "crude
compile-time fuel estimate", "best-effort only — not a sound WCET analysis" —
and reports `receive-established`, which has two self-calls, as `:unbounded`.
**No sound upper bound on this program exists.** That is precisely the
condition under which a hard counter earns its place: it is the last line,
and it fires when reasoning has already failed.

**Not for: governing how much work a healthy run may do.** Fuel firing on
correct work buys no safety. It ends a run that was going to end anyway — the
run is terminated by `debug-run-cycles`, deliberately, with a reset — and it
ends it in the one way that needs a human.

This asymmetry sets the direction. A budget that is too large delays the catch
of a runaway; on this board the runaway would otherwise be caught by the
watchdog nobody has written, so the delay is real but bounded and observable. A
budget that is too small halts a working machine and requires physical access.
**The costs are not symmetric, so the budget should not be centred.**

## Decision

1. **The run is bounded by an explicit cycle count. Fuel is the backstop.**
   `debug-run-cycles` says how long a run is and ends it recoverably. Fuel must
   be large enough that no healthy run ever reaches it. A fuel death is a bug
   report, not a schedule.

2. **Fuel is raised before the run is lengthened, and by more.** Margin per
   cycle must never decrease. This change is 64× the fuel for 16× the cycles:
   four times the per-cycle margin the previous configuration ran on. Doing it
   in the other order — cycles first, to see where fuel runs out — is how the
   estimator's own docstring says the cadence constant used to be set ("by
   watching `ud2` deaths on hardware"), and on this board each such death costs
   a person walking to the power button.

3. **The two endings stay distinguishable on the wire.** `DE` present is a
   deliberate end; `DE` absent is the guard firing. That single byte is the
   only instrument that says which, and it is what makes raising the budget an
   experiment rather than a hope. Nothing may be added that ends a run without
   saying which of the two it was.

4. **State the margin, because it is not derived from a bound.** No sound WCET
   exists, so the budget is derived from a configuration *measured to complete*
   times a stated factor. Here: the previous configuration completed 4 cycles
   inside 2^20, so per-cycle consumption is at most 262,144. The new budget
   allows 2^26 / 64 = **1,048,576 per cycle — at least 4× what was measured to
   suffice**. 2^26 is 2^27 below the mechanism's own ceiling of 2^53−1, so this
   is nowhere near the number ADR-0195 warns is a hang rather than a bound.

## Consequences

- `AIUEOS_NATIVE_FUEL` defaults to 67108864 (2^26) instead of 1048576, and
  `debug-run-cycles` to 64. Both defaults carry the reason beside them, in the
  build script and in `tcp_stream.kotoba`, so the next person does not have to
  find this file to know why.
- **The per-object tiers of ADR-0033 and ADR-0034 are untouched.** Those are
  replenishing, per-invocation budgets on objects that trap into a live kernel;
  the rule there is the same rule — measured, with a stated margin — but the
  numbers do not transfer in either direction.
- The QEMU smoke still exits 33 with marker `MPRCD` at the new budget, so the
  boot path is unchanged by it.
- **The older build scripts keep 1048576 on purpose.**
  `build-kotoba-native-kernel.sh` (pin 13d2f5df), `-5cec.sh` and
  `build-kernel-oldloader.tmp.sh` target compilers whose `max-native-fuel` was
  still 2^20; raising their default would make them refuse to compile rather
  than build something different. The live route is
  `build-kotoba-native-boot-46eeedae.tmp.sh` (pin 94f8fe37), and that is the
  one that moved.

## What this does not claim

- **2^26 is not a bound derived from analysis.** It is a measured-completing
  configuration times four. If `receive-established` ever becomes estimable,
  the budget should be re-derived from that and this number retired.
- **Raising fuel did not make the node reachable — it made it reachable nine
  times as often.** 0.91 % is better than 0.1 % and is still a machine that is
  mostly rebooting. The dispatcher must still send across the whole window
  rather than in a burst after the announcement.
- **The period doubled and this ADR does not explain it.** Uptime grew by
  173 ms; the gap between runs grew from 19.7 s to 39.8 s — twenty seconds that
  the longer run does not account for. It is recorded because it was measured,
  not because it is understood. The reboot path (reset → firmware → DHCP →
  TFTP) is where to look, and it is not this change's subject.
- Nothing here says what the *right* duty cycle is. The reboot, not the run,
  dominates the period; a node that wants to be reachable most of the time
  needs a run that does not end, which is a different decision about fuel
  replenishment and about why the kernel resets at all.
