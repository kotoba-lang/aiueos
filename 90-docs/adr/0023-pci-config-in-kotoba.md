# ADR-0023 — PCI configuration space moves out of C

- Status: accepted
- Date: 2026-08-06
- Extends: ADR-0015 (the honest C boundary); root ADR-2608060100 (`kernel-in-*`)

## Context

The owner asked why aiueos needs C at all, and directed the work at compiling
the whole OS memory-safely in Kotoba.

The answer was narrower than "kernels need C". A zero-C boot path already
existed and was enforced; the existing C was already *decision-free* (every
judgement — crypto verification, admission, capability planning, dispatch — was
already a Kotoba object). What kept the C was one missing primitive: the kernel
target had `kernel-out-u8`/`kernel-out-u32` and **no port read**, and PCI
configuration space is a write to 0xCF8 followed by a **read** of 0xCFC. With
write-only port I/O, PCI enumeration could not be expressed in Kotoba at all —
and enumeration is the root of every driver.

Root ADR-2608060100 added `kernel-in-u8`/`kernel-in-u32`. This is the first use.

## Decision

**Move PCI configuration access into Kotoba.** `config_read`/`config_write` in
`kernel/pci.c` become thin `uint8_t` wrappers over
`kotoba_aiueos_pci_config_read` / `_write`; the port arithmetic, the `out`, and
the `in` all move to `kotoba/pci-config-{read,write}.kotoba`. The inline asm
`out32`/`in32` helpers and the `PCI_CONFIG_ADDRESS`/`PCI_CONFIG_DATA` defines
are deleted — `pci.c` no longer contains a port-I/O instruction or a port
number.

This is the first **mechanism** to move, not another decision. It is also where
the mechanism/decision line stops being a slogan: the input-domain check moved
with it.

**Out-of-domain calls do no port I/O at all** (read returns `0xFFFFFFFF`, write
returns 0). The rationale is aliasing, not tidiness: only the offset is masked,
so an over-wide field *carries into its neighbour* — `dev 32` sets bit 16 and
reads bus 1; `fn 8` sets bit 11 and reads device 1. An unchecked write would
mutate a device that was never named. `0xFFFFFFFF` is the bus's own "nothing
here", which `aiueos_pci_enumerate` already tests for, so the refusal lands in a
path callers have.

## Consequences

- **`AIUEOS_PCI_OK bounded-scan virtio-vendor=1af4`** with enumeration driven by
  Kotoba, and every device downstream still found: virtio-net, IPv4 and TCP all
  still pass, and the full gate exits 0. This is a loud test by construction —
  if the Kotoba address word were wrong, enumeration would find nothing and the
  entire device stack would collapse.

- This is also the **first execution of `kernel-in`**. ADR-2608060100 could only
  claim correct encoding and admission; 8192 config reads per boot now execute
  it for real.

- **A latent fuel bug was found and fixed, in already-landed work.** Every
  function prologue decrements a fuel counter and traps on `ud2` at zero, and
  only objects in `kotoba-native`'s `bounded-memory?` set get a replenish in
  their wrapper. The IPv4 and TCP objects were *not* in that set. They worked
  anyway — by accident: they run after catalog admission, which calls
  `aiueos-sha256` and sets the shared counter to 10,000,000, so they were riding
  another object's replenish, with nothing asserting that ordering. PCI exposed
  it because enumeration runs 8192 times *before* anything replenishes.

  The frame-walkers now sit in the 4096 tier rather than 1024: a checksum over a
  1500-byte payload is ~750 recursive calls at one fuel apiece, and
  `tcp-segment-valid` runs two. 1024 clears a small frame and traps on a full
  one — a size-dependent failure that would read as a protocol bug.

- **This probably explains the unlanded X25519 fault too.** That object is not in
  the replenish set either, and its self-test was placed *before* catalog
  admission, so it ran on the initial 512 fuel while needing millions of
  operations. The observed `AIUEOS_EXCEPTION_FAIL unexpected-vector` is
  consistent with `ud2` (vector 6, which the kernel only expects from its final
  probe). Consistent with — not confirmed; that object has not been retested.

- **The C is not gone.** `pci.c` still owns BAR mapping, virtqueue setup, DMA
  page handling and every driver. The next primitive is runtime-established MMIO
  regions: a bounded load must NAME a region, so a BAR address discovered at
  runtime still cannot be expressed. After that, interrupt entry, MSR access and
  `lgdt`/`lidt`.
