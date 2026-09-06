# ADR 0156 — LAN2 (bus3) debug plane: why the single-shot send emitted nothing, and how the census resolves it

Status: accepted (2026-09-06)

## Context

The GMKtec K16 has two RTL8125BG 2.5GbE NICs. bus2/0x02:00.0 carries the PXE
stream profile (wired to the Mac en15 = 10.77.0.1). This ADR is about the
second NIC, bus3/0x03:00.0 (wired to the Mac en8 = 10.10.10.1) as a LAN2 debug
/ control plane.

com-junkawasaki `orgs/kotoba-lang/aiueos`, branch `n2-tcp-stream-tranche1`.

### What was attempted (Phase 1, commit e09e4f1)

A minimal raw-Ethernet UDP :9000 log on bus3: the kernel extends the
low-memory page plan to pages 14..17 (fifteenth..eighteenth-start), allocates
a dedicated 16 KiB `bus3-dma-pages` window, requires `native.debug-link`, and
before entering the resident stream loop runs `debug-init` (rtl/rings-start on
bar-b) plus `debug-send` (one raw UDP frame to Mac en8 10.10.10.1:9000).

### Observed on the physical K16

1. The K16 screen shows "PREFLIGHT RTL8125" and stands still there.
2. The PXE server log shows the latest debug-EFI boot (TFTP bytes=207872 =
   the Phase-1 artifact) reaching ARP_OK -> NIC_40/43/44/45 -> TCP_OK ->
   STREAM_A1 -> 00 45 00 FF 19 52 A2 A3 A4 -> 21, repeated (the bus2 stream
   resident loop), while the screen stays on "PREFLIGHT RTL8125".
3. The Mac en8 never received the bus3 'Z' UDP :9000 frame (socat log empty,
   en8 ARP for 10.10.10.2 showed `(incomplete)`).

The screen freeze and the live bus2 netlog looked like a contradiction. It is
not.

## Analysis (Claude Opus 5, commit 05a3142)

### 1. The screen freeze is normal, not a hang

`kernel/` main has not returned since commit e39c453 made the stream resident.
The k16-preflight loader prints `STATUS XX` only after main returns. With the
stream loop running forever, main never returns, so the loader's status line
is never updated and the panel stays at the last checkpoint it printed
("PREFLIGHT RTL8125"). The kernel is advancing (the bus2 netlog proves it);
the display is simply not refreshed because no code path back from main runs
the loader's status print.

### 2. The Phase-1 bus3 DMA window named memory the kernel does not own

The Amu loader allocates exactly fourteen pages (receipt
`kernel_scratch.pages = 14`, i.e. physical-start .. +57344). `bus3-dma-pages`
pointed at `(- fifteenth-start 4096)` size 16384 — pages 14..17
(+57344 .. +73728) — which is **outside the loader-owned region**. Letting the
RTL8125 DMA into unowned memory is invalid even if `bar-b` decoded. The
Phase-1 single-shot could therefore not be trusted even if it had submitted.

### 3. The debug frame had no IPv4 header checksum

`build-udp-debug-frame` wrote the IPv4 header but left the checksum field
(bytes 24..25) zero. The Mac's `ip_input` drops a frame with a bad IP checksum
before any higher layer (socat on UDP 9000) sees it. So even a perfectly
transmitted frame would never reach the listener. (Claude stored 0x52AC.)

### 4. The Phase-1 send was observably silent by design

`debug-init`/`debug-send` ran inside a `let` whose results nothing observed.
Every failure mode — bar-b zero, MMIO undecoded, mac-valid false,
rings-start fault — looked identical from the outside (no frame, no log).

### 5. (Flagged, pre-existing, unverified) staging/tx-frame/rx-buffer aliasing

`stream-staging` = thirteenth-start = physical-start+49152 =
`rtl-dma-pages + 8192`, which is byte-for-byte the same address as
`stream-tx-frame` **and** as rx-descriptor-0's `rx-buffer-a` target (offset
8192) in the stream's own ring layout. So the staging buffer, the TX frame,
and RX descriptor 0's DMA target are one page. That could be eating received
bytes, which would be interesting given the loop keeps reporting `21`
("window exhausted, peer sent nothing"). Not traced by Claude; pre-existing
either way.

## Decision

1. **Diagnose before wiring DMA.** Replace the Phase-1 single-shot send with a
   read-only **census** emitted over the already-proven bus2 netlog. The census
   is one byte per bus, no PCI write, no MMIO write, no DMA:
   bit0=RTL8125 answers config space, bit1=BAR decoded, bit2=MEM-SPACE-ENABLE,
   bit3=BUS-MASTER-ENABLE, bit4=revision readable, bit5=MAC plausible,
   bit6=PHY link. A qualified NIC reads 0x7F.
   - The kernel emits `AIUEOS_STREAM_B7` (sentinel), then bus3's census, then
     bus2's census (the control; expect 7F) once, before the resident loop.
   - These ride the existing bus2 netlog, so no second wire or reboot beyond
     this one is needed to read them.

