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

## Status of the artifacts

- Commit `e09e4f1` (Phase 1 — bus3 single-shot) is superseded by commit
  `05a3142` for the census/window strategy. `05a3142` drops the unowned-page
  bus3-dma-pages binding and adds the census.
- Build on commit `05a3142`: `AIUEOS_KOTOBA_NATIVE_BOOT_OK no-c no-crt
  no-linker imports=0`, byte-identical reproducible, `boot 810e0523…`,
  207872 bytes, k16-preflight on. Not staged to the PXE serve dir by Claude.
- The census boot has NOT yet been run on the K16. PXE serves the census EFI
  `810e0523…` (deployed by itonami after this ADR was drafted). Reading the
  census answer is the next decisive step.

## Consequences

- The screen "PREFLIGHT RTL8125" standing still must not be read as a hang.
- The original Phase-1 bus3 DMA (unowned window) was invalid; the census-first
  approach replaces it.
- bus2's `21` (peer sent nothing) deserves its own investigation into the
  aliasing of staging / tx-frame / rx-buffer-a.
- Honest ceiling: bus3 debug is not operational until the census reports the
  NIC can DMA and a loader-owned window backs it.