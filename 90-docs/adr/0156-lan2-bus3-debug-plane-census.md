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

## The step-1 boot (K16, 2026-09-06, artifact `730b1293…`)

```
AIUEOS_NATIVE_TCP_OK
AIUEOS_STREAM_B7      sentinel
AIUEOS_STREAM_7F      bus3 census
AIUEOS_STREAM_7F      bus2 census
AIUEOS_STREAM_B8      bus3 attempt
AIUEOS_STREAM_00      debug-init  = ready
AIUEOS_STREAM_00      debug-send  = submitted AND the engine cleared OWN
AIUEOS_STREAM_A1      resident loop resumes
```

and `Z` — the payload byte this module writes — arrived on the Mac's en8
UDP :9000. **LAN2 one-way debug output works end to end**: bus master armed,
ring installed, PHY up, descriptor submitted, OWN cleared, `ip_input`
accepted, listener delivered.

### The listener was the third instrument to fail silently today

The first reading of en8 said *empty*. That reading was void:
`socat -u UDP-RECV:9000 CREATE:<file>` held the datagram and wrote nothing
until the process was killed. A control datagram sent from the Mac to its own
en8 address was also not recorded, which is what exposed it —
`OPEN:<file>,creat,append` records both. **Validate the instrument with a
control before reading a null result as evidence.** This is the same shape as
the `LC_ALL` grep above and the two QEMU gates below: three different tools, in
one session, answering "nothing" when they meant "I did not run".

### A defect the boot did not expose

`build-udp-debug-frame` wrote the **frame** length (43) into the IPv4
**total-length** field, where 29 belongs (20 ip + 8 udp + 1 payload), and
baked a checksum consistent with that wrong value. The Mac delivered it
anyway: the RTL8125 pads every frame to 60 bytes, so 46 bytes of Ethernet
payload arrive and `ip_input` trims to 43 instead of rejecting. That is luck.
It stops being true as soon as the payload passes 17 bytes — i.e. the first
time this plane is asked to carry a real message. Fixed to 29 with checksum
0x52BA (`~(4500+001D+4011+0A0A+0A02+0A0A+0A01)`), and the two lengths are now
separately named so they cannot be confused again.

**A green wire receipt is not a correct frame.** The send byte reports what
the NIC did with the descriptor; it cannot report what the header says.

## What LAN2 can and cannot do today

| | state |
|---|---|
| K16 → Mac, one-way UDP log | **works** (this boot) |
| K16 → Mac, more than one message | not wired — `debug-send` is called once, before the resident loop |
| K16 → Mac, arbitrary text | not wired — the payload is a fixed byte, and there is no integer→ASCII path |
| Mac → K16 control | **not wired at all** |

Control is the larger gap and it is not a matter of enabling something.
`rings-start` installs an RX descriptor on bus3, but nothing reads it: this
module has no receive path, no ARP, no demultiplex, and the resident loop
services bus2 only. A LAN2 control plane needs an RX poll in that loop, a
frame filter, and a command decode — and every command it accepts is authority
crossing into the kernel, so it needs the admission discipline the rest of
this OS uses, not a raw byte switch.

### Prerequisite on the bus2 side

§5's aliasing is confirmed by arithmetic, and it is narrower and worse than
"could be eating received bytes": `stream-tx-frame` = `rtl-dma-pages + 8192`,
`stream-staging` = `thirteenth-start` = `rtl-dma-pages + 8192`, and the
stream's own `rx-buffer-a-offset` is 8192. All three are one page. Every
netlog byte the resident loop emits is written into RX descriptor 0's DMA
target while the NIC owns it. The loop's persistent `21` (window exhausted,
peer sent nothing) is consistent with that. (`stream-tcb` at
`rtl-dma-pages + 4096` is *not* aliased — the RX ring is at +1024, 1024 bytes
wide, and +4096..+8192 is unused.)

## The A+B boot, and two results that overturn earlier claims

Artifact `8d2d73c1…` (aliasing fix + LAN2 receive path + MAC report) booted.

```
B7 7F 7F              census
B8 00                 bus3 init = ready
DA 70 70 FC 0B B6 31  bus3 MAC = 70:70:fc:0b:b6:31
00                    bus3 send = submitted AND the engine cleared OWN
A1 ... 21             the resident loop, 280 times, then silence
```

### 1. The aliasing was real and is NOT what eats the SYN-ACK

`stream-tx-frame` and `stream-staging` now live on pages 8 and 9, disjoint
from `rx-buffer-a`. The loop still reports `21` — 279 times in this boot.
The hypothesis is refuted. The overlap was genuine and worth removing, but
the persistent "peer sent nothing" has a different cause, still unknown.

### 2. The loop is not resident. It has never been resident.

|  boot | artifact | completed cycles |
|---|---|---|
| 14220.. | `810e0523` census | 278 |
| 17312.. | `730b1293` step 1 | 277 |
| 20369.. | `8d2d73c1` A+B | 280 |

Three artifacts, three boots, the same stop. The kernel is built with
`--fuel 1048576` and the receipt says `"replenishable": false` — one budget
for the whole boot, no top-up (ADR-0034's per-call replenish is emitted by
`package-kernel-object`; this artifact comes from `package-aiueos-boot`).
At roughly 3,760 fuel per cycle that budget buys about 280 cycles.

