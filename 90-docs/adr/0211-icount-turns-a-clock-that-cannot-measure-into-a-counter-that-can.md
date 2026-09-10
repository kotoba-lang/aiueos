# ADR-0211 — icount turns a clock that cannot measure into a counter that can

- Status: accepted
- Date: 2026-09-10
- Related: ADR-0165 (two arms that agree on the wrong fixture), ADR-0116
  (the exact-artifact benchmark), ADR-0202 (the board computed the answer),
  ADR-0112 (superseded as C-free evidence)

## Context

The question arrives as a performance question — how much slower is aiueos
than a Linux server, for code the amu native compiler produced? It has been
asked of the K16, of a Ryzen 5 6600HS, and of QEMU. Every form of it runs into
the same two walls.

**There is no time unit on this side.** ADR-0116 fixed that missing timing is
`N/A` and never zero, and that raw TSC cycles stay raw until a calibrated
monotonic frequency exists. ADR-0202's physical board reports TSC deltas
(800–1344 cycles for four micro-infer jobs) and says in the same breath that
they are a serialized TSC delta, not a wall time.

**And under TCG the clock is worse than absent — it is confident.** ADR-0165
measured four runs of one fixture at ratios 1.17, 1.45, 1.55, 1.57 and named
the spread as the load rather than the code, then concluded that the
defensible statement about speed is a COUNT.

## What was measured (2026-09-10)

Two things, both on this workstation, both reproducible by
`os/aiueos/scripts/smoke-qemu-target-parity-icount.cljs`.

**1. The two targets share an emitter, byte for byte.** The code image
`target-parity-core.kotoba` emits for `x86_64-linux` is 180 bytes; those exact
bytes appear at offset 93 of the object the same source emits for
`x86_64-aiueos-kernel-v1`, and its 127-byte compute body appears again at 4173
inside the bootable probe image. `kotoba.compiler.nbb.cli` already said this in
a comment ("nothing about code generation kept them off this route"); it is now
a measurement that a mutant fails.

The consequence is the useful part: `x86_64-linux` is `:abi :sysv` +
`:runtime :kototama-linux-supervisor-v1` and `x86_64-aiueos-kernel-v1` is
`:abi :aiueos-kernel-v1` + `:runtime :none`, and **the difference between them
is entirely at the boundary and nowhere in the loop.** An OS comparison here
cannot be a codegen comparison.

**2. `-icount shift=0,sleep=off` converts guest `rdtsc` from a host clock into
an instruction counter.** Three runs printed `000001EA000003AC` byte for byte.
Sizes 30 / 60 / 90 land on 490 / 940 / 1390 — a straight line at 15.0
instructions per iteration over a fixed 40. The same image booted without
icount printed `00003A98` then `000032C8`: a ~30% spread in which the
30-iteration fold reads HIGHER than the 60-iteration one, which is an ordering
a clock cannot have and a translator can. The probe's own check catches that
inversion and exits 51 rather than 33, so the noisy arm names itself.

This matters beyond aiueos. The workspace's benchmark ladder is refused for
host load routinely — amu-rank tick 358 refused at load1 21.18 on the morning
this was measured. **An icount count is immune to that**, because it is not
measuring time at all.

## Decision

1. Where a number is wanted from a QEMU guest, use `-icount shift=0,sleep=off`
   and report a COUNT. Do not convert it to ns, and do not compare it with
   silicon numbers from the codegen ladder — TCG prices a 256-bit vector
   operation at about what it prices a scalar one (ADR-0165), so anything
   about ILP, cache or branch prediction is invisible or inverted here.
2. Keep the two-size difference and the untimed warm-up. ADR-0165 measured
   that without a warm-up a 256-element fold reported 788,000 ticks against a
   4096-element fold's 279,000, because the first pass pays for QEMU to
   translate the sequence and that cost exceeds the loop.
3. The smoke proves its own containment check can go red on every run, by
   compiling a one-constant mutant and requiring the original bytes to be
   absent from it — and refuses (exit 3, neither 0 nor 1) if the mutation
   matched nothing, so that a red result cannot be red for the wrong reason.

## What this does not say

**It does not compare aiueos with Ubuntu.** The comparison people want is an
aiueos capability call against a Linux syscall, and the aiueos side does not
exist to be measured: this repo's README says kernel execution is "not yet —
context switch, preemptive scheduler, ring 3, syscall entry/exit, capability
handle table all still reference-profile only", and ADR-0112, which had the
general CPL3 signed-ELF transaction provider, is superseded as C-free evidence
with a successor that "does not yet reproduce" it. Today's only aiueos arm is a
CPL0 kernel object, which pays no privilege transition; setting that against a
Linux userspace process would not be a comparison. The smoke prints this as
`NOT-MEASURED` with its precondition rather than printing a ratio.

The precondition is an executable CPL3 path, so `x86_64-aiueos-user-v1` has a
kernel to run under. That target's packaging was still being repaired on the
day this was written: a `cap-call` source exited 70 with `:kotoba/internal-error`
under `package-user` while the same source compiled for wasm32, `x86_64-linux`,
`aarch64-macos` and both aiueos kernel targets.

Nothing here is a claim about the physical K16, about a Ryzen 5 6600HS (which
has no probe receipt in this workspace at all), or about real silicon. This
workstation is the QEMU host; QEMU is not P5.

## Evidence

    PARITY-CODEGEN linux-image=180 compute-body=127 core-object@93 probe-image@4173
    PARITY-DISCRIMINATES mutated-body@-1 (-1 required)
    IMAGE-FRESH artifacts=1
    ICOUNT-DETERMINISTIC runs=3 identical=true status=[33 33 33] console="000001EA000003ACIP"
    ICOUNT-COUNT small=490@30it large=940@60it per-iteration=15.00 fixed=40.00
    HOST-CLOCK-ARM status=51 console="00003A98000032C8IP" differs-from-icount=true
    NOT-MEASURED boundary-cost aiueos-capability-call vs linux-syscall
    SCANNED 6
    TARGET-PARITY-OK codegen-identical and count-deterministic

Both red directions were exercised while landing. Mutating the probe's compute
body to `(* i 5)` produced `probe-image@-1`, exit 1, and two independent
findings — the containment check and the guest's own arithmetic check — while
the instruction count stayed `000001EA000003AC`, because a different multiply
constant is the same instruction shape. The counter counts instructions; the
guest checks the values.

## Two things that happened while landing, recorded rather than chased

**`CLJC contract tests` is red on main and was red before this branch.** Run
34435926740, on the base commit 12c4ba6 (Merge #332), has
`CLJC contract tests -> failure` with `EDN examples and docs` and
`aiueos UEFI bare-metal smoke` both green — the same shape this branch's run
shows. Documented here rather than fixed, so that the next reader does not
attribute it to the parity smoke; and stated explicitly because a PR that says
nothing about a red check reads as if it had none.

**This ADR was 0210 for about forty minutes.** ADR-0210 (`a stick that asks`)
landed on main from a parallel session while this was being written, and the
collision would have been SILENT: two files named `0210-*.md` are two different
paths, so git merges them without a word and the number stops identifying a
decision. Renumbered to 0211 after merging origin/main. The root CLAUDE.md's
rule against bare `ADR-<number>` ids is the same failure seen from the id side.

## Not done

The smoke is not registered as a fleet gate. It needs a QEMU with OVMF and an
amu checkout on the node, and its inputs live under west-managed `orgs/`, which
the fleet tree does not carry. Registering it needs those two facts fixed
first, not a line in `gates.edn`.
