# ADR-0180: A BOOTX64.EFI that decides which kernel may run

- Status: accepted
- Date: 2026-09-02

## Context

`os/aiueos/uefi/main.c` is 1,214 lines of C and it is the whole of K16's
`BOOTX64.EFI`. The K16 pure-native gate (`os/aiueos/scripts/k16-pure-native-gate.cljs`,
ADR-0131) refuses the profile on exactly that file:

```
REFUSED foreign-code: uefi/main.c
```

amu ADR-0299 got a Kotoba UEFI application to boot under OVMF and to name
strings, GUIDs and bytes. It closed with two things it could not do, and both
of them are what a loader is made of: it had no address it could WRITE, so
`AllocatePages`, `HandleProtocol`, `GetMemoryMap` and every other firmware call
with an out-pointer were out of reach; and nothing produced the address of a
Kotoba function, so `kernel-jump-to` stayed encoded and unexecuted.

## Decision

Take the half that needs neither, and take it first.

`main.c` mixes two kinds of work. Allocating pages, opening a volume, reading
16 MiB chunks and copying segments is MECHANISM in ADR-0015's sense. Deciding
**which bytes are allowed to become the kernel**, and **saying what happened**,
is not. Those two are this ADR's scope:

| module | lines | what it replaces in `main.c` |
|---|---|---|
| `aiueos/uefi/console.kotoba` | 103 | `console_ascii` (:186), `debug_string`, the hex writer |
| `aiueos/uefi/elf.kotoba` | 154 | the ELF admission block (:1100-1121) |

They are modules of ONE program, linked by amu's project route
(`--source-path`), not two kernel objects. A kernel object exports one symbol
and cannot call another, and a loader that cannot call a subroutine is not a
loader. Measured 2026-09-02: `--source-path` with `--artifact image` is
accepted for `x86_64-aiueos-uefi-v1`, though amu ADR-0296 records it is still
refused for the *kernel* image target.

**The admission rules are amu's, not `main.c`'s.** Where the two disagreed the
C was looser, and this follows `package-embedded-kernel`
(`src/kotoba/compiler/packaging/pe32plus.cljc:344-351`) so that the loader
admits exactly what the packager emits: two PT_LOAD segments, `filesz == memsz`,
`paddr` at or above 1 MiB and page-aligned, `memsz` at most 1 MiB,
`paddr + memsz` under 1 GiB, flags exactly `[5 6]`, the entry inside segment 0,
and no overlap.

**One rule is stricter than both.** `main.c` casts `kernel_file + elf->phoff` to
a struct pointer and reads it at whatever alignment; on x86-64 that happens to
work. A bounded Kotoba load checks alignment LAST and TRAPS, so an unaligned
`e_phoff` would kill the loader rather than refuse the file. Clause 8 turns
that trap into a verdict, and clause 9 does the same for the table's extent:
both bounds are proved before the first program-header byte is read.

**Printing a number without a buffer.** `OutputString` takes a `CHAR16 *`. With
no writable memory there is no scratch array to build digits in, so the digits
are not built: sixteen one-character literals sit in the pool, `digit-char`
selects one by value, and a number costs one firmware call per digit. When a
writable scratch region exists the buffer form can replace it without changing
a caller.

## Evidence

`os/aiueos/scripts/smoke-qemu-uefi-loader.cljs`, QEMU 10.1 with OVMF
(`edk2-x86_64-code.fd`), q35, TCG, the image on a `fat:rw:` ESP.

```
BOOTX64.EFI 9728 bytes
exit=33 debugcon="KHSCNAbcdefZ"
entry="0000000000101000" verdicts="0 2 5 24 23 41"
AIUEOS_UEFI_LOADER_OK markers=KHSCNAbcdefZ verdicts=0,2,5,24,23,41 entry=0000000000101000
```

and the firmware's own console driver rendered, on the serial line:

```
AIUEOS KOTOBA LOADER
entry  0000000000101000
verdict 0 2 5 24 23 41
```