**A loop designed never to end, under a budget that is spent once, cannot
both be true.** Plan A-2's "the stream never halts the machine" has been
false on every boot since it was written, and nothing looked, because the
symptom is a log that stops rather than an error.

`1048576` is 2^20, which was `max-native-fuel` when the line was written. The
ceiling moved to 2^53-1 on 2026-09-03 (kotoba-kir ADR 0268). **The budget was
never a decision — it was the old maximum, kept after the maximum moved.**

The next boot doubles it to 2097152 and predicts ~556 cycles. If it stops near
280 again the mechanism is something else and the number goes back. It is
raised to be *measured*, not to be a fix; a bigger finite number is still a
finite number, and a genuinely resident loop needs a replenish, not a bigger
bucket.

Raising it required editing five places that each carried the literal (two
`--fuel` flags, the sealed-context check, the receipt and the OK line), and
the policy EDN turned out to be the authority — `--fuel` alone changed
nothing and the build failed naming the old value. The build script now
generates the policy from one variable. This is the same shape the
`excessive-native-fuel-policy` fixture documents: *the fifth place the
ceiling was written down.*

### 3. Retraction: the 'Z' on en8 is not proof

The previous revision recorded 'Z' arriving on en8 as end-to-end proof. **That
claim is withdrawn.** On this boot `debug-send` again returned 0 (submitted,
OWN cleared) and **nothing reached en8** — with a listener validated by a
control datagram immediately before and after. `netstat -I en8` shows 27
packets received in the machine's entire uptime.

The one reading that showed 'Z' came from the listener whose sink was broken,
recovered when that process was killed, so it cannot be timestamped or
attributed to a boot; another session was experimenting on this same wire that
afternoon. One unrepeatable reading from a defective instrument is not
evidence, and it should not have been written down as if it were.

What is established: bus3 census 7F, `debug-init` ready, and the TX engine
clearing OWN. What is NOT established: that any byte left the NIC, or that
bus3 is cabled to en8 at all. `wait-tx-complete` reads the OWN bit and not the
descriptor's error bits, so it cannot tell those apart — and without `tcpdump`
(BPF needs root here) neither can the Mac.

The cheapest test of the wiring costs nothing extra: the Mac's ARP requests
for 10.10.10.2 are broadcasts, so if bus3 is on that wire `debug-poll` sees
them and answers `1` ("not ours"), which the loop logs as `D9 01`. That
requires a live kernel, which is what the fuel change is for.

## The fuel mechanism is confirmed

Budget doubled to 2097152. Predicted ~556 cycles. **Measured 582.**

| budget | cycles | fuel/cycle |
|---|---|---|
| 1048576 | 278, 277, 280 | ~3,760 |
| 2097152 | 582 | ~3,600 |

The count moved with the budget. The loop stops because it runs out of fuel,
and for no other reason. The prediction was made before the reading and the
reading was not adjusted to fit it.

The doubled budget still ran out before the wiring test could be sent, so the
`D9` question is still unanswered: **not measured**, not negative.

The budget is now 1073741824 (2^30) — 512x the original, roughly 143,000
cycles. That number is chosen to outlast a working session, **not** because it
is the maximum: taking the ceiling as the budget is exactly the mistake that
produced 1048576 in the first place. It is a bucket, not a fix. A loop that is
genuinely resident needs a replenish in `package-aiueos-boot` (the receipt
still says `"replenishable": false`), and that is a compiler change, not a
larger literal.

`netstat -I en8` moved 27 -> 34 across the 582-cycle boot. Seven frames is not
nothing and not an explanation; the interface counter cannot say what they
were, and without BPF neither can anything else on this machine. It is
recorded because it is the only en8 evidence that moved, not because it
supports a conclusion.

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
- Step 1 RAN (owner power-cycled the K16): `B7 7F 7F B8 00 00` on bus2 and
  `Z` on en8. The one-way LAN2 debug output is operational.
- Step 2 build (IPv4 total-length fix): `AIUEOS_KOTOBA_NATIVE_BOOT_OK no-c
  no-crt no-linker imports=0`, byte-identical reproducible,
  `boot c078201e7b1c3ee2e45a3de2a31e38f79054aa7e1a47e3b4e6c66fb114a8abf5`,
  207872 bytes, k16-preflight on. Staged; `BOOTX64.STEP1-730b1293.EFI` and
  `BOOTX64.CENSUS-810e0523.EFI` are kept beside it. **Not yet run** — it needs
  a power cycle, and nothing observable is expected to change, because the
  defect it fixes was masked by Ethernet padding at this payload size.
- The listener is now `socat -u UDP-RECV:9000,reuseaddr
  OPEN:/tmp/k16-bus3-en8.log,creat,append`, validated with a control datagram
  before and after each reading. `tcpdump` remains unavailable (BPF needs root),
  so a frame the Mac drops in `ip_input` is still invisible; the bus2 send byte
  is what separates that case from a NIC that never transmitted.

## Consequences

- The screen "PREFLIGHT RTL8125" standing still must not be read as a hang.
- The original Phase-1 bus3 DMA (unowned window) was invalid; the census-first
  approach replaces it.
- bus2's `21` (peer sent nothing) deserves its own investigation into the
  aliasing of staging / tx-frame / rx-buffer-a.
- Honest ceiling: bus3 debug is not operational until the census reports the
  NIC can DMA and a loader-owned window backs it.