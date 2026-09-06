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

## Root cause of the persistent `21`: the SYN was overwritten before it was sent

`stream-log` built its UDP/7777 receipt **in `tx-frame`** — it zeroes 62 bytes
of that page, writes a log datagram, and submits it. `stream-connect` builds
the SYN into `tx-frame`, then calls `stream-log` **seven times on the same
page**, and only then submits `tx-frame` as the SYN. What went on the wire was
the last log frame, truncated to `syn-len`. Every cycle. About 363,000 times
in the last boot alone.

### The Mac's own counters are unambiguous

```
netstat -s -p tcp:  0 connection request
                    0 discarded for bad checksum
                    0 discarded because packet too short
netstat -s -p ip:  10 bad header checksums   (whole machine, whole uptime)
lsof -nP -iTCP:8443: the bridge IS listening on 10.77.0.1:8443
bridge.log:         no K16_STREAM_CONNECTED since it started, 2026-09-05
```

Not "saw the SYN and rejected it" — **never received one**. Meanwhile the
netlog UDP from the same NIC, the same submit path and the same MAC helpers
arrives fine, which is what made the frame contents the only variable left.

### This was found once before, and the fix was reverted for a good reason

Commit `18bf9bd` (2026-09-06 00:44) diagnosed exactly this and pointed
`stream-log` at `staging`. Commit `485ed76` reverted it with no message.
The revert was right: **at that time `staging` and `tx-frame` were the same
page** (both `rtl-dma-pages + 8192`), so the fix was a no-op and would have
looked like it did nothing.

So the aliasing fix earlier in this document was not refuted after all — it
was refuted *as an explanation of `21`*, which it is not, and it is *the
precondition* for this fix to be able to work at all. Two separate defects
sharing one page, where removing either alone changes nothing.

### Why no ordering fixes it

`A2` is logged **after `tx-submit-stream` and before `wait-tx-complete-stream`**
— i.e. while the NIC owns the descriptor and is reading the frame. Moving the
seven diagnostics after the submit would still corrupt the SYN mid-DMA. Only a
separate buffer works.

### The fix: one scratch region, three fixed offsets

`stream-scratch` is 12 KiB at `second-start` (`page-table-root`, `pdpt`, `pd` —
contiguous, zeroed by `prepare-owned-pages`, and never installed in this branch
because `prepare-page-tables` is in the CR3 arm). Inside it: **log at +0, tx at
+4096, staging at +8192.**

`stream-connect`, `receive-syn-ack`, `receive-established` and
`stream-resident` now take `scratch` and derive all three, plus `tcb` from
`win + 4096`. Three separately-computed addresses became one region with fixed
offsets, so a future edit cannot quietly point two names at one page without
changing an offset.