**The fixture is a real kernel.** The 176 bytes the probe admits are the ELF
header and both program headers of a kernel amu itself produced
(`--target x86_64-aiueos-kernel-v1 --artifact image --fuel 32768`, 110,872
bytes, sha256 `e4a5c9dc3ca03e310d9b8a0924e9e2ce510fb9678335bd2fe6496b7e9c182f1e`).
A header invented to pass would only prove the rules agree with themselves.

**Five refusals, each naming its own clause.** Five copies of those same bytes
with ONE field changed each: `2` magic, `5` machine, `24` segment 0 below 1 MiB,
`23` segment 0 `filesz != memsz`, `41` segment 1 flags. A verdict that merely
went non-zero would not distinguish "the file is wrong" from "the reader is".

### The smoke discriminates

| break | debugcon | verdicts | reading |
|---|---|---|---|
| baseline | `KHSCNAbcdefZ` | `0 2 5 24 23 41` | |
| `paddr` floor 1048576 -> 1024 | `KHSCNAbc_efZ` | `0 2 5 **42** 23 41` | `d` alone goes. The segment now passes clause 4 and is caught two rules later, by the entry-in-segment check |
| ELF magic constant off by one | `KHSCN_b____Z` | `2 2 2 2 2 2` | `A` goes with `c d e f`: everything is refused at the first clause, so nothing downstream is measured any more |
| `put-hex64` divides by 8 | `KHSCNAbcdefZ` | `0 2 5 24 23 41` | every marker holds and `entry` prints `0000000000000040`. The console writer and the admission verdicts fail independently |

### A host oracle, because the CPU evidence was narrow

`os/aiueos/contracts/uefi-elf-admit-v1.edn` runs the same module against the
KIR interpreter, in the default `verify-admissions` set. It costs about five
seconds and needs no firmware.

The boot and the contract answer different questions, and the contract is the
broader one. The smoke asserts **six** verdicts because six is what fits in one
image's literal pool and one minute of TCG; the contract asserts **twenty-five**
distinct reason codes over 28 vectors — every clause the module can produce
except the four whose siblings already cover them.

Each vector is the **same 176 bytes with one field changed**, and it names the
field (`{:offset 88 :u64 32768}`) rather than carrying its own 352-character
blob, because a contract of opaque hex is 352 chances per vector to transcribe
one wrong and a reader cannot tell which byte was meant to differ.

Two vectors are boundary pairs rather than single points:
`:entry-just-inside-segment-0` (1052831) beside `:entry-outside-segment-0`
(1052832), and `:unaligned-phoff` (65, which clause 9 would also refuse) beside
`:unaligned-phoff-that-fits` (57, which only clause 8 refuses).

**The reds, each measured rather than asserted:**

| break | result |
|---|---|
| clause 8's alignment test made vacuous | `:unaligned-phoff-that-fits` **traps** — `:trap :kernel-memory-fault, :operation kernel-load-u32-4k, :check :misaligned-access, :width 4, :index 57`. This is the measurement behind "clause 8 turns a trap into a verdict"; without the isolating vector the claim was merely plausible, since clause 9 catches `phoff 65` on its own |
| `paddr` floor 1048576 -> 1024 | `:segment-0-below-1mib` expected 24, actual 42 |
| entry test `<` -> `<=` | `:entry-outside-segment-0` expected 42, actual 0 — caught only by the boundary pair |

The contract declares no memory assertions, and that is correct rather than an
omission: this object writes nothing. It answers a verdict, and the verdict is
the whole of its output.

## Consequences

- **This does not replace the K16 loader and the gate still refuses.** The K16
  profile also needs the model read, the embedded-release path and `BootNext`.
  `uefi/main.c` is unchanged and every byte of it still runs.
