# ADR-0136 — one copy of the AEAD, and two objects that could finally be built

Status: accepted
Date: 2026-09-02
Supersedes nothing. Extends ADR-0135 (a kernel object imports its first module),
ADR-0132 / ADR-0133 (the TLS objects), ADR-0129 / ADR-0130 (object producer
provenance), ADR-0021 (the duplication this repository already pays for).

## Context

amu#742 removed the refusal that made a `(:require ...)` in a kernel source
unbuildable (`:kotoba.error/namespace-require-needs-project`). ADR-0135 spent
that on one import: `hkdf-sha256.kotoba` stopped carrying its own copy of
SHA-256 and started importing `aiueos/lib/sha256_core.kotoba`.

It also left four things written down, which this ADR is the answer to:

1. Eight sources declare `(:require ...)` and six had no `.o` at all.
2. `sha256.kotoba` and `value-runtime-sha256.kotoba` were still copies of each
   other, as were `digest-equal.kotoba` and `value-runtime-digest-equal.kotoba`.
3. `tls13-record.kotoba` still carried a copy of `aes128-gcm.kotoba`'s entire
   AEAD, and said so in a comment that asked the reader to diff two files.
4. The `:verify-admissions` alias's compiler was said to be too old to check the
   contract ADR-0135 added.

Point 4 turned out to be wrong, and finding out why is the first result here.

## The reported failure was real, and it was not the pin

The report was that `verify-admissions.cljs` fails with
`source reader rejected input {:phase :read}` for **every** contract, and that
the fix was to advance the pinned `kotoba-sema` (244765d4) past the
linked-source printer fix.

Measured 2026-09-02, before changing anything:

* On the classpath the `:verify-admissions` alias resolves — amu `0085e138`,
  which carries kotoba-sema `8676e3d6` — `hkdf-sha256-v1.edn` **passes**:

  ```
  CONTRACT :aiueos.hkdf-sha256/v1 vectors=18 traps=0 memory=11 observed=0,1,2,3,4,5,6 ms=296088
  CONTRACTS 1
  {... :amu-sha "0085e138484b807dddcdec397c87de6513461881" ... :status :passed}
  ```

* On the classpath **`-M:test`** resolves — amu `6889fa73`, which carries
  kotoba-sema **`244765d4`**, the SHA the report names — the same contract fails
  in under a second:

  ```
  FAILED: source reader rejected input
     {:phase :read}
  ```

So the two closures behave exactly as `deps.edn` says they should. What was
broken is that **`verify-admissions.cljs`'s own run instruction named the wrong
alias.** Its header said

    --classpath "$(clojure -Spath -M:test)"

while `package.json` said `-M:verify-admissions`, and the file's receipt reads
the amu pin out of the `:verify-admissions` alias. Anyone following the
instructions in the file could not check a single contract, and the error they
got named a reader rather than a classpath.

The header now names `-M:verify-admissions`, quotes the failure the other alias
produces, and says why the two pins differ.

**This is the shape ADR-2608136000 is about, one level up from code.** A run
that could not be performed returned the same kind of output as a run that
performed badly, and the difference lived in a comment.

### The pins moved anyway

`:verify-admissions` now names amu `bb51dc14` (kotoba-sema `1acb9f83`,
kotoba-kir through amu `c4149fb1`) and pins kotoba-kir `0fd7e259` explicitly.
The old rationale for the explicit kotoba-kir pin — amu's own pin predating
`kir/execute`'s optional memory image — has expired (`c4149fb` is 34 ahead of
the `674a5be` it used to name), so the comment now states the weaker reason
that is still true: closest-wins makes the interpreter a stated choice.

`:test` was **not** advanced. Its pin is deliberate (it is the compiler the
committed value-runtime verdicts were measured against) and moving it
re-measures ten receipts as a side effect. The defect was never that `:test`
was old; it was that a runner told you to use it.

## Six sources, two objects

Compiled through the project route at amu `bb51dc14` +
kotoba-native `8a8c510`:

| source | result |
|---|---|
| `cid-v1-admit` | **11,576 B**, `3aeb2308…` |
| `value-runtime-cas-verify` | **10,680 B**, `dfef74bb…` |
| `value-runtime-dispatch` | refused |
| `value-runtime-entry` | refused |
| `value-runtime-provider-policy` | refused |
| `value-runtime-provider-transport` | refused |

Both objects pass `verify-kotoba-kernel-object.py`: one export, zero imports,
one relocation, and their export names are in kotoba-native's
`kernel-object-entries`.

The four refusals are a **measurement**, and they are the same measurement the
reachability list has been reciting since ADR-0057:

```
:kotoba.error/subset-reject  "operation has no admitted type signature"
```

They name `kernel-value-provider-queue`, `kernel-value-provider-status`,
`kernel-value-runtime-arena`, `kernel-value-runtime-cas-scratch` and
`kernel-value-runtime-capability-table`. Those five identifiers appear in
**none** of kotoba-kir, kotoba-sema or kotoba-native. Nothing in this repository
can build these four, and the reason is not the packager any more — amu#742
answered that — it is that the operations they call do not exist.

There is a second, independent obstacle for two of them that is worth recording
because it is not visible from the refusal: `value-runtime-provider-policy`
declares two `aiueos-` exports and `value-runtime-provider-transport` declares
three, and the object route selects **exactly one** public symbol
(`elf64.cljc`'s `object-entry` takes the first `kernel-object-entries` key
present in the artifact's exports). Packaging either would silently drop the
rest. `aiueos-value-runtime-provider-submit` is not in
`kernel-object-entries` at all. Both would have to be answered before those two
could be objects even with the operations in place.

**Neither new object is linked.** Nothing in the kernel calls
`kotoba_aiueos_cid_v1_admit` or `kotoba_aiueos_value_runtime_cas_verify`, and
linking an object into the kernel image is a decision about the kernel, not a
way to make a test greener. They are verified unconditionally in
`build-uefi.sh` with pinned digests, exactly as the qwen35 objects are.

## One SHA-256, one digest comparison, one AEAD

### sha256 and digest-equal: the copies are gone, the objects did not move

`sha256.kotoba` and `value-runtime-sha256.kotoba` were byte-identical apart from
the `ns` form; so were `digest-equal.kotoba` and
`value-runtime-digest-equal.kotoba`. The kernel links objects built from the
first of each pair; the second existed only so `cid-v1-admit` and
`value-runtime-cas-verify` could paste it in.

The two copies are deleted. The survivors moved to the path amu's resolver
derives from their namespace — `aiueos/sha256.kotoba` and
`aiueos/digest_equal.kotoba` — and gained an `(:export ...)` vector, which a
project module must declare (`project module requires an explicit :export
vector`).

**Adding that vector is byte-neutral, and that was measured at the compiler the
byte-attestation script pins, not assumed.** At amu `9cf3a0a` — the `expected=`
in `reproduce-kotoba-kernel-object.sh` — each source compiles to the same
object with and without the export vector, and both equal the committed
artifact:

```
af378b061725473bf4aa66d02d276973ffc5c7cef4b0ed1f4a0e01fc754a7753  sha256      (plain)
af378b061725473bf4aa66d02d276973ffc5c7cef4b0ed1f4a0e01fc754a7753  sha256      (with :export)
af378b061725473bf4aa66d02d276973ffc5c7cef4b0ed1f4a0e01fc754a7753  os/aiueos/kotoba/sha256.o   (committed)

6156db8b78f883610521ac4eb458cb98df655b26087e7d6808279c8b9d927b78  digest-equal (plain)
6156db8b78f883610521ac4eb458cb98df655b26087e7d6808279c8b9d927b78  digest-equal (with :export)
6156db8b78f883610521ac4eb458cb98df655b26087e7d6808279c8b9d927b78  os/aiueos/kotoba/digest-equal.o (committed)
```

So `sha256.o` and `digest-equal.o` are **unchanged**, their `cmp`s in the
reproduce script still compare what they always did, and the only edit that
script needed was the two paths.

### The AEAD: one core, one parameter

`aes128-gcm.kotoba` and `tls13-record.kotoba` shared 56 function names. 48 were
byte-identical. Seven of the remaining eight differed only in a parameter name
(`data` versus `body`) or a trailing comment. **One differed for a reason:**
`set-j0` reads the 12-byte nonce from `ctx+16` for the bare AEAD and from
`ctx+480` for the record layer.

`aiueos/lib/aes128_gcm_core.kotoba` now holds all of it — 57 definitions, 399
lines: the derived S-box, the key schedule, the cipher, GHASH, CTR, the tag
comparison. `set-j0` and `gcm-prepare` take the nonce offset as an argument.
The two entries call `(gcm/gcm-prepare ctx 16)` and `(gcm/gcm-prepare ctx 480)`.

The old comment in `tls13-record.kotoba` asked a reader to diff the two files
and promised the diff was 52 lines. That promise is gone, not because it was
wrong but because **there is nothing left to diff**: the one difference is an
argument, and drift is no longer expressible.

## The measurement that made the rebuild safe

Rebuilding a linked object mixes two changes — the source edit and whatever the
compiler has done since the artifact was produced. They were separated first.

**Control.** The *unmodified* sources, compiled at amu `bb51dc14` +
kotoba-native `8a8c510`, reproduce the committed objects byte for byte:

```
a3d22ecd5761e0273015267a7c3a59823fce94f545c9ee8b725b3728da665465  aes128-gcm.o   (committed, and rebuilt from HEAD source)
46d7bf41d9c50f519ef499134f0c021d06ab7dea256002f0c56e43f83391c46f  tls13-record.o (committed, and rebuilt from HEAD source)
```

That is worth stating on its own: ADR-0129 measured that 58 of 66 committed
objects did not reproduce at the compiler of the day. These two do, at this one.
So the delta in the new objects is the extraction and nothing else.

**After.** `aes128-gcm.o` 14,232 B (`65492e52…`, +144 B); `tls13-record.o`
17,736 B (`5c3dee81…`, +144 B).

**QEMU, production UEFI kernel, unchanged script.** The serial log is
**byte-identical to the pre-change baseline** — `diff` is empty, not
"equivalent":

```
AIUEOS_X25519_OK rfc7748-base-point 32-bytes
AIUEOS_AES_GCM_OK aes-128-gcm nist
AIUEOS_HMAC_HKDF_OK sha256 rfc4231-rfc5869
AIUEOS_ECDSA_P256_OK rfc6979-sample s+1-refused
AIUEOS_TLS13_RECORD_OK rfc8448-s3 seq0 seal-open tamper-refused
AIUEOS_UEFI_SMOKE_OK
```

`TLS-PARITY ok aes-gcm record 6-cases` is **not** reproducible and was not
re-run: ADR-0134 says the transition harness was deleted in the same change that
made the flip, and `git grep TLS-PARITY` finds it only in that ADR's prose.

## Three break/restore demonstrations, and one of them failed

A green smoke test that would be green anyway is not evidence, so the objects
were deliberately broken.

**Break 1 — and it did NOT discriminate.** `tls13-record.kotoba` was changed to
call `(gcm/gcm-prepare ctx 16)`, i.e. to read the nonce from the base IV slot
rather than from the derived nonce. The object changed
(`96d6244d…`), the image was rebuilt, and the serial log came back
**byte-identical to the baseline**, `AIUEOS_TLS13_RECORD_OK` and all.

The reason is arithmetic, not luck: the nonce is `iv XOR seq`, the boot
self-test is `seq0`, and `iv XOR 0 == iv`. The two offsets hold the same bytes
for the only sequence number the self-test uses.

This is recorded rather than quietly replaced, because it is a fact about the
kernel's coverage: **the boot self-test does not discriminate the record
layer's nonce offset.** A regression that pointed `set-j0` at the base IV would
ship green and fail on the second record of every connection.

**Break 2 — the shared core.** `xtime`'s GF(2^8) reduction constant was changed
from 27 to 26 in `aiueos/lib/aes128_gcm_core.kotoba` and **both** objects were
rebuilt from it. The boot stops at the first crypto self-test:

```
AIUEOS_X25519_OK rfc7748-base-point 32-bytes
AIUEOS_AES_GCM_FAIL nist-vectors
```

exit 1; `AIUEOS_AES_GCM_OK`, `AIUEOS_HMAC_HKDF_OK`, `AIUEOS_ECDSA_P256_OK`,
`AIUEOS_TLS13_RECORD_OK` and `AIUEOS_UEFI_SMOKE_OK` all absent.

**Break 3 — isolating the record layer.** Break 2 proves `aes128-gcm.o`
executes the core, but the boot halts before the record self-test, so it says
nothing about `tls13-record.o`. So `aes128-gcm.o` was restored from the good
core and **only** `tls13-record.o` was rebuilt from the broken one:

```
AIUEOS_X25519_OK rfc7748-base-point 32-bytes
AIUEOS_AES_GCM_OK aes-128-gcm nist
AIUEOS_HMAC_HKDF_OK sha256 rfc4231-rfc5869
AIUEOS_ECDSA_P256_OK rfc6979-sample s+1-refused
AIUEOS_TLS13_RECORD_FAIL rfc8448-s3
```

exit 1. Both objects genuinely execute the shared module, each break landed
where it was aimed, and the failure reason is the one the break was about.

## The reachability test could not see a module

`kotoba_object_reachability_test.clj` listed **one directory**. Every module
under `aiueos/` — `sha256_core.kotoba` since ADR-0135, and now three more — was
invisible to it: not built, not declared, not looked at. A module nobody
imported would have sat there unnoticed, which is precisely the state the test
exists to make impossible.

It now walks the tree and admits a third answer beside "a script builds it" and
"it is declared unbuilt": **"another source imports it"**, resolved by amu's own
rule (`.` → `/`, `-` → `_`). A new assertion,
`the-tree-walk-actually-descends`, fails if the walk ever stops at the top —
the previous bug was not a wrong answer, it was a shorter list, and a shorter
list passes every other assertion in the file.

**A second defect, found while fixing the first.** `built-here?` searched the
concatenated text of every script, and `build-uefi.sh` contains the line

    # `hkdf-sha256.o` is deliberately NOT here: it does not return ...

so a substring scan read that comment as a build. `hkdf-sha256` was
`built-here?` **because a comment said it was not**, and its entry in the
declared list was therefore rejected as decoration. The scan now drops comment
lines (`#` and `;`, after leading whitespace).

## Two other tools were reading the flat layout

* **`build-k16-pure-native.cljs --emit-provenance`** derived an object's source
  as `<stem>.kotoba` in the flat directory and stops the run with
  `source-absent` if it is missing. It now falls back to the ns→path spelling,
  and `kernel-recipe-pin`'s regex reads `kotoba/aiueos/<munged>.kotoba` as well
  as the flat form — without that, `sha256.o` and `digest-equal.o` would have
  been silently demoted to `:compiler {:sha nil :recipe :unrecorded}`, a quieter
  receipt rather than an error. Regenerated: 75 objects, 47 recorded,
  28 unrecorded.
* **`verify-jvm-free-object-parity.cljs`** compiled every source without
  `--source-path`, so all five project-route objects would have come back
  `:failed` — a statement about the invocation, not the object. It now retries
  with `--source-path` when, and only when, the compiler's own refusal names
  `namespace-require-needs-project`. Passing the flag unconditionally is **not**
  the fix and that was measured too: `fnv1a.kotoba` declares no `ns` at all and
  is refused with `project module requires exactly one namespace`. It also
  looked for a source only at the flat name, which would have dropped
  `sha256.o` and `digest-equal.o` from the run.

  Fixing that surfaced a **route divergence in amu worth recording**: the two
  routes disagree about what a bare `--source-path` means. The JVM route
  refuses a multi-module compile outright — `a multi-module compile needs
  pinned inputs` — while the JVM-free route proceeds and reports
  `:kotoba.compile/inputs :unpinned-source-path`. The script now sends
  `--unpinned` with `--source-path` on both routes so the comparison is about
  the object rather than about that difference; measured 2026-09-02, adding the
  flag to the JVM-free route leaves the bytes unchanged (`cid-v1-admit`,
  `3aeb2308…` either way). The divergence itself is amu's to answer.

### The line counts

| | before | after |
|---|---|---|
| `aes128-gcm.kotoba` | 519 | 132 |
| `tls13-record.kotoba` | 616 | 216 |
| `aiueos/lib/aes128_gcm_core.kotoba` | — | 442 |
| **AEAD total** | **1,135** | **790** |
| `sha256.kotoba` + `value-runtime-sha256.kotoba` | 176 + 177 | 177 (one file) |
| `digest-equal.kotoba` + `value-runtime-digest-equal.kotoba` | 18 + 19 | 19 (one file) |

The AEAD figure is the one that matters, and it is not really 345 lines: it is
that an S-box, a key schedule and a tag comparison existed twice and now exist
once.

## What is NOT done

* **The full 66-object roster is still not regenerated.** ADR-0132 measured that
  job at roughly nine hours (the JVM half of the comparison costs ~8 minutes per
  object on this machine) and the script's own `tree-digest` guard discards the
  run if the compiler is touched meanwhile. This ADR extends the SCOPED receipt
  instead.
* **Four of the six sources still have no object**, for the measured reason
  above. Closing that is a change to kotoba-kir / kotoba-sema / kotoba-native
  (five operations) plus an answer to the one-public-symbol rule for the two
  multi-export sources — none of which belongs in this repository.
* **`hkdf-sha256.o` was not rebuilt.** Its non-return under QEMU is still
  undiagnosed (ADR-0134) and is being worked on elsewhere; rebuilding it here
  would put a new artifact under that investigation.
* **The boot self-test does not cover the record-layer nonce offset** (Break 1).
  A `seq1` case would close it. Not added here: it is a change to
  `kernel/main.c`'s self-test, not to these objects.
* **`aes128-gcm.o` and `tls13-record.o` remain `:recipe :unrecorded`** in
  provenance, even though this ADR shows they reproduce at amu `bb51dc14`. The
  `:recipe` field keys off `reproduce-kotoba-kernel-object.sh`, which pins
  `9cf3a0a`; advancing that pin means regenerating every object it names, which
  ADR-0129 already identified as its own decision.
