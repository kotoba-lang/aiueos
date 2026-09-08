# ADR-0202 — The board computed the answer

Status: accepted
Date: 2026-09-08
Supersedes in part: the `:transport` clause of
`os/aiueos/contracts/micro-inference-qualification-v1.edn`

## What happened

The physical K16 ran an `aiueos-micro-infer` job and returned an answer it
computed. Four times, for four prompts the Mac chose, with four different job
ids and four different boot nonces:

| prompt | expected | returned | TSC cycles |
|---|---|---|---|
| `murakum` | `("o", 2, 5)` | `token=6f score=02 total=05` | 1152 |
| `aiueos` | `(" ", 5, 7)` | `token=20 score=05 total=07` | 1344 |
| `kotoba` | `("i", 2, 8)` | `token=69 score=02 total=08` | 800 |
| `wave` | `(" ", 2, 9)` | `token=20 score=02 total=09` | 864 |

`murakum` is the `:known-answer` of the qualification contract. The
expectations come from `MURAKUMO_MICRO_INFER_ROWS` in `k16-pxe-server.py` and
the lines were parsed by that file's own `JOB_RESULT` regex, both read out of
it rather than copied, so there is no second table to drift.

Image `6e6df6bd`, board MAC `70:70:fc:0b:b6:31` (bus3).

## Why this is not a relay echo

The Mac sends the prompt and knows the answer, so an echo would look the same
from one end. Three things separate them.

**Different prompts give different answers.** A constant would have failed
three of the four.

**The board refuses.** 173 datagrams carrying prompts the board must not
answer — `z`, whose transition row is all zeros, and `aQ`, which leaves the
vocabulary — produced **zero** result lines. The bus2 netlog names which
refusal each was: `D9 73` three times (empty transition row, the C reference's
`!total` refusal) and `D9 72` three times (outside `" a-z"`). A board that
answered those with something plausible would have been indistinguishable from
a working one at the Mac.

**A second, independent instrument agrees.** The answers crossed LAN2; the
receipts are on the bus2 netlog, a different NIC and a different socket. It
counted `D8 00` on 21 consecutive boots (the announcement left the NIC) and
`D9 70` exactly four times — one per verified answer.

The cycle counts are the one number the Mac cannot predict, which is why they
are in the line. They are a serialized TSC delta, not a wall time, until the
TSC frequency is measured — as the contract already said.

## The transport clause was never true

The contract named bus2, `10.77.0.1:7777 <-> 10.77.0.10:7779`, from the day it
was written. **Bus2 has no UDP receive path.** The board can only transmit
there; the TCP stack owns that ring and re-initialises it every cycle. No job
could ever have arrived on the wire the contract specified, and nothing said
so, because nothing had tried.

The relay now rides LAN2/bus3, whose ring is installed once at `debug-init`
and polled non-blockingly every cycle. The old addresses are kept in the
contract under `:retired` with the reason, rather than deleted.

## The relay was rejecting the board's own announcement

`MURAKUMO_EXPECTED_MAC` was a single value, bus2's `…b6:32`. The announcement
leaves by the NIC it is sent from, which is bus3, `…b6:31`. The first hello the
board ever sent would have been dropped as `unexpected-mac` while every log on
both ends read healthy — the same shape as the PXE server's own MAC bug two
days ago, and fixed the same way: a list, because the guarantee being protected
is *one board under qualification*, not *one cable*. The selftest now asserts
both NICs are accepted and a foreign MAC is not, and was watched failing in
both directions.

## How the board holds a 27x27 matrix

It does not, as data. The native word-typed subset has no arrays and `def`
takes literals only. Every count in the frozen matrix is at most 5, so a row
packs losslessly into two i64 literals of 4-bit fields — 15 cells in `lo`
(bits 0..59, clear of the sign bit) and 12 in `hi`. `native/micro_infer.kotoba`
is generated from `kernel/micro_infer.c`, which stays the source of truth for
the model.

**The argmax is deliberately not precomputed.** Precomputing it would make the
board a lookup table, and the claim being qualified is that the K16 performed
the reduction.

The first version of the generator packed with `Math.pow` and lost the top
cells silently past 2^53. Its own round-trip assertion caught it. That
assertion stays.

## How the lines are written

Every numeric field the relay reads goes through `int()`, which accepts
leading zeros, so each is written at a constant width and the board needs no
variable-length decimal formatter. The one exception is `id`, which the relay
compares **as a string** — so the result line is a head and a tail with the
request's own id bytes copied between them, and the board never reformats it.

The templates live in `native/relay_text.kotoba`, packed eight ASCII bytes to
an i64 (ASCII never sets the sign bit). A per-byte `if` chain would have cost
8,281 comparisons for the 91-byte announcement against a 2^20 per-boot fuel
budget shared with a TCP stack; eight-byte words cost 144. The generator checks
every template it emits against the relay's own regexes, so a protocol change
on the Mac breaks the build here instead of making the board transmit into
silence.

A variable-length datagram cannot bake its IPv4 header checksum, which the
one-byte plane could. Getting that wrong is invisible from the board — the Mac
drops it in `ip_input` and both ends read as healthy. That cost this plane a
day on 2026-09-08 and is why `relay_line/header-checksum` exists.

## What is NOT shown

No murakumo credential is configured on this machine. Nothing has been
enqueued at, claimed from, or posted to `api.murakumo.cloud`. The
`:requires` clauses `:claimed-job-id`, `:result-persisted-before-ready` and
`:fresh-liveness-renewal` are **unproven, not met**, and the contract now says
so in `:measured :unproven`.

Turning the last hop on needs two values this session must not go looking for:
`AIUEOS_MURAKUMO_NODE_DID` (`did:key:…`) and `AIUEOS_MURAKUMO_SERVICE_TOKEN`,
supplied through a credential tool or the environment of the PXE server.

Liveness ping/pong (`AIUEOS_NODE_PING_V1` / `_PONG_V1`, the contract's
`:idle-proof :physical-ping-pong`) has its template on the board but no
handler yet. That is the next board slice.

## Gates

`verify-wire-bytes` 0, `verify-store-tautology` 0, both generators `--check`
clean, `check-micro-infer-table` FINDINGS 0, PXE selftest OK, QEMU smoke exit
33 marker `MPRCD`.

`check-micro-infer-table` carries its own control every run: it mutates row 13
— the contract's known-answer row — in a copy and requires that copy to be
refused by `division-by-zero` specifically. The first version of that control
reported "this gate cannot go red" whenever the base table was already red,
because two drifted rows make `(quot 1 (- 1 2))` a legal `-1`. It now skips and
says which. A control that means nothing must not print like one that passed.
