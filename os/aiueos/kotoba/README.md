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

`digest-equal.o` performs the fixed 32-byte SHA-256 comparison in Kotoba. It
always reads and accumulates all 32 byte differences before deciding, uses the
compiler's 512-byte bounded load primitive, and exports
`kotoba_aiueos_digest_equal`. Its pinned SHA-256 is
`6d005bf596ff10343377d9c243d473437fa272559b7f9130cba47cc4cd80d3aa`.

`app-catalog-valid.o` validates the authenticated aiuefs application catalog
in Kotoba: canonical header and IDs, one-to-four entries, 12 KiB extent bounds,
signer policy, capacity, and all data/signature/catalog collision pairs. Its
five-argument ABI receives catalog bytes, capacity, and an 8-byte routing
receipt containing the already-bounded catalog sectors. Its pinned SHA-256 is
`bf990c3775bd1351627daa669a124adad8e194710dc41d93f0c1b2ccfdacd927`.

`app-lookup-plan.o` scans every admitted packed metadata record for a 16-byte
application ID, validates `ready` and the 12 KiB length bound, and returns only
a packed one-based index/length plan. C dereferences the selected object after
rechecking that plan's public bounds. Its pinned SHA-256 is
`aa8ecea382820707638aa24e49226dbab243c95dc2a28ebfe3fac3a4dffe1a6c`.

`user-elf-valid.o` owns user-process ELF admission: the ELF64/x86-64 header,
the fixed RX and RW+NX load segments, all image bounds, and the 88-byte native
runtime context ABI. C only copies the admitted fixed-layout segments into a
new address space and maps them. Its pinned SHA-256 is
`b363aa7608f95c5fee37ddb95961c7e7524ca307f4d7407c4c25ca05435426ab`.

`user-context-build.o` constructs the complete 160-byte ring-3 interrupt
return frame in the final bytes of a bounded 4 KiB kernel stack. Kotoba owns
zeroed registers, RIP/RDI, user CS/SS, IF and user RSP; the C scheduler stores
only the returned frame pointer. Its pinned SHA-256 is
`8e743cba708c79e6800d5c0f26c68dfefe055179f2bef8e24753012a4bc21e5b`.

`page-mapping-plan.o` owns per-process virtual-page selection, private-page
isolation, user RX versus RW+NX permission classes, bounded image-page sizes,
duplicate-map rejection, and executable-entry admission. C translates the
admitted permission class to x86-64 PTE bits and installs physical pages. Its
pinned SHA-256 is
`c492472360f4632a5f4e0457ef3f2dd867306a36ea8ba3415cdb4463c78106b5`.

`process-create-plan.o` scans the complete eight-slot native process table and
owns domain validation and duplicate rejection, deterministic free-slot
selection, and non-zero 16-bit generation advancement including wrap. Its
recipe stages identity/address-space, execution, result, task binding, and
active publication around native resource acquisition. Its pinned SHA-256 is
`487d01555529e78c2df4321c467c807886b7ec7fa7a8f073701aed6e1ebf5f57`.

`process-teardown-plan.o` enforces the native teardown state machine: a reaped
task must revoke its owner's capabilities before its address space is
reclaimed, and only then may its descriptor become inactive. It also enforces
the domain-specific minimum revocation evidence and returns the final
execution/ownership/task/active/result clear recipe before reclaim is
committed. Its pinned SHA-256 is
`0a82d0757a24557e6b82de2ef195a712b5f489e0fb9acbe227ed2d9f62aecb13`.

`task-slot-plan.o` reads the complete nine-slot native scheduler table and
owns deterministic non-kernel slot allocation, non-zero generation advance
with wrap, stack-presence exclusion, and inactive-with-stack release
admission. Allocation plans carry the pointer/counter/CR3/service/generation/
active initialization recipe; release plans preserve generation and carry the
pointer/counter/CR3/service clear recipe. C only allocates/frees stack pages
and transactionally applies the admitted recipe. Its pinned SHA-256 is
`084118840d07e6e4db568215dac1e7c064b437de78f9c9043aa98a67469e077f`.

`scheduler-dispatch-plan.o` owns timer-tick exit-to-reaped admission and the
bounded round-robin selection of the next active task, including selection
against the post-reap table state. Its recipe drives reap, current-task and
switch counters, user-domain/kernel-stack publication, CR3 switching, and
outgoing context/counter updates. A restarted service retains its reconstructed
context instead of being overwritten by the interrupted frame. C applies those
admitted native mutations. Its pinned SHA-256 is
`b23dbea5125611ad041a16c548a083d94f0c4571ba68f5436e3feff16a099006`.

`task-exit-route.o` performs a complete bounded task-table scan for a requested
user domain, rejects kernel/invalid domains and duplicate active owners, and
returns the unique task slot eligible for an exit request. C only commits the
exit-request bit on that admitted slot. Its pinned SHA-256 is
`dbf1dacb2d4a2fc0adf49134cbd6b973fa3a85e780f3d2b242a9baacb28799d2`.

`service-task-transition.o` connects lifecycle candidates to native task state.
It returns the complete generation/restart/action commit plan plus an explicit
state/context/task mutation recipe, admitting spawn
only for inactive services with no assigned slot, restart/query only for a live task, and terminate
only for a non-current live task. C executes the admitted allocation, context
reset, or release without reconstructing lifecycle state or mutation intent.
Its pinned SHA-256 is
`a6b70f28d7b63a64b9b0ff0b66eba0e465a65caa39b0413f34eef5245d32d466`.

`rsa2048.o` implements RSA-2048 public exponent 65537 and the complete
PKCS#1 v1.5 SHA-256 encoded-message comparison in Kotoba. Its five-argument
kernel ABI accepts a 256-byte signature, 32-byte digest, and caller-owned
workspace. The public function requires 1284 bytes; compiler-emitted 4 KiB
load/store guards and a 250-million-unit fuel receipt bound every access and
loop. Its pinned SHA-256 is
`97a6c6b1f4c3f3569bf8d40423db924d291aa0b6f10cd7bace79f54e193387a6`.

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
`ab98299f535a2d0752135032b960d7830cca8aee4cdfff8a2f4952d897cfe3dd`.

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
`b0e9c90aaef5477fb5ababd6dd3067dd95a7eba93f3bb262cc49bace7e5a44ce`.
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
