# ADR-0212 — the C-free conversion order, and the one rule that blocks most of it

- Status: accepted
- Date: 2026-09-10
- Related: ADR-0013 (C-free hard-flip release rule and the 6-phase gap ledger),
  ADR-0110, ADR-0112 (superseded as C-free evidence)

## Context

ADR-0013 states the rule — the production bare-metal profile does not compile,
link, load or execute C — and carries a six-phase gap ledger of what remains.
What it does not carry is an ORDER: which file to convert first, and what
actually stops each one. Measured 2026-09-10.

## The surface

`os/aiueos` is 27,623 lines of `.c`, 1,188 of `.h`, 979 of `.S` against 23,166
of `.kotoba`. But the split is not even: **`os/aiueos/kernel` is 21,444 lines of
C and assembly and zero lines of `.kotoba`.** The `.kotoba` lives in
`os/aiueos/native` (7,157 lines) as objects the kernel links.

## Ordering: use the symbol graph, not the include graph

An `#include` graph gives four layers and is WRONG. `ioapic.c` includes nothing
local and declares `extern uint32_t aiueos_acpi_ioapic_address(void)` inline, so
an include-based sort puts it in layer 0 when it depends on `acpi.c`. Sorting by
which `aiueos_*` symbols each file defines and references gives eight layers:

| layer | files | lines |
|---|---:|---:|
| 0 (references nothing another `.c` defines) | 15 | 3,622 |
| 1 | 3 | 1,567 |
| 2 | 6 | 2,372 |
| 3 | 2 | 2,493 |
| 4 | 2 | 707 |
| 5 (`main.c`, 20 deps) | 1 | 3,699 |
| 6 (`pci.c`, 16 deps) | 1 | 5,482 |
| 7 (`loader.c`) | 1 | 86 |

Reproduce with the symbol scan in this ADR's commit message; it is the ordering
the conversion should follow, prerequisites first.

## The rule that blocks most of layer 0

Within layer 0, splitting by whether a file touches hardware
(`__asm__`/`volatile`) leaves 1,083 lines of pure logic — protocol formatting,
qualification, ACPI validation, AES-GCM. That is exactly the "judgment" the
workspace rule says belongs in `.kotoba` rather than C. **It does not follow
that those 1,083 lines convert.**

Measured, with a two-line probe:

    (let [ptr (slice-load-u64 base length 3)]
      (slice-load-u8 ptr 32 0))

    exit 65  :kotoba.error/kernel-region-provenance
             "kernel memory base must name a region, not compute one"

**A pointer loaded from memory cannot be a region root.** The frontend admits a
literal, `kernel-boot-info`, a parameter, or a `kernel-subregion` narrowing of
one — and a struct field holding a `const char *` is none of those. So any
component whose contract is "here is a pointer to a struct, validate what it
points at" is blocked at the language surface, not at the effort level:

| layer-0 pure-logic file | lines | `->` uses | status |
|---|---:|---:|---|
| `relay_protocol.c` | 63 | 0 | **convertible** |
| `tls_aes_gcm.c` | 180 | 0 | **convertible** |
| `inference_status.c` | 53 | 27 | blocked |
| `model_handoff.c` | 71 | 17 | blocked |
| `device_worker_protocol.c` | 77 | 11 | blocked |
| `qualification.c` | 154 | 12 | blocked |
| `job_protocol.c` | 181 | 8 | blocked |
| `acpi.c` | 304 | 21 | blocked |

**243 of 1,083 lines convert today. 840 are blocked on one rule.**

Per the whole-component migration rule, a blocked component is recorded blocked;
it is NOT reduced to the subset of its checks that avoid pointers in order to
make a gate green. `aiueos_inference_status_valid` validates three `const char *`
fields; a version of it that skips them is a different function that happens to
compile.

Two honest exits, neither taken here: re-express the public surface so the
strings arrive as parameters (a versioned API decision, which whole-component
migration explicitly permits), or add a provenance-preserving load to the
language surface plan. `kernel-load-ptr` exists in the operation table and is
the obvious place to look first.

## The binding constraint is arity 5, not the provenance rule

Measured 2026-09-10, twice and independently:

    (defn f [p0 p1 p2 p3 p4]    …)   exit 0
    (defn f [p0 p1 p2 p3 p4 p5] …)   exit 65
        :kotoba.error/max-parameters
        "function parameters exceed ABI-supported arity"

**Five parameters. Six is refused.** This reshapes the "C marshals, Kotoba
judges" convention that the rest of this ADR leans on: if every buffer arrives
as a `(base, length)` PAIR, a function gets two and a half buffers before it
runs out of arity. `aiueos_inference_status_valid` reads seventeen struct
fields including three `const char *`; flattened to parameters that is roughly
twenty, so it cannot be expressed that way at all.

The way through is not more parameters but fewer: **C flattens the struct into
one contiguous region and Kotoba reads it back by offset**, which is two
parameters (`base`, `length`) regardless of how many fields there are. That is
a different and larger marshalling job than "pass the spans", and every
conversion of a wide struct needs it.

## The conversion coefficient, measured once

`kernel/job_protocol.c` was converted whole — all five public functions — and
compiles for `x86_64-aiueos-kernel-v1`. The artifact is
`native/job_protocol.kotoba`, landed unwired as evidence.

| | |
|---|---|
| C, code lines (excl. comment/blank) | 163 |
| Kotoba, code lines | 340 |
| **expansion** | **2.09x** |
| public functions | 5 C → 6 Kotoba |

The function count grows because `aiueos_job_request_parse` fills an out-struct
and there is no out-parameter across this boundary, so it splits into
`job-request-verdict` and `job-request-prompt`. Expect that split wherever the
C returns through a pointer.

Projected onto the 13,481 kernel C lines that sit in functions touching no
hardware, 2.09x is **roughly 28,000 lines of Kotoba to write**. Treat that as
an order of magnitude and not a plan: **n = 1**. Two sibling measurements on
`inference_status.c` and `qualification.c` were started and did not finish, so
there is no spread, and protocol parsing is plausibly the friendliest shape in
the tree.

## What is confirmed to work

Probed on `x86_64-aiueos-kernel-v1`, all compiling:

- string literals, and `string-code-point-at` over them
- `kernel-store-u8` / `-u16` / `-u32` into a parameter-rooted region
- `kernel-load-u8` / `-u16` / `-u32` / `-ptr`

So `relay_protocol.c` — which writes literal banners into a caller's buffer and
reads a 6-byte MAC parameter — has no missing primitive. It is the first
conversion, and this ADR does not claim it is done: **zero files are converted
as of this record.** What is done is knowing the order and the blocker.

## Order of work

1. `relay_protocol.c` (63) — proven convertible, no missing primitive
2. `tls_aes_gcm.c` (180) — proven convertible
3. decide the pointer question (API change or language surface), because 840
   lines of layer 0 and most of layers 1-4 wait behind it
4. the rest of layer 0, then layers 1-4 in order
5. `main.c`, `pci.c`, `loader.c` last — they depend on everything

## What this does not say

It does not say the refactor is small. 20,028 lines of kernel C, of which the
two provably-convertible files are 243. It does not say `pci.c` (5,482 lines,
16 deps) is reachable on this ordering without the hardware-mechanism question
being answered separately — the workspace rule currently permits C for
registers/MMIO/GDT/paging, and ADR-0013's hard flip does not. That tension is
real and is an owner decision, not a measurement.
