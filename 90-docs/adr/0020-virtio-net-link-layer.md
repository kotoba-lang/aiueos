# ADR-0020 — The link layer: aiueos sends and receives its first real packet

- Status: accepted
- Date: 2026-08-05
- Extends: ADR-0019 (USB removable-media boot; named "no network stack" as the
  gap that stopped a USB-booted node from reaching murakumo.cloud)

## Context

ADR-0019 shipped a USB-bootable OS whose node could **decide** its murakumo.cloud
participation and could not **act** on it, for one reason: aiueos had no network
of any kind. PCI enumerated virtio RNG, BLK, INPUT and GPU; there was no NIC
driver, no frame path, no stack. `src/aiueos/net.cljc` is 41 lines of URL
allowlist for *host* adapters and has never touched a wire.

Reaching murakumo.cloud is a stack: link → ARP/IPv4 → TCP → TLS → HTTPS. Only
the bottom of it is decided here. The rest is named in Consequences so nobody
reads this ADR as "aiueos is on the network now".

## Decision

**Implement the link layer, and prove it against a real peer rather than a
loopback.**

1. **virtio-net driver** (`kernel/pci.c`), following the virtio-blk/rng pattern
   already in that file: modern PCI transport, VERSION_1 feature negotiation,
   two virtqueues (0 receive, 1 transmit). `prepare_queue` gained an index
   parameter — every device before this one used exactly one queue, so it
   selected queue 0 by construction; the old single-queue signature is kept and
   delegates.

2. **The receive buffer is posted before `DRIVER_OK`**, so a reply cannot arrive
   with no buffer to land in.

3. **Frame admission is a Kotoba object**, not C — matching the mechanism/decision
   split this OS is built on (ADR-0015). `kotoba/net-arp-reply-valid.kotoba`
   compiles to `x86_64-aiueos-kernel-v1`, exports
   `kotoba_aiueos_net_arp_reply_valid` through kotoba-native's reviewed
   allow-list, and is admitted by the same fail-closed verifier as every other
   object (`imports=0 relocations=1`). C performs the bounded DMA and hands the
   bytes over; it decides nothing.

4. **The exchange is ARP, deliberately.** It is the smallest thing that proves a
   real peer answered — 14 bytes of Ethernet, 28 of ARP, and no IP stack
   anywhere. Under QEMU's SLIRP (`-netdev user`) the gateway 10.0.2.2 answers,
   with no host network access involved.

5. **The admission checks every field of the reply, not just the opcode.** A
   device model that echoed our own broadcast back would pass an opcode-only
   check; requiring opcode 2 (reply, not the 1 we sent) *and* sender IPv4
   10.0.2.2 is what makes the evidence mean "a peer replied".

## Consequences

- **`AIUEOS_VIRTIO_NET_OK modern-pci rx/tx arp-reply kotoba-admitted`** — aiueos
  transmits and receives real frames on bare metal, and the received one is
  admitted by compiler-emitted Kotoba.

- **A NIC is optional and stays optional.** The gate attaches one only under
  `AIUEOS_TEST_NET=1`, so every existing gate boots the machine it booted
  before, and the kernel reports `AIUEOS_VIRTIO_NET_ABSENT no-nic-attached`
  rather than failing when none is present. Measured both ways: no-NIC boot
  green with `NET_ABSENT`, NIC boot green with `NET_OK`. An attached-but-broken
  device cannot pass as "no network" — presence is what is reported, and when
  present the exchange must have completed and been admitted.

- **This is the link layer and nothing above it.** A node still cannot reach
  murakumo.cloud. Remaining, in order: ARP cache + IPv4 (fragmentation,
  checksums), TCP (state machine, retransmit, windowing), TLS 1.3, HTTPS. Each
  needs its own evidence; none of it is claimed here.

- The driver **polls** rather than taking an MSI-X interrupt, unlike blk and rng.
  This is the first packet the OS has ever sent, and a completion that never
  arrives must fail the gate instead of parking the boot in `hlt` forever.
  Interrupt-driven receive is follow-on work.

- The MAC is a fixed locally-administered address and `VIRTIO_NET_F_MAC` is not
  negotiated: the peer replies to whatever source it sees, so nothing is read
  from device config space. A node that needs a stable identity on a real
  network will have to negotiate it.
