# ADR-0014 — Self-owned VMM ("hvt tender"): owning the hypervisor side of aiueos virtualization

- Status: **accepted** — 2026-07-17 owner decision; option C V0 authorized
  (Linux/KVM, JVM-FFM, receipts required), options B/D stay live until the
  V2 checkpoint
- Date: 2026-07-17 (proposed and accepted same day)
- Deciders: Jun Kawasaki
- Scope-out origin: ADR-0011 Phase 1 ("a separate, multi-week project-scale
  ADR of its own — not attempted in this pass"). This is that ADR.

## Context

### 1. Every virtualization path in this stack today *consumes* an external VMM

`aiueos.vm` plans and launches **QEMU** (`accel=hvf` on macOS, `kvm:tcg` on
Linux). ADR-0008/0011/0013 all use QEMU as the boot-evidence oracle; ADR-0013's
release gates say "successful QEMU boot is the minimum evidence." ADR-0010's
`computer:vm` surface names a **Parallels/QEMU microVM** backing — still future
work, and also a consumer. A 2026-07-17 workspace-wide search confirms nothing
in `kotoba-lang` or `cloud-itonami` implements the monitor side of
virtualization: we boot *inside* VMs; nothing of ours *hosts* one.

### 2. The vocabulary and the named gap

Superproject ADR-2607022400 (accepted 2026-07-02) adopted the Solo5 split:
**tender** = the deliberately non-sandboxed native mediator (`hvt`
hardware-virtualized, `spt` seccomp-sandboxed); **guest** = aiueos wasm
components. It flagged the actual `hvt` tender as unstarted. ADR-0011 Phase 0
delivered the *hosted-Linux* tender (PID 1 + VFIO); its Phase 1 explicitly
deferred the hardware-virtualized piece — "a minimal native boot stub (CPU mode
setup, memory map, hypervisor-guest trap handling)" — to a project-scale ADR.
ADR-0013 then settled the **guest** side: aiueos owns the bootable product,
`kotoba-lang/compiler` owns freestanding targets, bare-metal kernel Phases 1–6
are underway. The **host/VMM** side remains the only undesigned layer.

### 3. Precision: "hypervisor 自作" means the tender/VMM, not a type-1 hypervisor

In the Solo5 framing the *hypervisor* is the OS's virtualization facility —
Linux **KVM** (`/dev/kvm`), macOS **Hypervisor.framework** (HVF). The *tender*
is the small userspace VMM that creates the VM, maps guest RAM, loads the guest
image, runs vcpus, and services exits. Parallels Desktop and VMware Fusion on
modern macOS are exactly this shape (userspace monitors over Apple's
hypervisor APIs). What is on the table here is a **self-owned tender/VMM**
riding KVM/HVF — never a from-scratch type-1 hypervisor (firm non-goal, §
Non-goals).

### 4. Constraints inherited from the superproject (owner rules)

- **No new Rust/C crates** to fill app-level gaps (owner rule 2026-07-10).
  ADR-0011 already noted the one arguable exception: *adopting* upstream
  Solo5's existing `hvt` (consuming a public, minimal C substrate, not
  authoring one).
- **Runtime priority** kotoba wasm > clojurewasm > ClojureScript > nbb, with
  JVM as a last-resort *compat layer* for hardware access. The established
  precedent is `aiueos.vfio`: raw `ioctl`/`mmap` from Clojure via
  `java.lang.foreign` (FFM) — "clj on clj."
- New scripts/harnesses are nbb `.cljs`, never `.sh`/`.mjs` (owner rules
  2026-07-14 / ADR-2607100100 M2).

### 5. What a minimal VMM actually is (scope reality check)

Open the facility (`/dev/kvm` ioctls: `KVM_CREATE_VM`,
`KVM_SET_USER_MEMORY_REGION`, `KVM_CREATE_VCPU`, mmap'd `kvm_run`, `KVM_RUN`;
HVF: `hv_vm_create`/`hv_vm_map`/`hv_vcpu_run`), map guest RAM, load the guest
image, set initial vcpu state (or run firmware), then loop servicing exits —
PIO/MMIO (the virtio transport), halt/shutdown, IRQ injection. The guest-side
entry contract (load ELF/PE, long-mode setup, boot-info handoff) is precisely
ADR-0011's deferred "native boot stub." Note the symmetry dividend: the
portable virtio *protocol* logic ADR-0011 ported to CLJC (queue layout,
descriptor validation) is the same logic a device model needs — one codebase
can drive both the guest and the host side of the queue.

## Options

**A. Status quo — QEMU only.** Zero new code; battle-tested device models;
but the largest possible TCB for the job, an external binary dependency, no
ownership of the layer ADR-2607022400 named, and `computer:vm` (ADR-0010)
stays hostage to Parallels/QEMU availability.

**B. Adopt upstream Solo5 `hvt`.** Existing, minimal, public C tender —
consuming, not authoring, native code (the carve-out ADR-0011 pre-argued).
But: Solo5 defines its *own* guest boot/hypercall ABI, which is not the
ADR-0013 kernel's UEFI/BIOS entry contract — making the aiueos kernel a Solo5
guest, or teaching `hvt` to direct-load our kernel, is C authorship in all but
name. Upstream `hvt` targets Linux/KVM and the BSD `vmm` APIs; no maintained
macOS/HVF backend as of this writing.

