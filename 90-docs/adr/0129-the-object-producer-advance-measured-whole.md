# ADR-0129 — The object-producer advance, measured whole instead of sampled

Date: 2026-08-31

## Status

Accepted as a measurement. It does NOT take the advance, move a pin, or touch
a committed object. What it does is replace a five-object sample with the
whole inventory, because the decision that was deferred on that sample needs
the rest of it.

## Context

`reproduce-kotoba-kernel-object.sh` pins amu `9cf3a0a` and its comment records
why the pin has not moved:

> THE FULL ADVANCE WAS MEASURED AND NOT TAKEN. […] Five objects were compiled
> there and compared against the checked-in bytes […] ALL FIVE DIFFER. Taking
> that advance means regenerating every object in this script and every pinned
> digest in `build-uefi.sh`, which is a change to the shipped kernel and not a
> side effect of adding DHCP.

That reasoning is sound and the conclusion may well still be right. But five
of sixty-six is a sample, and "all five differ" supports "some differ", not
"all differ" — and it cannot say anything at all about a third outcome the
sample never contained.

## What the whole inventory says

`os/aiueos/scripts/measure-object-producer.cljs` compiles every committed
object with a given compiler and compares the bytes. Against amu `0085e138`,
with the same flags `reproduce-kotoba-kernel-object.sh` uses:

| | |
|---|---|
| Objects with a committed `.o` and a sibling source | **66** |
| Reproduce byte for byte | **4** — `broker-admit`, `ime-romaji`, `scanout-bind`, `session-restore` |
| Differ | **58** |
| **Fail to compile at all** | **4** — `dhcp-option-u32`, `dhcp-reply-valid`, `ecdsa-p256`, `ecdsa-p256-sign` |

Two things here were not visible from five.

**Four objects no longer compile.** Exit 70, `:kotoba/internal-error`,
"internal compiler error", `:details {:entry main}` — a crash, not a subset
rejection, on four objects the pinned compiler builds without complaint. Same
flags, same sources. That is a regression in amu somewhere between `9cf3a0a`
and the tip, and it is a blocker for the advance in a way that differing bytes
is not: differing bytes need re-measurement, a crash needs a fix. The two DHCP
objects are the ones ADR-0076 added; `ecdsa-p256` and `ecdsa-p256-sign` are the
signature path.

**The 58 that differ all get smaller, and none stays the same size.** The size
delta runs from −8 to −10,408 bytes; the total goes from 264,768 to 160,488,
a 39% reduction. The earliest difference is at offset 40 — past the ELF header,
in `.text`. This is consistent with codegen improvement rather than drift, and
it is the shape you would want the advance to have. It is also why every pinned
digest in `build-uefi.sh` moves.

Note that reproduction is not explained by an object being trivial:
`broker-admit` has no `:export` clause, and neither do the four that crash.

## What this changes about the decision

Nothing yet, deliberately. It converts "we would have to regenerate
everything" into a list with four names on the blocking end of it. The order
is now visible: the four internal-compiler-errors have to be fixed upstream
before the advance is even possible, and only then does the question of
re-measuring 62 objects and their digests arise.

## What it does not claim

The 58 differing objects were not booted, executed, or otherwise shown to be
correct — only to be different, and smaller. A smaller object is not a better
one until something runs it. This measurement says what the advance would
disturb, not whether the result would work.

The receipt is `qualification/object-producer-measurement.edn` and it names
the compiler it measured, because a reproduction count without one is a number
with no closure.