2. **Withhold bus3 DMA until it has a loader-owned window.** Do not re-enable
   the bus3 send until the census shows the NIC can DMA AND bus3 has an owned
   window. The cleanest source of an owned 16 KiB window is the K16 stream
   branch (bar-a > 0, so CR3 replacement never runs): pages 4..7 (`pt`,
   `reusable-page`, `frame-page`, `stack-page` = fifth-start..+32768) are
   loader-owned, already zeroed, contiguous 16 KiB, idle for the life of that
   branch. Point bus3-dma-pages at `(- fifth-start 4096)` inside the stream
   branch and delete the pages 14..17 extension. (Raising
   `kernel-scratch-pages` 14->18 touches pe32plus.cljc, the boot verifier,
   kernel.kotoba:389 and two contracts — no benefit here.)

3. **Fix the remaining bus3 send prerequisites** (once census says the NIC can
   DMA): a `pci-write bus 4 (bit-or (bit-and command 65535) 6)` to set
   MME+BME before MMIO, and a `(mmio-load-u16 mmio 108)` PHY link gate before
   submitting.

4. **Investigate the staging/tx-frame/rx-buffer-a aliasing** as a separate
   follow-up: it is a plausible contributor to the persistent `21` (bus2
   stream never sees the peer's SYN-ACK).

## The census answer (read on the K16, 2026-09-06)

The census EFI `810e0523…` booted and the three bytes arrived on the bus2
netlog, once, between `AIUEOS_NATIVE_TCP_OK` and the resident loop's first
`A1`:

```
AIUEOS_STREAM_B7      sentinel
AIUEOS_STREAM_7F      bus3 census
AIUEOS_STREAM_7F      bus2 census (control)
```

Order is `census-b` (bus 3) then `census-a` (bus 2), read from the source, not
inferred from the wire. **bus3 = 0x7F is every bit set**: the 8125 answers
config space, a memory BAR decodes inside the admitted window, MEM-SPACE-ENABLE
and BUS-MASTER-ENABLE are already on, ChipCmd reads back a revision, IDR0..5
holds a plausible unicast MAC, and PHYstatus reports link. The NIC can DMA.
bus2 reads the same, which is what makes the bus3 reading trustworthy rather
than a constant.

So the Phase-1 silence was not the NIC. It was the two faults in §2 and §3
plus the unobservability in §4.

Reading note: `grep` on this machine returns *no match* on `server.log` under a
UTF-8 locale, because the file carries a byte that is not valid UTF-8 and
macOS grep then reports every line as unmatched rather than erroring. `LC_ALL=C
grep` finds the census. A search that could not run and a search that found
nothing returned the same answer.

## Step 1 — the send, with an owned window and every stage observable

1. **Window.** `bus3-dma-pages` is bound inside the stream branch at
   `(kernel-subregion 4096 549755809792 (- fifth-start 4096) 16384)` =
   `fifth-start` .. +32768 = `pt`, `reusable-page`, `frame-page`, `stack-page`.
   The stream branch never replaces CR3 (that is the other arm of the `if`), so
   those four pages are allocated by the loader, zeroed by
   `prepare-owned-pages` / `prepare-extra-pages`, and idle for the life of this
   branch. They do not overlap `rtl-dma-pages` (`eleventh-start` .. +57344),
   `stream-tcb` or `stream-staging`. The pages 14..17 bindings and their five
   guard clauses are deleted; `kernel_scratch.pages` stays 14 and the receipt
   still says so.

2. **Prerequisites.** `rtl/arm-bus-master` and `rtl/link-up` name the two
   things `rtl/qualify` already did inline for bus2, so bus3 can ask for them
   without the whole ARP qualification. `arm-bus-master` writes
   MEM-SPACE|BUS-MASTER and then **reads the register back**, returning 1 only
   if the device reports both — the census says bus3 already has them, so on
   this board the write changes nothing; it exists so a submit never depends on
   firmware having left them on. `link-up` gates on PHYstatus before submitting,
   because a submit into a down PHY leaves the descriptor owned forever, which
   is indistinguishable from a ring fault.

3. **Observability.** This is the fault §4 named, and it is the one that made
   Phase 1 worthless regardless of what the wire did. Every stage now has its
   own code and all of them ride the bus2 netlog:

   | byte | meaning |
   |---|---|
   | `B8` | sentinel: the bus3 attempt starts here |
   | init `0` | ready |
   | init `6` | bar-b never decoded — no MMIO to init |
   | init `9` | the device would not report MEM-SPACE\|BUS-MASTER after the write |
   | init `7` | IDR0..5 is not a plausible unicast MAC |
   | init `2` | ChipCmd revision unreadable |
   | init `8` | PHY reports no link |
   | init `10+n` | `rings-start` refused with n (4 fifo, 1 install) |
   | send `0` | submitted **and** the engine cleared OWN — bytes left the NIC |
   | send `1` | the frame could not be built |
   | send `5` | descriptor still owned, or the wire length was rejected |
   | send `3` | a descriptor store did not read back |
   | send `4` | submitted, but OWN never cleared — the engine is not consuming |
   | send `7` | not attempted, because init refused |

   `debug-send` previously returned **0 both when the frame failed to build and
   when it was submitted** (`rtl/tx-submit` returns 0 on success, and the
   build-failure arm also returned 0), and `debug-send-status` then mapped
   success to 1 and a store fault to 0. That function is deleted rather than
   fixed: nothing called it. This is the workspace's recurring shape — a check
   that could not run returning the same value as a check that ran and found
   nothing — appearing inside the very module written to diagnose.

4. **What the pair of readings discriminates.** bus2 says whether the NIC
   transmitted (`send 0` means the engine consumed the descriptor). en8 says
   whether the Mac accepted it. `send 0` with nothing on en8 would isolate the
   frame contents (checksum, MACs) from the NIC; `send 4` would isolate the
   engine. Neither reading alone can do that, which is why both are taken.

The IPv4 header checksum stays 0x52AC, verified by hand against the header
this module writes: `0x4500 + 0x002B + 0x4011 + 0x0A0A + 0x0A02 + 0x0A0A +
0x0A01 = 0xAD53`, and `~0xAD53 = 0x52AC`. The peer MAC baked into
`store-peer-mac` (3c:18:a0:d5:82:a8) matches the Mac's live en8.

### No automated gate discriminates on this branch

The QEMU smokes are pinned to compiler `13d2f5df`, which now **refuses this
branch's kernel outright** (`:kotoba/target-rejected`, "typed values currently
require …") — measured on the unmodified source as well as the modified one.
Repointed at the `46eeedae` pin the build succeeds but the gate's expected
QEMU exit (33 / marker `MPRCD`) does not match the k16-loader-port loader —
again identically before and after this change. Both directions were measured;
neither gate is a regression signal here, and neither is claimed as one. The
physical K16 is the only instrument this branch has.

## Status of the artifacts

- Commit `e09e4f1` (Phase 1 — bus3 single-shot) is superseded by commit
  `05a3142` for the census/window strategy. `05a3142` drops the unowned-page
  bus3-dma-pages binding and adds the census.
- Build on commit `05a3142`: `AIUEOS_KOTOBA_NATIVE_BOOT_OK no-c no-crt
  no-linker imports=0`, byte-identical reproducible, `boot 810e0523…`,
  207872 bytes, k16-preflight on.
- The census boot RAN on the K16 and answered `B7 7F 7F` (above). The serve
  dir's previous file hashed `810e0523…`, which confirms independently that
  the census EFI is what booted.
- Step 1 build: `AIUEOS_KOTOBA_NATIVE_BOOT_OK no-c no-crt no-linker
  imports=0`, byte-identical reproducible across two packagings,
  `boot 730b1293d930acd7030139d206c69b5bd1b54c1000c5bd5a437ed77b16f8bb80`,
  207872 bytes, k16-preflight on, `kernel_scratch.pages = 14`. Staged to
  `/tmp/aiueos-k16-pxe/BOOTX64.EFI`; the census EFI is kept beside it as
  `BOOTX64.CENSUS-810e0523.EFI`.
- **Not yet run.** The kernel is resident, so the K16 does not reboot itself;
  the step-1 boot needs a power cycle. `socat -u UDP-RECV:9000,reuseaddr
  CREATE:/tmp/k16-bus3-en8.log` is listening on the Mac (the previous listener
  was an orphan writing to a discarded stdout). `tcpdump` is unavailable —
  BPF needs root on this machine — so a frame the Mac's `ip_input` drops would
  not be seen; that is what the bus2 send byte is for.
- Until that boot is read, bus3 debug is **not** operational. The honest
  ceiling from the previous revision stands, with one clause discharged: the
  NIC is qualified.

## Consequences

- The screen "PREFLIGHT RTL8125" standing still must not be read as a hang.
- The original Phase-1 bus3 DMA (unowned window) was invalid; the census-first
  approach replaces it.
- bus2's `21` (peer sent nothing) deserves its own investigation into the
  aliasing of staging / tx-frame / rx-buffer-a.
- Honest ceiling: bus3 debug is not operational until the census reports the
  NIC can DMA and a loader-owned window backs it.