- **`integrity` RUNS.** amu#764 made `--fuel` reach the image, and the digest
  comparison — the loader's central security decision — now executes as
  x86-64 machine code with SHA-256's 512-byte state in `kernel-scratch-region`:

  ```
  exit=33 debugcon="Kgh"
  digest 0 4
  ```

  `g` is the 176 fixture bytes hashing to the digest `shasum -a 256` computes
  for them off this machine (`db5502b1…`); `h` is that digest with one nibble
  changed being refused, **and refused with reason 4** rather than merely
  refused, so a comparison that rejected everything would not pass either.

  **The discriminating red is one constant.** Changing the last nibble of
  `g`'s expected digest, and nothing else:

  | | debugcon | console |
  |---|---|---|
  | baseline | `Kgh` | `digest 0 4` |
  | `…1825e1` -> `…1825e3` | `Kh` | `digest 4 4` |

  Exactly that marker goes and exactly that verdict flips. Both exit 33, so it
  is the assertion failing and not the boot.

  **The fuel it actually consumes**, now that a real budget arrives — three
  64-byte blocks and a 32-byte comparison per call:

  | `--fuel` | result |
  |---|---|
  | 6,144 | traps before the first verdict — `K` |
  | 16,384 | the FIRST verdict completes, the second traps — `Kg`, `digest 0` |
  | 32,768 | both complete — `Kgh`, `digest 0 4` |

  So one `verify` over 176 bytes costs between 6,144 and 16,384, two cost
  between 16,384 and 32,768, and the budget is spent **cumulatively across the
  whole `efi-main` call** rather than replenished per callee. Consistent with
  `sha256-region.kotoba`'s measured 1,772 per block: 3 x 1,772 = 5,316 of
  compression per call, plus padding, the comparison and the console writes.

  The earlier measurement is what made the failure legible and is worth keeping:
  the image ran on a fixed 512 whatever `--fuel` said, and `--fuel 512` and
  `--fuel 1048576` produced **byte-identical artifacts**
  (`714a7509d057b654cb8ae4181284250ce82514626d50e7d25f624cc979872f29`). A knob
  that is validated and then discarded looks exactly like a knob that works.

- **The combined probe cannot be built today, and that is not this stream's
  break.** `uefi/loader-probe.kotoba` now carries the two digest verdicts, and
  it also exercises `aiueos.uefi.memory`, whose `kernel-uefi-alloc-region` is
  not in amu main (amu#766 open). Measured on main's probe **unchanged**, at
  every `--fuel` value including none: `aggregate ABI rejected:
  call-abi-not-admitted`; and on a three-line module using only that operation,
  the same. So the loader smoke is red on `aiueos` main until #766 lands.
  `uefi/integrity-probe.kotoba` is the digest half on its own so the evidence
  above does not wait on it, and it carries its own deletion condition — two
  probes for one program is the shape ADR-2608080100 argues against, and this
  one only earns its place while the other cannot be compiled.
- **Three of the seven planned modules are BLOCKED, not deferred.** `memory`
  (`AllocatePages`), `fs` (`HandleProtocol`, `OpenVolume`, `Read`) and `exit`
  (`GetMemoryMap`) can now ALLOCATE and INSPECT — BOOT-SCRATCH's
  `kernel-scratch-region` gives a firmware call somewhere to put an
  out-pointer — but cannot PLACE a kernel image, because the page address an
  `AllocatePages` out-pointer yields arrives from a load and the region
  provenance rule refuses a base it cannot trace. FIRMWARE-STORE owns that
  primitive and is writing `uefi/memory.kotoba` as its first consumer, so this
  stream did not duplicate it.
- **`kernel-jump-to` is still unexecuted.** The loader can now compute a
  kernel's entry point and print it. It cannot go there, because it cannot load
  the kernel first.
- kotoba-sema ADR-0026 was needed on the way: the three address-producing rodata
  heads were not region roots, so an image could obtain the address of bytes it
  had emitted itself and then not read them with a checked load.
- The amu that compiles this is NOT on amu main. It is amu `a8e30d5b` with the
  kotoba-sema pin advanced to `e7e13bad`.