That also answers the reason `18bf9bd` gave for not doing this ("a dedicated
netlog page would need a 6th arity and is rejected by the compiler"): `tcb` is
derivable from `win`, which frees the slot. The arity ceiling was real; the
conclusion drawn from it was not.

The seven diagnostics now *read* `tx-frame` and *write* to `log-frame`, so for
the first time they report the actual SYN's ethertype, IHL, IP checksum fold
and TCP checksum rather than the previous log frame's.

**Predicted, before the boot:** `netstat -s -p tcp` "connection request"
becomes non-zero and `bridge.log` gains `K16_STREAM_CONNECTED`. If neither
moves, this diagnosis is wrong and the frame contents are the next suspect —
the diagnostics will then be reading the real SYN and can say so.

## A silent kernel that was a deploy bug, not a code bug

The banner earned its place on its first boot. The panel read
`AIUEOS K16 BUILD 93ead90d392de1f5 42838f3-dirty` — **one artifact behind what
was being served.** Without it the next reading (`0 connection request`) would
have been recorded as "the SYN fix did not work", when the fix had not been on
the machine at all.

That boot then produced **no netlog at all** — not one line — while the
previous boot of a **byte-identical kernel** (`93ead90d`, only the loader
differed) had produced 363,313 cycles. Both artifacts boot correctly under
QEMU, printing ENTER and BUILD and reaching the kernel's `M` marker on the
debug port. And the panel showed ENTER, BUILD and RTL8125, so the loader had
completed everything and called the kernel.

The cause is the deploy, not the code. The PXE server does
`content = selected.read_bytes()` at request time, and the artifact was being
installed with `cp` **onto the live path**, which truncates and rewrites in
place. Both builds are **exactly 211968 bytes** — the two note strings happen
to be the same length, so the layouts match — so a transfer overlapping a
deploy reads a full-length file whose head and tail come from different
builds. Nothing downstream can see that: the size is right, the loader runs,
the banner (near the end of the file) reads from one build while the kernel
(near the start) comes from another.

The deduction, rather than the guess: the panel proves the tail was
`4d75d6bb`; `4d75d6bb`'s kernel is byte-identical to one that had just run
363,313 cycles, so a *pure* `4d75d6bb` would have produced netlog; it produced
none; therefore what ran was not a pure `4d75d6bb`.

`scripts/k16-pxe-deploy.sh` now writes a temporary file beside the target and
`mv`s it. `rename(2)` on one filesystem is atomic: a reader gets the old file
or the new one, never a seam.

**This is the same shape as everything else in this document** — an unmeasured
assumption ("copying a file is atomic") producing a result that looks exactly
like a failure of the thing under test. It is the seventh instance today.

## Why no automated gate discriminates: the CR3 path triple-faults, and always has

Earlier this document recorded that the QEMU smokes do not discriminate and
left it there. They do not discriminate for a *reason*, and the reason is a
real defect:

```
v=0e e=0003  page fault, PRESENT + WRITE   CR2=0x110100  IP=0x11a424
             -> v=0d (#GP) -> v=08 (double) -> Triple fault -> reset
```

The receipt's own `protection` block says `kernel_text 0x101000-0x11bfff` is
**RX** and `kernel_state 0x11c000-0x12efff` is RW-NX. **CR2 `0x110100` is
inside the kernel's own read-only text.** So once `enable-page-protection`
sets CR0.WP, a write into text that had been silently tolerated becomes a
fault — and the #PF handler is installed *later* in the same branch, so there
is nothing to catch it. Double fault, triple fault, reset. With `-no-reboot`
QEMU then exits 0, which is why the gate's "expected 33" never matches and why
the failure has looked like a stale expectation rather than a crash.

Measured across four artifacts spanning the whole of this work — including
`810e0523`, the census build this ADR was originally written about — the
debug port emits exactly `M` (the map marker) and nothing else. Not one of
them reaches `P`, `R`, `C` or `D`. **The CR3 branch has never completed under
QEMU during any of this work.**

The K16 does not take that branch (`bar-a > 0`), which is why the physical
machine has been the only instrument. But it also means the one automated
signal this kernel has was dead the whole time, and "the gate's expected value
is stale" — written in this document a few hours ago — was the wrong reading
of the same evidence. The expectation is fine; the guest crashes.

Next step for that path is to name what writes `0x110100`: the fault IP is
`0x11a424`, both addresses are stable across runs, and the sequence is
deterministic.

## Two silent boots, and a retraction of the explanation for each

Boot of `4d75d6bb` (banner loader, kernel `93ead90d`): silent.
Boot of `f21cdc14` (banner loader, kernel `3b949b08`, SYN fix): silent.
Boot of `cb41be47` (pre-banner loader, kernel `93ead90d`) before them: 363,313
cycles.

Silent means silent: `netstat -I en15` shows **zero packets received in 30
seconds**, twice. The K16 transmits nothing. The panel shows ENTER, BUILD and
RTL8125 — so the loader completed segment copy, `AllocateAnyPages`,
`GetMemoryMap` and the PCI check, and called the kernel. `STATUS` never
appears, so `main` has not returned.

**Retraction 1 — the torn image.** The first silence was attributed to a
non-atomic deploy: `cp` onto the live path while the server was reading it,
with both artifacts exactly 211968 bytes so the seam would be invisible. The
second boot was deployed with `rename(2)` and was silent too. The atomic
deploy is a correct fix and stays; **it was not the explanation.**

**Retraction 2 — the banner loader.** With `93ead90d` failing under the new
loader and succeeding under the old one, the loader looked like the common
factor. It is not: the two `.text` sections were compared instruction by
instruction. The insertion is 26 bytes in the intended place, **all fourteen
subsequent rip displacements are corrected by exactly 26**, the two absolute
addresses the preflight hands the kernel (`context 0x11c000`,
`returnable-entry 0x11bac1`) are unchanged, and the BUILD string's reference
resolves to the right `.data` offset — verified against where the bytes
actually sit in the file. PE section layout is consistent (`.text`
0x21e -> 0x238, `.data` 0x33252 -> 0x332c4, same vaddrs, same `SizeOfImage`,
same file size). **The loader is byte-correct.**

So "the banner is the common factor" was an inference from three data points,
and the code says it is wrong. What else changed between the last working boot
and the first silent one: the machine had just run **363,313 cycles**, roughly
four million frames through bus2 — three orders of magnitude more than any
boot before it.

**Next reboot re-runs the control**, `cb41be47` itself. Two nulls have now been
read off a rig whose baseline was last confirmed before that long run, and the
whole of this document is a record of what happens when a null is read from an
instrument nobody re-checked. If the control is silent, the machine changed and
every artifact reading since is void. If it runs, the variable really is in the
newer artifacts and the bisect continues with the pre-banner build of the SYN
fix (`04aa5988`), which is built and waiting.

## The three silent boots were a dead logger. The machine never stopped.

`netstat -I en15` decided it, and the deciding number was the packet SIZE:

```
delta pkts  = 3,728,295
delta bytes = 230,535,288
average     = 61.8 bytes/packet     <- the netlog frame is 62 bytes
3,728,295 / 12 frames per cycle = 310,691 cycles
```

310,691 against the 310,553 that the last *logged* boot ran. The control
transmitted a full fuel-exhaustion run and **the Mac received every frame**.
Walking the counter backwards, the interval covering the `f21cdc14` boot shows
`+3,728,288` — the same run. Those boots were never silent.

`lsof -nP -iUDP:7777` returns nothing: the PXE server's netlog thread is gone.
The log holds the epitaph at line 4026326 —
`Exception in thread Thread-3 (netlog_server)` — and nothing else, because the
traceback went to stderr and nothing captured it (`grep -c 'File "'` over the
whole 270 MB file: **0**). The process kept serving DHCP and TFTP throughout,
so from outside it looked healthy.

**I caused this.** The receive loop had no exception handling, and raising the
fuel budget to 2^30 raised the netlog from ~3,400 lines per boot to
**3.7 million**. The instrument was fine at the budget it was designed under
and died at the one I chose without asking what it would cost.

Three conclusions rest on those readings and all three are withdrawn:

| conclusion | status |
|---|---|
| the deploy tore the image | withdrawn (retracted once already; still not it) |
| the banner loader broke the boot | withdrawn (and the loader was separately proven byte-correct) |
| the machine degraded: 310k -> 52k -> 0 -> 0 | **withdrawn** — the decline is the logger dying mid-run, not the machine |
| the aliasing is not what eats the SYN-ACK | **stands** — measured at boot 20384, before the logger died |
| fuel determines the cycle count | **stands** — 2^20 gave 277-280 five times, 2^21 gave 582 |

### What replaced the instrument

The PXE server binds UDP 67 and 69 and **cannot be restarted by this user** —
binding any port below 1024 is EPERM here, so killing it would end PXE booting
with no way back. It was left alone. The netlog port is 7777, unprivileged, so
the receiver was rebuilt beside it: `tools/k16-netlog-standalone.py`, which
never dies on a datagram, keeps the exception text, and prints a liveness line
every 10,000 receipts so that a dead receiver and a quiet wire can no longer
produce the same empty file. It was validated with a control datagram before
anything was read from it. The same guard is applied to the server's own loop
for whenever it is next restarted.

### And the budget goes back down

2^30 was chosen to outlast a session. What it actually bought was 3.7 million
log lines per boot, which is what killed the receiver. The SYN fix needs a few
hundred cycles to prove itself, so the next artifact is built at **1048576** —
not because that is a ceiling, but because it is what the instrument can read.
A budget is a choice about the whole rig, not just about the kernel.

## The bus2 stream: from `21` on every cycle to a completed handshake on every cycle

With the netlog instrument rebuilt and the board resetting itself at the end of
each run (0xCF9), iterations cost about two minutes and no hand on a button.
What that bought, in order, each step measured on the K16 before the next:

| # | defect | how it showed | fix | wire after |
|---|---|---|---|---|
| 1 | `stream-log` built its receipt in `tx-frame`, overwriting the SYN between build and submit | Mac never saw a SYN; netlog fine | separate log page in one `scratch` region (log +0, tx +4096, staging +8192) | SYN reaches the Mac (`SYN_RCVD`) |
| 2 | IPv4 checksum summed over the previous cycle's checksum field | fold alternated `FF`/`D4`, field `2B`/`00`, 139 vs 138 | zero the field first | `FF 2B` every cycle |
| 3 | SYN carried seq 0; `receive-syn-ack` wanted ack ISS+1 | offline TCP checksum matched seq 0, not the ISS | `tcb-init` seeds snd-nxt = ISS | TCP checksum byte `8B`→`A8` (= recomputed for ISS) |
| 4 | stream RX ring wrote opts1 @+0 / addr @+8; NIC (RxDescV3) reads addr @+16, opts1 @+28 | poll read a zero OWN forever; `A4` every cycle | rings-start's layout, zeroed descriptors, EOR only on the last | — (masked by 5) |
| 5 | RDSAR written into a running receiver is not latched | still `A4` after 4 | StopReq → FIFO drain → ChipCmd 0 → RDSAR → RE\|TE | frames arrive: `A8` (checksum path reached) |
| 6 | TCP checksum verified over 20 bytes; a macOS SYN-ACK carries 20–24 bytes of options | MAC/IP/ports/ack all passed, every frame fell to 168/96 | TCP length = IPv4 total − 20 | **`K16_STREAM_CONNECTED from 10.77.0.10`** |
| 7 | the retry was a `let` binding: ran before the frame was judged | `A4` then eight `A8` | a call, made only in the not-ours branches, advancing the ring head | clean single verdicts |
| 8 | ESTABLISHED poll 250000 × 8 ≈ 2M calls; a boot has 2^20 fuel, no replenish | `A9 E8 E7` then silence until reset | 8000 per tick, and a note that fuel is the budget | window completes: `E8 … E0 21` |
| 9 | `rcv-wnd` seeded 0 | every data segment out-of-window | 8192 | — |
| 10 | fixed source port 49155 every cycle | cycle N+1's SYN hit the peer's FIN-WAIT-1 for cycle N: `A6`/94 on every cycle after the first | port and ISS move per cycle | **every cycle completes the handshake** (ports 49636, 49637, …; no lingering 8443 states) |
| 11 | the ESTABLISHED window had no 4-tuple check; DHCP/ARP broadcasts were judged as TCP | `F5` on the first frame of every window | ethertype / IHL / proto / addresses / ports, stray = one tick | `F6` never fires — the remaining `F5` frame is ours |

Also in the same run of work: the segment is now parsed rather than assumed
(data offset from byte 46, payload = IPv4 total − 20 − doff, seq from the
header; it used to be `bytes − 54` starting at rcv-nxt, which counted options,
pad and FCS as data and copied from byte 0 of the frame); delivered data is
acknowledged with a real segment (`build-ack` wrote no Ethernet header and no
checksums into the staging page and was never submitted); a peer FIN is
acknowledged and answered with our own FIN\|ACK (result 100 = `0x70` on the
wire). These are landed and not yet exercised: every window so far has ended
on one out-of-window frame followed by an empty window.

### Open at this revision

The first frame after every handshake is ours (passes the 4-tuple filter) and
is judged out-of-window by `seq-core/acceptable?`. A build is on the board
that emits the verdict's inputs after `F5`: flags, payload length, seq low
byte, rcv-nxt low byte. Until it is read, this is **not measured**.

### Instruments, and the two that were dead

- `netstat -s -p tcp` on this Mac reports **zero for everything**, including a
  connection made from this machine that the bridge logged at that moment.
  Every "0 connection request" recorded above came from it. Dead; do not use.
- The bridge (`k16-bridge.py`, 10.77.0.1:8443) was validated by connecting to
  it directly (`K16_STREAM_CONNECTED from 10.77.0.1:…`). It is the handshake
  oracle. `netstat -an -p tcp | grep 8443` shows the peer's state
  (`SYN_RCVD` / `ESTABLISHED` / `FIN_WAIT_1`) and is live.
- The netlog is `tools/k16-netlog-standalone.py` on 10.77.0.1:7777; the PXE
  server's own receiver thread is dead and the process cannot be restarted by
  this user (UDP 67/69 need root). Validate with a control datagram before
  reading a null.

### Wire bytes (one table — collisions cost a whole iteration today)

`A1` SYN built · `A2` submitted · `A3` TX complete · `A4` SYN-ACK window
expired · `A6` ports/ack mismatch → 94 · `A7` IPv4 checksum → 93 · `A8` TCP
checksum → 96 · `A9` handshake complete · `AE` retransmit budget gone · `AF`
build failed · `B0` submit failed · `B7` census sentinel · `B8` bus3 sentinel ·
`D9` bus3 event · `DA` bus3 MAC sentinel · `DE` run end → reset · `E0–E8`
ESTABLISHED window entry with ticks remaining · `F1` empty poll · `F2` bad
descriptor · `F3` duplicate · `F4` queue full · `F5` out-of-window (+4 bytes
of inputs) · `F6` not ours · `F7` admitted · `F8` staged · `F9` delivered ·
`0x0C+result` cycle result (`21` window expired, `6A` 94, `6F` 99, `70` 100).
The first instrumentation used `B0`, `B6`, `B7`, `B8` and was unreadable by
construction.

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