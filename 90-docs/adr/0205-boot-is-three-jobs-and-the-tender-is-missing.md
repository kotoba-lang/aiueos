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

## Step 1 is landed: the image entry is a host loop

`kotoba.compiler.packaging.pe32plus/k16-preflight-tokens` now emits, in the
`--k16-preflight` image only:

```
        movabs r9, 0x128000        ; the guest's context
        mov    rax, [r9+8]         ; the budget the COMPILER sealed
        mov    [rip+tender-fuel], rax
tender_step:
        lea    rdi, [rip+boot-info]
        movabs r9, 0x128000
        mov    [r9+0x50], rdi
        mov    rax, [rip+tender-fuel]
        mov    [r9+8], rax         ; replenish
        movabs rax, returnable-entry
        call   rax
        mov    r15, rax
        cmp    r15, 250            ; "that was one step; call me again"
        je     tender_step
```

**The loader does not know the number.** It reads the word the compiler wrote
and writes that word back. This is not a stylistic choice; it removes two
whole failure modes. The loader cannot disagree with the artifact — that word
is the same one `elf64/artifact-fuel` checked `:limits :fuel` and `:fuel-abi
:initial` against, so it *is* the receipt. And there is no immediate to
overflow.

The first version of this did use an immediate: `mov qword [r9+8], imm32`,
with a guard refusing any budget at or above 2^31 because the field is
sign-extended. That guard was correct and the design was wrong. ADR-0203
raised `ir/max-fuel` to 2^53-1 specifically so the bound would stop being a
knob; a tender whose ceiling is the width of one instruction field puts the
knob straight back, one layer lower, where nobody would look for it. **Do not
re-introduce the immediate form.**

The snapshot is deliberately *outside* the loop and the replenish inside: the
budget is read once, before the guest has had an opportunity to touch
anything. `rdi` and `r9` are re-materialised every iteration because both are
caller-saved and the guest may clobber them.

`250` (`FA`) is the sentinel because the panel renders the low byte of this
same register as STATUS. A sentinel colliding with an ordinary return would
make a running node and a finished one print the same two characters.

### What was measured, 2026-09-08

| control | result |
|---|---|
| non-preflight image, unmodified compiler vs modified | **byte-identical**, `b3cc1297...` both — the change is scoped to `--k16-preflight` |
| preflight image byte scan | exactly one snapshot (context `0x128000`), one replenish, one `cmp r15,250 / je` |
| `je` target vs loop head | target `0x384`; snapshot begins `0x36f` and is 21 bytes, ending `0x384`. The branch lands on the instruction after the snapshot, which is the loop head |
| sealed word in `KERNEL.ELF` at `0x128000+8` | `1073741824` = 2^30, equal to the kernel verifier's `fuel=1073741824` |
| QEMU, preflight image `20728546...` (261,120 bytes) | exit 33, debugcon exactly `MPRCD` |

The 16-byte slot the tender saves into is at the **tail** of `.data`, after
every message, so boot-info, the memory map and all four messages keep the
offsets they had. A preflight image with the tender differs from one without
it in `.text` and in those 16 bytes, and nowhere else.

### What this does NOT show

**The loop has never been taken, and nothing here shows that it can be.** Two
independent reasons, both by construction:

- Under QEMU there is no RTL8125, so the preflight path branches to
  `:exit-boot` on the PCI ID mismatch *before* reaching the tender. The QEMU
  pass says the image is still structurally sound and boots identically. It
  says nothing about the loop.
- On the board the tender *is* reached, but the guest's `main` does not
  return at all — it runs `debug-run-cycles` and then resets. Zero iterations
  complete.

So the honest claim is exactly the one step 1 was for: **the mechanism is
present, and behaviour is unchanged because nothing yet returns the
sentinel.** A control that takes the branch is not available until step 2
makes the guest return per step. Until then, do not report that the tender
runs.

### The hardware measurement is pending, and why

The tender image (`36d81484...`, 261,120 bytes, compiler `a6102cda`) was
written to the PXE root at 16:42 JST and **has not been fetched.** The board
stopped speaking at `2026-09-08T07:02:39Z` (16:02 JST), 40 minutes before that
deploy, after fetching and booting the previous image (`0576a52e`): it sent one
`AIUEOS_NODE_HELLO_V1`, reached `state=live-not-ready enrollment-status=200
heartbeat-status=201`, and then went silent.

It is wedged, not off. Both host links report `status: active`,
`1000baseT <full-duplex>`, and both board MACs are still in the ARP cache; what
is absent is DHCP, netlog, bus3 and ICMP. A powered-down mini-PC would drop
link. This is the same failure ADR-0203 recorded once before and could not
attribute — it needed two power cycles then, and needs a person now.

