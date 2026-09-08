# ADR 0205: boot is three jobs wearing one coat, and the layer that should hold them is missing

Status: accepted. Date: 2026-09-08. Extends: ADR-0204.
Owner question: *"そもそも boot と fuel という考え方は適切なのかな? 根本的な
アーキとして、どう os, boot と aiueos, kotoba, kototama を成立させると良い?"*

## Is "boot" the right notion? No — it is three unrelated jobs bundled into one

Every ~20 seconds the K16 does all three of these at once, and it does them
together only because there is nowhere else to put any of them:

| what actually happens | why it is bundled into boot | where it belongs |
|---|---|---|
| **the fuel budget is refreshed** | an image's budget is a lifetime cap; the only way to get another is a new image | per step, held by the caller (ADR-0204) |
| **new code arrives** | deploy = rebuild a 261,120-byte image, PXE-fetch it, reset | a new definition CID arriving; no reset |
| **all state is erased** | a new boot nonce, a fresh TCB, nothing remembered | state should survive a code change |

Reading that table is the whole argument. **The reboot is not a lifecycle
event; it is the only mechanism this machine has, doing three jobs.** Each of
the three has a proper home and none of them is "restart the CPU".

The third row is the one that is easiest to miss and hardest to live with. A
node whose identity is regenerated on every code change cannot hold a job, a
session, or a lease — and everything ADR-0202/0203 fought (the boot-nonce
binding, `ready?` flapping, jobs dispatched at a machine that is already gone)
is that row.

## Is "fuel" the right notion? Yes, but it is answering a different question

Fuel is a **step counter**: a proxy for time, for a guest you cannot preempt.
That is exactly right for WASM, for an object called from C, and for the KIR
oracle — none of which can be interrupted. It also has a property a timer never
has: **it is checkable before the machine runs**, which is what
`kotoba.compiler.fuel-estimate` and the admission gate are for.

The property actually wanted at run time is different: *no step may wedge the
machine*. On hardware with an interrupt controller the honest primitive for
that is **preemption**, not a counter.

And this workspace already has it, in Kotoba, in this repository:
`os/aiueos/native/rt-kernel.kotoba` — *"the hard real-time vertical slice. The
source owns the interrupt table, APIC setup, fixed-priority dispatcher,
periodic release... no C ABI or hosted runtime is present."* A Kotoba kernel
that owns the APIC and releases work periodically exists and is built by
`build-kotoba-rt-kernel.sh`. **The K16 stream kernel simply does not use it.**

So the answer is not to choose. It is to stop making one mechanism do both:

- **fuel = the compile-time bound.** Static, checkable, per step, and the thing
  admission refuses on. It never needs to be large, because it bounds a step.
- **the timer = the run-time bound.** It is what actually stops a step that
  overruns, and it can do what fuel cannot: end the step *without* ending the
  machine.

Today the K16 uses fuel as a run-time bound, and the outcome when it fires is a
halt that needs a person at the power button. That is the whole cost of not
having the second mechanism.

## The layer that is missing is kototama

The names line up cleanly once the question is asked:

| layer | owns | K16 today |
|---|---|---|
| **kotoba** | definitions, effect rows. No addresses, no budgets, no loop | present |
| **kototama** | the **tender**: the context (fuel word, grants, definition table), re-entry per step, the only thing that must be trusted | **absent on hardware** |
| **aiueos** | capability providers — MMIO, DMA, link frames, page tables. Supplies effects, never budgets | present |
| **firmware / boot** | bring the CPU to where the tender can run. Once | present, doing three jobs |

kototama's own maturity ladder is R0 contract → R1 JVM/Chicory → R2
browser-native. **There is no hardware tier.** On the K16 the kernel *is* the
artifact, packaged straight into a PE image, so there is no host holding the
context, no place to install a handler, and no place to swap a definition.

That absence explains every symptom without further hypotheses. The budget got
sealed into the image because nothing else could hold it. Deploy means reboot
because nothing else can receive code. State dies on deploy because the thing
that would have kept it is the thing being replaced.

## What "成立させる" looks like

**The image contains a tender, not a program.** It holds the context and a
definition table, and it re-enters the guest per step. It is small, it is not
Kotoba (the same reason `kexe_loader.c` and the object wrapper are not), and it
is the only trusted mechanism — which is the point: *a handler inside the
computation is not a handler* (ADR-0204).

**The guest is content-addressed definitions with effect rows.** Unison's real
lesson is not only abilities: it is that code has no version and no rebuild,
only hashes and names. A node running a definition CID does not need a new
image to run a different one — it needs the definition and a name to move.
`amu` already emits definition CIDs and `:kotoba.output-set/v1`; the Execution
IR already carries `{program, input, state, runtime, policy, effects}`.

**A step is `state + event -> state' + effects`** — the shape ADR-2607201300
already prescribes for `kotoba/app`, bounded by fuel at compile time and by the
timer at run time.

**Deploy stops being a reset.** A definition arrives over the wire and the
tender moves a name. LAN2 already carries commands and already answers them;
this is that channel doing the job it was shaped for. Reset survives for what
only firmware can change.

## Order, and what not to do

1. **The image entry becomes a host loop** (ADR-0204 step 1). That *is* the
   minimal tender: replenish, call, re-enter. Nothing else is possible before
   it, and everything else is easier after it.
2. **The tender gains a definition table**, so "which code runs" is a name, not
   an image.
3. **The timer becomes the run-time bound**, borrowing `rt-kernel.kotoba`'s
   APIC and periodic release rather than writing a second one.
4. **Definitions arrive over LAN2.** Deploy without reset.

Not to do, each for a reason already recorded here:

- **Do not let the guest replenish its own fuel** (ADR-0204). It would work.
- **Do not build a second runtime.** kototama exists; what is missing is a
  hardware tier of it, not a new thing beside it.
- **Do not add a second store for artifacts.** The five canonical IRs share one
  physical plane by decision (ADR-2608160200).
- **Do not treat the reboot loop as the design.** It was a way to iterate
  deploys without a button, and it worked; it is not how a node lives.

## What this does not settle

Whether the tender should be emitted by the packager (as the object wrapper is)
or be a distinct artifact the loader hands control to. Both keep the handler
outside the computation, which is the property that matters; the choice is
about who owns the bytes, and it should be made with kototama rather than for
it.
