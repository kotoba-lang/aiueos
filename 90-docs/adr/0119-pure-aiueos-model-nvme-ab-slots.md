# ADR-0119: Pure AIUEOS streams model updates into guarded NVMe A/B slots

Date: 2026-08-29
Status: accepted; UEFI/NVMe adapter implemented and QEMU-qualified, K16 write unverified

## Context

ADR-0117 loaded a split 10.9 GiB model into one contiguous allocation. That is
not a practical 16 GiB K16 update path. ADR-0118 selected immutable Kotobase
CID blocks and an IPNS head, but deliberately left the physical writer red.
An update must not overwrite the model the node can still boot, and the loader
must never interpret any convenient internal disk as its cache.

The K16 firmware already exposes NVMe namespaces through UEFI. Using that
mechanism before `ExitBootServices` keeps this slice pure AIUEOS: no Ubuntu,
Linux filesystem, init system or userspace downloader is introduced. It does
not claim a native post-handoff NVMe driver.

## Decision

1. AIUEOS writes only a non-removable, writable, logical UEFI Block I/O handle
   whose device path contains an NVMe namespace and whose first block is a
   valid `AIUEOS-MODEL-AB1` anchor. It does not format or discover a target by
   free space, position, size or disk model.
2. The partition has two selectors, two slot headers and two aligned equal-size
   data slots. Data is streamed into the inactive slot in bounded memory.
3. Activation order is data write, flush, full SHA-256 readback, slot-header
   write/readback, then alternate-selector write/readback. The old selector,
   header and data remain intact until the final selector is durable.
4. Boot selects the highest valid selector whose generation, slot, byte count
   and SHA-256 exactly match a valid slot header. A torn or corrupt newest
   selector falls back to the prior generation. Partial data has no selector
   and is not bootable.
5. Normal bytes will come from the admitted Kotobase/IPFS HTTPS channel. For
   this first physical qualification, the same exact three-file FAT32 bundle
   may be streamed from removable USB. USB is a source, never the cache target.

## Evidence and limits

`model_slots.c` passes a host state-machine gate that forces interruption at
selector commit and corruption of the newest selector. The QEMU gate boots the
real UEFI application against a QEMU NVMe namespace, imports generation 1 to
A, generation 2 to B without changing A, and refuses a corrupt generation 3
while generation 2 remains active.

This is physical-adapter-shaped evidence, not a completed K16 disk write. The
K16 still needs a deliberately provisioned model partition, a physical boot,
and saved receipt. The existing RTL8125/TLS direct-HTTPS profile also remains
transport-only until certificate-chain and hostname admission are completed.
No Qwen runtime or generation speed follows from storing the artifact.