**C. Self-authored FFM tender — `aiueos.hvt` (Clojure, JVM-FFM).** The
`aiueos.vfio` technique applied one layer down: FFM downcalls against
`/dev/kvm` first (Linux), HVF later. Pros: satisfies the no-new-Rust/C rule
outright; plan-as-data + exit-log receipts fall out of the existing aiueos
discipline (`aiueos.vm` plans, ADR-0001 audit); virtio protocol logic is
already in CLJC. Cons: a JVM sits in the vcpu exit loop — acceptable for boot
gates and `computer:vm` interactive fidelity, not yet argued for production
I/O paths; macOS HVF requires the `com.apple.security.hypervisor` entitlement
codesigned onto the JVM binary (a real operational wrinkle — Linux-first);
greenfield systems code with all that implies.

**D. Compiler-emitted native tender.** Once `kotoba-lang/compiler` freestanding
targets (ADR-0013) mature, a native *userspace* target is strictly smaller
than a kernel target; the tender could eventually be kotoba-emitted. Farthest
out; not startable today.

## Decision (accepted 2026-07-17)

Phased, Linux/KVM-first, with option C as the spike vehicle and B/D as
checkpointed alternatives:

- **V0 — KVM spike (`aiueos.hvt`, JVM-FFM):** boot a minimal test guest (a
  few hundred bytes of long-mode code writing to the serial port) via
  `/dev/kvm`; then direct-load the ADR-0013 kernel image, implementing the
  boot-info handoff as data. Gate: the tender boots the same image QEMU boots
  and produces the same first-N serial bytes, with a run receipt.
- **V1 — device model:** virtio-console + virtio-blk backed by the ADR-0011
  CLJC virtio protocol logic (host side of the same queues). Gate: ADR-0008's
  block/console smoke passes under `aiueos.hvt` as it does under QEMU.
- **V2 — checkpoint:** measure V1 against adopting Solo5 (B) and the compiler
  trajectory (D); decide whether to proceed to a macOS/HVF backend and whether
  ADR-0010's `computer:vm` backing rides `aiueos.hvt` or stays
  Parallels/QEMU. QEMU remains the CI evidence oracle for ADR-0013 gates
  until the tender demonstrably reproduces them.

Verification harnesses are nbb `.cljs`; every phase lands with plan-as-data,
run receipts, and audit events (no screenshot-only or file-shaped evidence,
per ADR-0013's own standard).

## Non-goals (firm)

- No from-scratch type-1 hypervisor; we always ride KVM/HVF.
- No general-purpose desktop virtualization product (no Windows/macOS guests,
  no GUI VM manager) — the guests are aiueos images and `computer:vm` QA VMs.
- No removal of QEMU from CI evidence gates in this ADR's lifetime.

## Consequences

- (+) Closes the last undesigned layer of ADR-2607022400's vocabulary: tender
  (`hvt`) becomes ours, guest is already ours (ADR-0013), the protocol between
  them is already CLJC (ADR-0011).
- (+) `computer:vm` (ADR-0010) gains a self-owned, audited backing path
  instead of depending on Parallels licensing or QEMU's full device zoo.
- (+) The no-new-Rust/C rule is honored without a waiver (option C), while B
  remains available under the "consume upstream substrate" carve-out if the
  spike loses.
- (−) A JVM inside the vcpu exit loop is unproven for anything beyond boot
  gates; the ADR deliberately defers production I/O claims to V2 measurement.
- (−) macOS/HVF (the actual daily-driver host) is deferred behind the
  entitlement/codesigning question; until then macOS keeps using QEMU+HVF.
- (−) This is TCB code by definition (the tender is the one deliberately
  non-sandboxed layer) and must be reviewed as such, like the ADR-0010
  provider backings and Phase-7 MMIO/DMA adapters.

## Decision gate

As proposed, this ADR was escalated for owner judgment per the 2026-07-10
owner rule (gaps the sanctioned runtimes cannot obviously fill are not
self-granted). Accepting it means: option C V0 may start (Linux/KVM, JVM-FFM,
receipts required); options B/D stay live until the V2 checkpoint. Rejecting
it means: option A stands, and `computer:vm` proceeds against Parallels/QEMU
when scheduled.

**Outcome — accepted by owner, 2026-07-17.** Option C V0 is authorized;
tracked as kotoba-lang/aiueos#110. Practical note recorded there: V0 needs a
Linux/KVM host — the primary dev machine is macOS, whose HVF backend is
deliberately deferred to V2, so the spike runs on a Linux VM or remote Linux
host with `/dev/kvm`.

## References

- ADR-0008 (bootable image + virtio guest drivers), ADR-0010 (`computer:vm`
  backing, increment 4 ⏳), ADR-0011 (Phase 1 scope-out; CLJC virtio protocol
  port; `aiueos.vfio` FFM precedent), ADR-0013 (guest-side ownership, release
  gates, compiler freestanding mandate).
- Superproject `90-docs/adr/2607022400-kototama-unikernel-tender-runtime-vocabulary.md`
  (tender/guest split, `hvt`/`spt`, Solo5 survey).
- Linux `Documentation/virt/kvm/api.rst`; Apple Hypervisor.framework
  (`hv_vm_create` et al., `com.apple.security.hypervisor` entitlement);
  Solo5 `docs/architecture.md` (`hvt` tender).
