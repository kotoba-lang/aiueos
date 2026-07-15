# Kotoba native kernel input

`kernel-probe.o` is the byte-for-byte output of the merged
`kotoba-lang/compiler` commit
`624d8f4e8adb2596b1151f22f843a9a73e797cb3` for the checked-in Kotoba sources:

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

`sha256.o` implements the complete application-admission SHA-256 path in
Kotoba. Its five-argument kernel ABI accepts the message, its length, a
32-byte output, and a caller-owned bounded workspace. `kernel-load-u8-16k`
admits at most 16 KiB while the public function narrows application input to
12 KiB; the function requires 352 bytes from its caller-owned workspace and
workspace/output stores retain the ordinary 512-byte compiler
bound. The wrapper replenishes ten million fuel units and compiler-lowered
tail recursion reuses a fixed native stack frame across blocks and rounds.
Its pinned SHA-256 is
`ad28e7d83d6e582df2dacf802e915fc9532fc99e141e174e7bf8642191db2c29`.

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
`object-transaction-route.o` additionally returns the checksum-validated object
class and target sector as one route receipt. Native virtio-blk code consumes
that receipt for service/domain apply and recovery instead of branching on raw
transaction fields. Its SHA-256 is
`b2d8c72642733d6ce84ac21516aa523d598fcd99f56cb84a1bca06a4b7ea547b`.

`superblock-valid.o` owns filesystem magic, header shape, object bounds, and
payload checksum validation. `mutable-object-valid.o` owns materialized object
magic/metadata/checksum validation and bounded byte equality against the
committed transaction. Together these complete the storage read-side
validation path in Kotoba; C retains sector I/O and passes exact buffer sizes.
The validator uses a non-recursive fixed-stack FNV and unrolled 16-byte
comparison, including the user-object readback and boot replay paths.

`journal-record-build.o` and `mutable-object-build.o` use the checked
`kernel-store-u8` lowering. Null, oversized, and out-of-bounds writes trap
before mutation. Kotoba now serializes journal/transaction metadata, sequence
payloads, checksums, mutable-object metadata, and transaction bytes. C clears
the sector, invokes the builder, and owns only the subsequent virtio-blk I/O.
The mutable builder copies the committed payload with fixed-stack unrolled
stores, so service and user transactions share the same Kotoba materializer.

The PCI planners validate real hardware-derived inputs: vendor capability
length/BAR/32-bit range, probed BAR extent shape, and MSI-X table/PBA regions.
Config-space reads, BAR probing writes, MMIO mapping, and interrupt programming
remain in C; their derived bounds must pass Kotoba before use.

`syscall-range-valid` owns the bounded half-open range decision used by both
CPL0 bootstrap and CPL3 log-write syscalls. It rejects empty, out-of-window,
high-half, and wrapping pointer/length pairs before the native syscall layer
can consume user memory. Interrupt entry and capability dispatch remain native.

`user-smoke.kotoba` is compiled with the least-privilege
`user-runtime-policy.edn`. Its admitted `cap-call 2` through `cap-call 5` lower to the compiler's
aiueos runtime-v2 trampoline and native syscall 5. The loader installs a
domain-owned object-read/service-send handle at context offset 80 only after authenticating
the ELF; the static context otherwise contains no handle or kernel address.
Both catalog processes read service-registry object 0 and send payload 42 to
their domain-bound persistent service mailbox before returning 42. The kernel
independently checks type, rights, owner, operation, object index, mailbox
capacity, recipient mapping, and payload bound on every call. Service tasks
consume both messages under their reserved CR3s and remain active after all
user address spaces and stacks are reclaimed.

`copy-in` then transfers an admitted payload into a 256-byte kernel-owned
buffer. Both source and destination accesses use the compiler's trapping
bounded-byte operations, recursion consumes replenished freestanding fuel, and
the syscall records a Kotoba FNV hash of only the copied bytes. Oversized calls
are rejected before either buffer is touched.

`capability-plan` derives the only admissible 63-bit handle from a table slot,
generation, type, active bit, rights, and the requested type/rights. The same
planner issues and checks handles. Revocation clears active state and advances
the generation before reissue, so stale, wrong-type, and insufficient-rights
handles cannot alias the live slot.
Generation exhaustion retires a slot instead of wrapping to an older identity.
The state also carries a 16-bit owner domain and each request carries its
caller domain. Owner equality is decided inside the planner, before payload
copy, so kernel and user slots cannot be used across their security domains.

`service-lifecycle` owns supervisor spawn, restart, and termination decisions.
It emits a packed action plus generation/restart state; a failure advances the
generation and restart count only while both remain bounded and the configured
budget is not exhausted. The native scheduler consumes that plan to allocate,
replace, or release a generic descriptor-driven task context; it does not
duplicate lifecycle admission in C. The pinned object SHA-256 is
`cd6d9c57cd4dd94839ef1a255c6d82b6c1b231c08aa1f7de86ab8c0029720816`.
Spawn accepts both a new zero-generation descriptor and a validated persisted
generation, allowing journal replay to recreate service tasks without resetting
their durable lifecycle counters.

`service-registry-build` serializes the two bounded scheduler service states
into a versioned 16-byte registry inside the journal transaction. It writes all
transaction and journal metadata and checksums with bounded stores. The native
virtio-blk substrate supplies the observed states, commits the journal before
materialization, and verifies readback/replay. Its pinned object SHA-256 is
`70eee5d4dd599ea2049261e92a656931768b355eefc0fb6d83deee192a3a05f0`.
After the common Kotoba transaction checksum validator passes,
`service-registry-state` validates the complete `SRV1` routing/schema contract
and returns either indexed state.
Its fixed-stack object SHA-256 is
`d73f13de0d86a4af46e33516b8b0f6358b5d477307c61d40624b971f34c15f3e`,
so the native substrate no longer parses service registry payload bytes.

`user-object-journal-build` defines the compiler-checked journal schema for
domain-owned objects. User tasks submit capability 4 writes and read capability
5 receipts; the kernel task commits domains 4/5 through independent dual slots
44–47 into objects 42/43. Recovery replays each domain's highest valid sequence
before new user code is admitted. Its pinned object SHA-256 is
`994d8a296d17afa67a8c9267cafa6079edca5068aeed46e78d8f455a40df1cfd`.
The paired fixed-stack validator is
`0f2015e53ed083741687abfbaff72edf8a525947b9fc753cacc7a1bf10faf46f` and
the value decoder is
`bd1de2777d75e02968939d2b7bc74e84dc16a8a9431fe36bd2c2170d6866fad3`;
all three use bounded loads/stores without recursive calls. The decoder is
called only after the complete domain-routed journal contract passes, so C no
longer parses user payloads.
