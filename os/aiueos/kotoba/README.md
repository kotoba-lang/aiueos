# Kotoba native kernel input

`kernel-probe.o` is the byte-for-byte output of the merged
`kotoba-lang/compiler` commit
`b6466cf6a02adb4868b505b561371bf2130494a5` for `kernel-probe.kotoba`:

```clojure
(defn main [] 42)
```

It was produced with:

```sh
bin/kotoba-compiler compile /path/to/aiueos/os/aiueos/kotoba/kernel-probe.kotoba \
  --target x86_64-aiueos-kernel-v1 \
  --output kernel-probe.o
```

SHA-256:
`10d91712fccd887e68f9caa25413c8fa2c783968e72b1bead4025c6a294ffa42`.

Run `scripts/reproduce-kotoba-kernel-object.sh /path/to/compiler` to compile the
checked-in source with that west-pinned compiler checkout and compare both
objects byte-for-byte. This pinned object is temporary cross-repository CI
input and may only be updated from a reviewed compiler artifact. The aiueos
verifier validates every supplied object before link and forbids host imports
or dynamic/runtime dependencies.

`journal-plan.o` is produced by the same compiler revision from
`journal-plan.kotoba`. It exports the four-argument SysV function
`kotoba_aiueos_journal_plan`. Given validity and sequence values for both
bounded journal slots, Kotoba selects the latest committed slot and returns the
next sequence, alternate write slot, and recovery flag as a packed 64-bit plan.
The C substrate retains bounded virtio I/O and validates the returned plan
before replay or mutation. Its pinned SHA-256 is
`c24c7bdab170d65624c1ee2cb939b949c94750b651f59b5aa7d4bc192ec62df6`.

`fnv1a.o` moves every checksum used by the superblock, journal record,
transaction payload, and mutable object validators into Kotoba. Its
`kotoba_aiueos_fnv1a(base, length)` export uses `kernel-load-u8`, whose compiler
lowering rejects null bases, lengths above 512 bytes, and unsigned indices at
or beyond the supplied length before touching memory. Invalid access traps;
there is no host import or ambient address-space API. Each public call receives
an independent 1024-fuel budget, sufficient for the admitted 512-byte maximum.

`journal-record-valid.o` and `object-transaction-valid.o` construct
little-endian 32-bit fields from checked byte loads and perform magic,
version/state, length, sequence, and checksum validation in Kotoba. C supplies
only the address of its packed record and the exact structure size; invalid
records return false before replay or mutation.