So: **the tender is proven in the image and unproven on the machine.** Do not
close step 1 as measured-on-hardware until a boot of `36d81484...` appears in
the server log. Nothing here attributes the wedge to the tender — that image
has never run — and nothing here attributes it to `0576a52e` either; one boot
followed by silence is not yet a cause.

### On hardware, 2026-09-08 evening: the tender runs, and the reset is the wound

The tender image booted and the node came up:

```
AIUEOS_PXE_TFTP_OK bytes=261120
AIUEOS_NODE_HELLO_V1 boot=...
AIUEOS_MURAKUMO_RELAY state=live-not-ready enrollment-status=200 heartbeat-status=201
```

Behaviour is unchanged with the mechanism present, which is what step 1 asked.
Every boot since enrolls `200`/`201` with the node's own did:key.

What that exposed is a separate defect that had been eating the rig all day:
**the board stops at the platform reset.** The end-of-run path is

```
DE DE DE                 three fire-and-forget DMA transmits
(kernel-out-u8 3321 6)   0xCF9 <- 0x06 = SYS_RST|RST_CPU
223                      -> loader prints STATUS DF
```

Three facts bracket the failure to one instruction: `DE DE DE` reached the
wire, so the transmits were submitted; the panel showed `RTL8125` and **no
`STATUS`**, so `main` never returned, and the only statement between the last
DE and that return is the 0xCF9 write; and the panel text was still on screen,
so no POST happened and therefore no reset happened either.

`0x06` is a WARM reset and does not reset PCIe devices. Three transmits had
just been handed to the RTL8125, so the NIC is still bus-mastering when the CPU
is reset. It behaves like the race it is: DE-three-times landed at 15:10 and
the 19.7 s cadence ran until 15:43 before stopping for good.

The fix drains the last transmit descriptor's Own bit
(`wait-tx-complete-stream`, which already existed) before the write, with a D7
receipt emitted BEFORE the drain -- `stream-log` is itself a DMA submit, so a
receipt after the drain would re-arm what was just drained.

| image | self-resets observed |
|---|---|
| 1024 cycles | **0** across 3 power cycles |
| 64 cycles | 1 |
| 64 cycles + drain | **2** (21 s and 158 s apart), 3 boots, all enrolled |

Better, and not yet the steady 19.7 s cadence. After the last run the board
sent **no DHCP at all**, so it stops on its own rather than because the server
failed to answer.

### Two things measured today that were wrong when first claimed

**`AIUEOS_PXE_TFTP_FAIL stage=oack` is not a fault.** This firmware fetches in
two phases: an RRQ carrying `tsize` to learn the size, then a second RRQ
without it for the transfer. It abandons the probe TID with a TFTP ERROR
(`opcode-0005`) once it has the size, so **one benign FAIL per boot is normal**.
A reading of "50 OK / 61 FAIL" as a 45% success rate, and the load-starvation
hypothesis built on it, were both wrong. The diagnostic added that day is what
disproved them.

**Marker counts per boot are not evidence of where a run ended.** The netlog is
fire-and-forget UDP and ADR-0203 already recorded that only 216 of 259 runs
carried a DE *while the board kept rebooting throughout*. Separately, an
analysis that segments boots on time gaps misattributes markers across
boundaries, because lines without a timestamp inherit the previous one. The
non-lossy witness for "did the run end" is the PXE server's fetch log, and
nothing else.

## Order, and what not to do

1. ~~**The image entry becomes a host loop**~~ — **landed 2026-09-08**, see
   above. It is the minimal tender: replenish, call, re-enter.
2. ~~**The guest returns per step**~~ -- **on main 2026-09-08, and NOT YET
   MEASURED ON HARDWARE.** `stream-resident`'s end of run no longer writes
   0xCF9; it drains the last transmit and returns 250, and the tender
   replenishes and re-enters. QEMU passes (exit 33, `MPRCD`); the board was
   stopped when this landed and has not run it.

   ⚠ It reached main by accident. It was committed to the branch with "branch
   only, not merged" in its own message, and a later branch->main merge for an
   unrelated fix carried it along, because merging a branch merges everything
   on it. Nothing is being reverted -- the reset path it replaces is the one
   that demonstrably halts the board, and main is not deployed automatically --
   but "unmeasured" is a property of the change, not of which ref it sits on,
   and the merge message that says "on the branch" is wrong.

   The cost is explicit: **a deploy no longer takes effect by itself**, because
   the board stops re-fetching. `k16-control.cljs R --to 10.10.10.2:9000` still
   resets at the one remaining 0xCF9 site.

3. **The tender gains a definition table**, so "which code runs" is a name, not
   an image.
4. **The timer becomes the run-time bound**, borrowing `rt-kernel.kotoba`'s
   APIC and periodic release rather than writing a second one.
5. **Definitions arrive over LAN2.** Deploy without reset.

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
