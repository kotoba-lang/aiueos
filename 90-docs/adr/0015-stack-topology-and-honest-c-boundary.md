# 0015 — Stack topology position, and an honest statement of the C mechanism boundary

Status: accepted
Date: 2026-07-24
Root authority: `com-junkawasaki/root` ADR-2607241100 (kotoba stack topology
and design cleanup). This ADR is the aiueos-repo mirror; the canonical
topology and the full cross-repo cleanup list live there.

## Position in the stack topology

```
kotoba    = language + datom model
compiler  = AOT compiler              (foundation; depends on nothing in the stack)
kototama  = Wasm tender               (depends on: aiueos — "aiueos decides, kototama enforces")
aiueos    = capability OS (THIS REPO) (deps.edn: security + chicory ONLY)
kotobase  = datom database            (depends on: kotoba, never the reverse)
```

Two invariants this repo owns:

1. **aiueos stays dependency-minimal.** The decision plane must never depend
   on `kototama`, `kotoba`, or `kotobase`. Enforcement layers import aiueos
   (kototama's `aiueos_adapter` does today); aiueos imports nobody's
   enforcement. This keeps the broker auditable in isolation.
2. **The compiler edge is an artifact edge, not a library edge.** The
   bare-metal kernel consumes compiler-emitted freestanding objects
   (`x86_64-aiueos-kernel-v1` / `x86_64-aiueos-user-v1` ELF64, fail-closed
   verified before link/load). aiueos never links the compiler as a library
   into the kernel; the boundary is verified bytes.

## Decision 1 — state the real C boundary instead of the "crt0 shim" story

The workspace-level rule (com-junkawasaki/root CLAUDE.md, ADR-2607198300 era)
describes the permitted non-CLJC/non-Kotoba code as "the minimal crt0-style
entry shim an OS executable format requires." Measured reality in this repo
(2026-07-24): `os/aiueos/kernel/` carries ~5,000+ lines of C/asm — `pci.c`
1178, `main.c` 690, `scheduler.c` 570, `syscall.c` 501, `paging.c` 425,
`entry.S` 372, plus multiboot/UEFI loaders. That is not a crt0 shim; it is a
real mechanism kernel.

The **actual** boundary — which the code already honors and CI gates prove —
is better than the stale claim, and should be stated as-is:

> **C/asm owns mechanism only** (register/MMIO access, GDT/IDT, paging
> primitives, APIC/SMP bring-up, virtio queue plumbing, context switch).
> **Every decision is compiler-emitted Kotoba**: SHA-256 and RSA-2048
> signature verification, ELF/catalog/journal admission, capability
> encode/admit/derive/revoke planning (generation-safe), scheduler dispatch
> planning, pointer/length window admission, syscall-range validation. The C
> substrate contains no digest, signature, admission, or capability logic.

**Decision:** this ADR is the authoritative statement of the boundary.
"Decision-free C mechanism" is the reviewable property — enforced by the
existing pattern that every new admission/validation path lands as a
`kotoba/*.kotoba` object with QEMU-gate evidence, never as C logic. The
root-repo ADR ledger gets a corresponding amendment (ADR-2607198300's
"kgraph unsupported on the native backend" claim is also stale — the
compiler's x86-64/aarch64 backends now implement and test
`kgraph-assert!`/`kgraph-get`/`kgraph-count`/`kgraph-entity-at`).

Growth direction remains: when the native backend gains an op family that
lets a C mechanism block become a plan/validate split (as happened for
SHA-256, RSA-2048, catalog admission, capability planning), migrate it. The
C line count going down over time is desirable; pretending it is already ~0
is not.

## Decision 2 — grant vocabulary joins the canonical capability schema

kototama's `aiueos_adapter` can translate only the 3 kernel capabilities
(`log-write`/`clock-monotonic`/`random-bytes`) into `HostCaps`; the other
`actor:host` imports have no aiueos-decidable counterpart and fall back to
caller-supplied caps — a hole in "aiueos decides." As the canonical typed
capability-descriptor schema lands (root ADR-2607241100, kototama ADR-0009),
aiueos extends its grant vocabulary so every hosted import is decidable here,
and the adapter's coverage becomes a generated, mechanically-checkable
mapping instead of a hand-maintained partial one.
