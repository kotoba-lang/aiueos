# aiueos bare-metal integration

This directory contains the Linux-independent aiueos boot path owned by the
canonical `kotoba-lang/aiueos` OS repository.

## Which path is production

The production hard-flip input is `native/kernel.kotoba`, compiled and packaged
by exact-pinned Amu into a C-free PE32+ loader and ELF64 kernel. It consumes
the freestanding ABI and instruction semantics owned by `kotoba-native`; this
directory owns OS policy, physical-memory admission, effects, and QEMU
evidence. `aiueos.vm` is the hosted QEMU launcher and `aiueos.hvt` is a JVM/FFM
experiment. Neither is the bare-metal provider.

The larger C/assembly implementation below is retained as a reference profile
and regression oracle. Its feature count must not be read as production C-free
maturity. The exact dependency/evidence rule is in the repository `README.md`
and `90-docs/adr/0013-native-os-ownership-and-boot.md`.

The current Phase 1 slice builds a PE32+ `BOOTX64.EFI` and a separate ELF64
`KERNEL.ELF`. OVMF starts the loader, which validates and places bounded ELF
segments, captures the firmware memory map, exits UEFI boot services, and
hands control to the kernel. The kernel validates the handoff and terminates
QEMU through the test-only debug-exit device. Its assembly entry switches to a
private 64 KiB stack before entering C, and its first hardware driver
initializes COM1 at 115200 baud. The kernel then installs its own GDT and IDT;
the smoke gate executes `ud2` and requires the vector 6 handler to terminate
QEMU. Before that test the kernel replaces the firmware CR3 with its own
four-level identity map, enables write-protect and NX, and maps text RX,
rodata R+NX, and writable state RW+NX. It does not use Linux, a JVM, GRUB, or a
host initramfs in the guest. The smoke test writes to text and attempts to
execute a byte in rodata; both must raise vector 14 with the expected x86 page
fault error-code bits before execution can continue.

The kernel link also consumes `kotoba/kernel-probe.o`, emitted by the Kotoba
compiler target `x86_64-aiueos-kernel-v1`. A fail-closed verifier permits only
the versioned freestanding ELF object ABI, and the VM gate requires the kernel
to call `kotoba_aiueos_probe` and observe result `42`. Set
`AIUEOS_KOTOBA_KERNEL_OBJECT` only to test another compiler output under the
same verifier; a hosted or import-bearing object is rejected before link.

The loader also selects the ACPI 2.0 configuration-table GUID. The kernel
validates both RSDP checksums, the XSDT and MADT checksums and lengths, and every
MADT subtable boundary. The QEMU gate starts two vCPUs and requires both to be
reported as enabled by MADT. The BSP then copies a real-mode trampoline below
1 MiB, sends INIT plus two SIPIs to the second MADT APIC ID, and requires the AP
to enter long mode on its own 64 KiB stack before the boot smoke may pass.

The BSP enables its Local APIC, maps the MMIO page cache-disabled, installs a
periodic timer on vector 32, enters `sti; hlt`, and requires the interrupt stub
to acknowledge EOI before the smoke test can continue.

The timer stub also preserves the complete x86-64 integer interrupt frame and
passes its stack pointer to a minimal round-robin scheduler. Two kernel tasks
run on separate 16 KiB stacks alongside the boot task. Each worker owns a
distinct CR3 and increments only its private page; the timer switch restores
the kernel CR3 before the boot task resumes. The QEMU gate proceeds
only after all three contexts have been preempted and both worker tasks have
resumed at least twice, producing `AIUEOS_SCHEDULER_OK` and
`AIUEOS_SCHEDULER_CR3_OK`. Interrupt and kernel mappings remain shared and
supervisor-only in every root.

The Phase 3 bootstrap retains a DPL0 `int 0x80` gate for kernel self-tests, but
CPL3 uses the architectural `SYSCALL`/`SYSRETQ` path. STAR/LSTAR/EFER/FMASK are
read back before user entry; entry validates the lower-half canonical RIP and
RSP, switches to the scheduler-published per-task kernel stack, and sanitizes
return flags before `SYSRETQ`. A tagged, generation-bearing
capability handle is required by the log-write admission path. The QEMU gate
proves that a stale generation, a non-canonical pointer, and a range crossing
the bootstrap mapping are denied before dereference. The process foundation reserves distinct U/S pages
for RX user text and RW+NX user data, leaves an unmapped guard page, and builds
a loaded 64-bit TSS descriptor with a dedicated kernel-entry stack. A one-shot
CPL3 processes enter through `iretq` and exercise valid and rejected native
syscalls through the current task's allocator-owned kernel stack. Per-process
address-space groundwork then constructs two distinct CR3 roots. Each root
clones the low kernel page-table path, shares the kernel/MMIO branches, maps a
different private user page, and leaves the other process's page non-present.
The smoke switches CR3 sequentially, proves independent contents, and requires
real non-present page faults for both cross-process reads before restoring the
kernel CR3. A bounded eight-entry process-create table now owns each user
entry, initial argument, stack top, domain, address-space slot, task slot, and
generation. It claims an available address-space root and creates the scheduler
task through a generic ABI; the kernel no longer wires two fixed entry functions
to roots 0 and 1. The boot slice creates two descriptors whose roots map distinct
private pages used for each process's result, message, and user stack. Domains 2
and 3 receive separate runtime
capabilities, successfully call the same syscall, reject each other's handles,
and reject the other process's unmapped private address. Both are then installed
as APIC-timer-preempted scheduler tasks. Each task has its own supervisor-only
interrupt stack; every switch updates CR3, TSS.RSP0, and the syscall owner
domain before `iretq`. Boot requires both tasks and the kernel task to be
preempted at least twice.
Boot reads a signed aiuefs-v3 application catalog from virtio-blk, then looks
up compiler-emitted `x86_64-aiueos-user-v1` ELF64 objects by their bounded
16-byte application IDs. The catalog and every application bind their distinct
sector ranges and lengths to SHA-256 digests and RSA-2048 PKCS#1 v1.5
signatures under the boot application public-key policy. SHA-256 padding,
message scheduling, compression rounds, and digest emission execute in the
compiler-emitted `kotoba_aiueos_sha256` object with a 12 KiB input bound,
caller-owned 512-byte workspace, and metered stack-safe loops. RSA-2048 modular
arithmetic and the complete PKCS#1 v1.5 encoded-message comparison execute in
the compiler-emitted `kotoba_aiueos_rsa2048_sha256_verify` object with a
caller-owned 1284-byte workspace, compiler-enforced 4 KiB memory ceiling, and
250-million-unit fuel ceiling. The fixed-work 32-byte digest comparison runs
in `kotoba_aiueos_digest_equal`; the C substrate contains neither digest nor
signature verification. Canonical IDs, extent bounds, signer policy, and every
catalog/application/signature sector collision are admitted by
`kotoba_aiueos_app_catalog_valid`; C performs only the resulting bounded block
I/O. Loader ID lookup and ready/length selection execute through the fixed-scan
`kotoba_aiueos_app_lookup_plan` object. The private key is not
present in the repository or image builder. Digest comparison and the complete
encoded-message comparison are constant-time and must pass before bytes reach
the loader. Negative QEMU gates mutate the catalog, an ELF, and an application
signature and require admission failure; an unknown application ID is also
rejected. The loader then admits exactly two bounded `PT_LOAD`
segments, rejects every unexpected header, address, flag, size, and range,
copies text and context into allocator-owned pages, and maps them RX and RW+NX
respectively in that process root. The generic scheduler enters the ELF entry
at CPL3. The authenticated runtime-v2 context admits only Kotoba capabilities
2 and 3,
contains an RX syscall trampoline, and receives a domain-owned runtime handle
from the kernel loader. Kotoba `main` uses that capability to read the persisted
service-registry object through native syscall 5; the kernel rechecks handle
type, rights, owner domain, capability ID, and bounded object index before
returning a scalar state. Capability 3 places a scalar message into one of two
bounded mailboxes: domain 4 targets persistent service 0 and domain 5 targets
persistent service 1. Those scheduler service tasks retain their reserved CR3,
consume and validate sender, recipient, sequence, and payload while user tasks
remain preemptible, and survive user-process reap. The compiler shim then
publishes result 42 in the
context page. The boot slice launches catalog entries `app/hello` and
`app/worker` as separate CPL3 processes in domains 4 and 5; both remain
preemptible until normal domain teardown. Recreating either image must reuse
zeroed text/context pages. The kernel no longer embeds ELF bytes. The release
build emits a deterministic `aiueos-x86_64-data.img`
alongside the GPT boot image.
The first process also transfers an attenuated log handle to domain 3 while the
capability-table lock covers source revalidation, target-slot issuance, and
publication. Domain 3 atomically claims and uses that handle. A request for a
right absent from the source handle is rejected by the Kotoba planner before
publication. Each derived slot records the parent slot and the parent's exact
generation. Revoking a parent walks that generation-safe graph and advances
every live descendant before releasing the table lock.

After all four user processes and both service deliveries complete their
evidence, the kernel requests user-task exit and waits until each user task is
removed while the kernel and two persistent service tasks remain runnable.
Owner teardown of domain 2 recursively revokes its domain-3
descendant before domain 3's remaining root is revoked, all with generation
advancement. Only after returning to the
kernel context are both allocator-owned supervisor interrupt stack pages
returned to the physical free list. Private mappings
are removed, their backing pages are zeroed, and the bounded process slots are
remapped to prove clean reuse. The complete per-process mapping path is now
allocator-owned rather than a static kernel array: each process consumes five
physical pages (PML4, PDPT, page directory, page table, and private backing).
The two syscall test processes return ten mapping pages; the Kotoba ELF process
adds its allocator-owned RX and RW pages for seventeen pages total. Exit returns
them through a validated, double-free-rejecting free list; recreation reloads
the ELF and must observe its writable result context zeroed.
Address spaces are assigned from an eight-slot generation table rather than
being identified by two permanent roots. Boot fills every remaining slot,
rejects allocation when full, reaps the temporary generation, and proves that
the lowest free slot is recreated with a new generation and zero backing.
Scheduler tasks use a separate eight-slot generation table. Boot fills the
task table, verifies exhaustion, reaps every temporary task, and recreates the
lowest slot from a zeroed physical stack page; no per-task interrupt stack is
statically reserved in the kernel image.

The pointer/length window admission for both bootstrap and CPL3
calls is compiler-emitted Kotoba code and is exercised at both valid boundaries
and rejected overflow/empty inputs. An admitted log payload is copied by Kotoba
bounded load/store operations into a fixed 256-byte kernel buffer and verified
by a Kotoba FNV receipt for both CPL0 and CPL3 calls; oversize requests fail
before memory access. General page-fault-recoverable copy-in remains later work.

The log capability is backed by a native slot table rather than a fixed magic
constant. A compiler-emitted Kotoba planner encodes and admits handles from the
slot generation, object type, active state, rights, and owner domain. Kernel
and CPL3 receive distinct slots. Boot proves each caller rejects the other
domain's handle, revokes generation 1, reissues generation 2, and exercises
stale, foreign-owner, wrong-type, and no-rights rejection again from CPL3.
The table is allocated as a zeroed physical page after memory-map admission,
providing at least 256 slots instead of a compiled four-entry array. Allocation
and admission are spinlock-serialized. Boot allocates a third owner domain,
reuses its revoked slot only with an advanced generation, and rejects the same
handle from another domain. A kernel self-test constructs a two-hop derivation,
revokes its root, rejects both stale descendants, and proves root-slot reuse
cannot reconnect children from the prior generation.
An exhausted generation retires its slot rather than wrapping and reviving an
old handle.

The PCI path performs a bounded configuration-space scan and validates modern
virtio vendor capabilities, including a cycle-limited capability chain, BAR
kind and width, and overflow-safe capability ranges. PCI MMIO is identity
mapped UC/NX only after validation, including QEMU's 64-bit MMIO window above
512 GiB. The virtio-rng smoke path negotiates `VIRTIO_F_VERSION_1`, allocates
separate zeroed pages for the descriptor, available ring, used ring, and data
buffer, submits one writable 32-byte request, and requires the device's used
ring completion. The same bounded capability parser also drives a modern-only
virtio-blk device. It reads the generation-stable capacity, rejects an empty or
overflowing device, submits a three-descriptor `VIRTIO_BLK_T_IN` chain, and
requires a 513-byte used completion, success status, and deterministic sector-0
identity. The smoke disk is a separate writable 1 MiB fixture, so neither the
ESP nor a release image can be modified by this gate. Sector 0 remains a
bounded read-only `aiuefs-v1` root. Sectors 1 and 2 form a dual-slot redo
journal: boot validates both records,
selects the highest valid committed sequence, then appends the next sequence to
the alternate slot and verifies it by readback without destroying the prior
commit. Each payload is a bounded object transaction for sector 3. A committed
payload is replayed idempotently before append; the new journal commit is made
durable before its object mutation, and both writes require readback. The VM
gate creates matching journal/object sequences 1 and 2, corrupts the latest
slot, and requires fallback, redo, and reconstruction of sequence 2. This is a
single-object transactional slice with a two-record rollback window, not yet a
general allocator, filesystem, or kotobase IStore. The blk queue uses MSI-X
vector 35 for synchronous sector completions and sleeps with interrupts
enabled instead of polling. The rng queue uses a bounded MSI-X
capability walk, validates the complete table and PBA against probed BAR
extents, maps their MMIO UC/NX, and requires vector-34 IRQ evidence before
accepting the DMA completion.  MSI-X for the remaining transports,
indirect descriptors and a reusable multi-request transport remain later Phase 4 work.

ACPI DMAR discovery validates the complete table, bounded remapping structures,
DRHD register bases, and variable-length device scopes. The QEMU VT-d gate owns
the selected segment-0 remapping unit, installs legacy root/context and four-level second-level
tables, limits domain 1 to the first 128 MiB, invalidates caches, and requires
hardware `GSTS.TES` before PCI DMA. Unsupported DMAR topologies fail closed. Only the QEMU bring-up profile
may use unisolated DMA when DMAR is absent, and its serial evidence explicitly
labels that exception `test-only-unisolated` rather than claiming isolation.
When QEMU advertises `ECAP.IR`, the kernel also owns a bounded 256-entry
interrupt-remapping table. The blk IRTE validates the complete PCI requester
ID, targets vector 35, and uses remappable-format MSI-X with zero data. IRTA
pointer status and interrupt-remapping enable status are required while
translation remains enabled; unsupported IR capability or topology fails
closed in the DMAR profile.

The desktop transport bootstrap obtains the active UEFI GOP mode before
`ExitBootServices` and hands only the aperture base/length, dimensions, stride,
and RGB/BGR format to the kernel. The kernel independently validates every
bound, maps the aperture supervisor-only RW+NX and uncached in a dedicated page
directory, then presents a deterministic retained-rectangle test frame. A
stable readback hash is required before `AIUEOS_FRAMEBUFFER_OK` is emitted.
The kernel packages that real GOP result as a versioned desktop-surface
envelope with an opaque surface handle, generation, content hash, pixel
metadata, and full-surface damage. A generation-checked, rectangle-bounded copy
operation transfers pixels into caller-owned memory; no physical address is exposed. QEMU uses a
modern `virtio-vga` device, submits `GET_DISPLAY_INFO` on its real controlq,
validates the returned enabled scanout, and binds the envelope only when its
dimensions match the GOP surface.
This is the native display capability boundary for the browser-owned desktop:
the browser remains the workspace/focus/permission authority, while the kernel
only admits validated surfaces and hardware input. Direct framebuffer mapping
is not granted to the browser. The input boundary uses a versioned, sequenced
envelope (`pointer`, `key`, or `text`); raw virtio DMA memory stays kernel-only
and IME interpretation belongs to the browser desktop authority. The default
QEMU smoke still compiles with `AIUEOS_INPUT_SMOKE_SYNTHETIC` because gpu /
guest-paint must stay green without a used-ring event. `nbb --classpath src
scripts/compositor-guest.cljs guest-input` (ADR-0093) rebuilds without that ifdef and admits only a
virtio-keyboard used-ring event injected by QMP `input-send-event`. HMP
`sendkey` is not that gate. Production builds do not enable the synthetic
fallback.
Virtio 2D resource creation, backing attachment, transfer/flush, a compositor,
mapping the surface into a user component, ambient display authority, and an
invented browser runtime are intentionally excluded.

```sh
./os/aiueos/scripts/build-uefi.sh
./os/aiueos/scripts/smoke-qemu-uefi.sh
./os/aiueos/scripts/build-release-image.sh
./os/aiueos/scripts/smoke-qemu-release-image.sh
```

### Physical K16 qualification before installation

The K16 stage-one image keeps every internal disk read-only while returning a
machine-readable result on the removable USB.  v4 added a dedicated 9 MiB FAT16
Microsoft-basic-data partition labeled `AIUEOS RSLT`; macOS mounts it as a
normal user volume instead of requiring privileged access to an EFI System
Partition.  The probe writes `PROBE.LOG` there, arms the
current USB entry as one-shot `BootNext`, and chainloads an AIUEOS native-core
profile.  The native core stores only a bounded UEFI result record and resets;
the probe then boots once more, writes `RESULT.LOG` to the same USB's result
partition, and stops with `RESULT SAVED`.  Binding requires the loaded USB
device-path prefix, the exact result partition GUID, and the exact
`AIUEOS.ID` marker; there is no fallback write to another filesystem.  A
success result proves only the loader/kernel,
integrity, Kotoba object, paging, GOP, allocator, and ACPI floor.  It stops
before IOMMU, PCI DMA, block/network drivers, Murakumo, or any internal-SSD
write.  GOP discovery first checks the firmware console handle, then performs
a bounded protocol-handle scan for firmware that publishes GOP separately.
The UEFI probe also walks each xHCI controller's extended-capability list using
bounded MMIO reads and records whether optional Debug Capability (DbC) is
present and which debug port it reports.  It does not enable DbC or perform any
PCI/MMIO write; live USB logging remains gated on the physical K16 result.
If the UEFI loader returns before entering the kernel, it now prints the exact
failure marker and code, stores a bounded failure record in the existing
qualification variable, and returns to the probe.  The probe immediately writes
that failure to `RESULT.LOG`; a real machine no longer sits at the initial
`loading kernel.elf` line with its reason visible only to QEMU debugcon.  Loader
codes are 101 through 118 in execution order; code 110 is fixed-address ELF
segment allocation, the physical-K16 failure suspected by the v4 observation.
The v5 code map is authoritative in
`contracts/physical-qualification-usb-v5.edn`; v4 remains the historical
layout/result contract.  v6 also persists progress codes 201 through 209
*before* firmware calls and native-core handoff.  A 90-second firmware watchdog
resets a hung qualification run so the existing one-shot `BootNext` collector
can write the last progress code to `RESULT.LOG`; a manual power cycle remains
the fallback when firmware does not implement the watchdog.  The final memory
map is obtained only after persisting progress and completing GOP discovery, so
`ExitBootServices` receives the current map key.
The physical K16 returned v6 code 209, proving ELF admission, fixed-address
segment allocation, and GOP discovery while narrowing the hang to
`ExitBootServices` or the native-core path.  v7 persists code 211 after a
successful `ExitBootServices`, then codes 220 through 229 before the kernel's
entry, early self-tests, GDT/IDT, PIC, paging, cryptography, framebuffer,
allocator, ACPI, and final result stages.  These records use the same bounded
16-byte UEFI variable and still never reach a block driver.
The physical K16 then returned v7 code 224: native entry, early self-tests,
GDT/IDT and PIC shutdown all completed, and the stop is inside paging setup.
v8 records the NX, table-build, NXE, write-protect, pre-CR3, post-CR3 and
validation boundaries.  Its CR3 codes also carry the inherited handoff shape:
260/270/280 are four-level plaintext, +1 means firmware left CR4.LA57 active,
and +2 means the live AMD firmware CR3 carries the CPUID-reported SME C-bit.
The replacement root preserves either condition: a fifth-level root is used
when LA57 is already active, and encrypted RAM/table addresses inherit the
C-bit while MMIO mappings remain unencrypted.
The physical K16 returned v8 code 260, so its handoff is four-level and the
firmware CR3 does not carry an SME C-bit; the stop is at the first owned CR3.
v9 additionally records firmware CET in feature bit +4, disables the
firmware-owned shadow-stack control before replacing its address space, and
uses a bounded 1 GiB 2 MiB-leaf transition root.  The transition root is never
published as `kernel_cr3`: v9 records 260+features before it, 270+features
after it, 280+features before the final split W^X root, 300+features after that
root, and 310+features only after the final permissions validate.
The physical K16 also returned v9 code 260 with no feature bits.  This proves
LA57, SME and CET were absent, but it does not distinguish the CR3 load/first
instruction from the following C call that hands back to UEFI.  v10 makes that
boundary observable: it clears inherited PGE (+8) and PCIDE (+16, only after
reloading the firmware root with PCID zero), then switches to each candidate
root, reads CR3, and switches straight back in one inline assembly block with
no C call under the candidate map.  Durable markers 320, 352, 384, 416, 448
and 480 (+feature bits) mean before normalization, after normalization, after
the transition-root round trip, after the final-root round trip, after a C
call under the final root, and final W^X validation respectively.
The physical K16 returned v10 code 416 with no feature bits.  Both candidate
CR3 loads and readbacks therefore passed with four-level paging and no active
SME, CET, PGE, or PCIDE handoff bit.  v11 narrows the remaining boundary by
entering the qualification recorder through assembly: its first instructions
save the candidate CR3 and restore the known firmware CR3 before any C
prologue, global access, or additional stack use.  The C SetVariable helper and
the final reset helper now run only under the firmware map; their assembly
entries restore the candidate root before returning.

For repeated v11 trials, the diskless PXE path removes the USB move from the
diagnostic loop.  It builds one deterministic PE/COFF file containing the
fresh kernel and initramfs, verifies both against their compiled SHA-256
digests, arms only the current PXE option as one-shot `BootNext`, and stores a
bounded 16-byte qualification record in UEFI NVRAM.  The normal-user Mac
server serves that native file once and then falls back to the read-only
control EFI.  On the following boot, the control EFI reports
`AIUEOS_QUALIFICATION_RESULT` over the already-qualified direct Ethernet PXE
path and remains ready for a bounded `ping` or `reboot-pxe` command.  It never
writes `BootOrder`, USB storage, or an internal disk.  A native-core hang can
still require one physical power cycle because no K16 NIC driver or resident
AIUEOS control plane exists after `ExitBootServices` yet.
Before each native trial, the same control EFI also emits one
`AIUEOS_RTL8125_HANDOFF` record per readable RTL8125.  It contains the exact
PCI command, working BAR, MAC, chip command, Tx/Rx configuration and PHY status
left by the firmware PXE driver.  This is a bounded read-only snapshot: it does
not reset the NIC, enable DMA, acknowledge an interrupt, or modify a register.

```sh
SOURCE_DATE_EPOCH=0 \
  ./os/aiueos/scripts/build-physical-qualification-pxe.sh
./os/aiueos/scripts/smoke-qemu-physical-pxe.sh

python3 ./os/aiueos/tools/k16-pxe-server.py \
  --next-boot build/aiueos-physical-qualification-pxe/aiueos-k16-native-pxe.efi \
  --control reboot-pxe
```

The QEMU smoke boots a FAT view containing only `BOOTX64.EFI`, then substitutes
the control EFI behind the same boot option while retaining the variable
store.  Its green result proves embedding, admission, native-core completion,
result persistence, control fallback, and deterministic rebuilding in QEMU;
it deliberately reports `physical-k16=unverified`.  Only the subsequent K16
`state=success code=0` observation can satisfy the physical gate in
`contracts/physical-qualification-pxe-v1.edn`.

```sh
SOURCE_DATE_EPOCH=0 ./os/aiueos/scripts/build-physical-qualification-usb.sh
./os/aiueos/scripts/smoke-qemu-physical-loader-failure.sh
./os/aiueos/scripts/smoke-qemu-physical-loader-hang.sh
./os/aiueos/scripts/smoke-qemu-physical-kernel-hang.sh
```

After `RESULT SAVED. REMOVE USB AND CONNECT IT TO THE MAC.`, power off and move
the USB to the Mac.  `AIUEOS RSLT` mounts without an administrator password,
with `PROBE.LOG` and `RESULT.LOG` at its root.  Check the result without
interpreting the raw log by running:

```sh
./os/aiueos/scripts/check-physical-qualification-result.sh
```

An optional user-level LaunchAgent can watch for an attached volume (no
`sudo`):

```sh
./os/aiueos/scripts/install-macos-result-watcher.sh
```

The latest automatic decision is written to
`~/Library/Logs/AIUEOS/latest-result.txt`.  macOS privacy controls can deny a
background LaunchAgent access to `/Volumes` even when the interactive checker
works, so the interactive command above remains the authoritative retrieval
path unless Full Disk Access has been explicitly granted.  This watcher is
mount-triggered result retrieval, not the still-gated DbC real-time transport.

For the K16, the real-time path is qualified separately with the xHCI Debug
Capability.  The dedicated UEFI probe does not open block I/O, leave firmware
boot services, write the USB at runtime, or touch an internal disk.  It exposes
one vendor-defined SuperSpeed bulk interface to the Mac and keeps the five DbC
controllers observed on the physical K16 armed while the normal-user libusb
receiver reconnects automatically:

```sh
SOURCE_DATE_EPOCH=0 ./os/aiueos/scripts/build-dbc-live-usb.sh
receiver=$(./os/aiueos/scripts/build-dbc-receiver.sh)
"$receiver"
```

`AIUEOS_DBC_MAC_CONNECTED`, a received `AIUEOS_DBC_LIVE` heartbeat, and then a
later heartbeat with `rx>=1` are all required.  Together they prove K16-to-Mac
streaming and a Mac-to-K16 acknowledgement.  A compiled client or a visible
Type-C cable alone is not physical transport evidence.  The exact gate is in
`contracts/dbc-live-v1.edn`.

It prints `AIUEOS_K16_PHYSICAL_RESULT_OK` only when the native-core result and
the required read-only probe markers are both present.  The
exact v11 gate and later SSD-install prerequisites are in
`contracts/physical-qualification-usb-v11.edn`; v1 through v10 remain historical
contracts.  Secure Boot must be disabled for this unsigned
development image.  A green QEMU result is build evidence, not a physical K16
result.

The release-image command creates a deterministic 64 MiB GPT raw disk image
with a protective MBR and a FAT32 EFI System Partition. The ESP contains
`EFI/BOOT/BOOTX64.EFI` and `EFI/AIUEOS/KERNEL.ELF`. It also emits a canonical
JSON build receipt with the SHA-256 digest and byte size of the disk and both
boot artifacts. Set `SOURCE_DATE_EPOCH` to record a release timestamp without
making the disk image host-time-dependent. The image builder uses only Python's
standard library; validation checks both GPT CRCs, the ESP layout, FAT chains,
boot-image magic, and byte-for-byte embedded artifact contents.

The same build also emits `aiueos-x86_64.iso`, a deterministic ISO9660 image
with an El Torito EFI (no-emulation) boot entry. Its boot image is a 16 MiB
FAT16 volume containing the same `EFI/BOOT/BOOTX64.EFI` and
`EFI/AIUEOS/KERNEL.ELF`, so the catalog's 512-byte virtual sector count stays
within the entry's 16-bit field. The verifier checks the primary volume
descriptor, the El Torito boot record and validation-entry checksum, the boot
catalog extent, the `ESP.IMG;1` directory record, FAT16 chains, and
byte-for-byte embedded artifact contents; the receipt records the ISO digest
and catalog geometry.

The GPT disk also carries a second, independent 16 MiB FAT16 recovery ESP at
the end of the disk, byte-identical to the ISO's El Torito boot image and
holding known-good copies of both boot artifacts. When the primary loader
fails firmware `LoadImage` admission (its PE image is corrupted), the UEFI
boot manager falls back to the recovery partition and the complete evidence
gate must still pass. When the primary *kernel* fails the loader's compiled-in
SHA-256 admission, the loader emits the rejection marker and retries the same
kernel path on every other filesystem volume under the identical digest
requirement, so a fallback can only ever load the expected kernel bytes; it
announces `AIUEOS_LOADER_RECOVERY_OK` and fails closed when no volume passes.
The verifier validates the recovery GPT entry, FAT16 chains, and byte-for-byte
artifact contents, and the receipt records the partition extent, GUID, and
digest.

The release smoke first requires the verifier to reject a one-byte kernel
mutation inside the ISO and inside the recovery partition, then boots the GPT
disk, the ISO, a primary-loader-corrupted disk (firmware fallback), and a
primary-kernel-corrupted disk (loader digest fallback) through OVMF, requiring
the complete UEFI evidence gate on each boot. The `corrupt` subcommand of
`make-release-image.py` produces those deterministic mutations.

The protective MBR carries a 62-byte real-mode stage-1 stub as the legacy-BIOS
test fixture. BIOS is not a supported boot path: under SeaBIOS the stub prints
`AIUEOS_BIOS_STUB uefi-required` on the debug console and terminates
deterministically through isa-debug-exit (halting forever when that test
device is absent), so legacy firmware gets an explicit refusal instead of a
hang. The release smoke boots the GPT image under SeaBIOS and requires that
marker and exit status. The stub's disassembly is documented next to its bytes
in `make-release-image.py`, and the verifier requires it byte-for-byte.

The `apply-update` subcommand implements the update flow over those two
partitions: it writes a new loader/kernel pair into the primary ESP only,
requires the recovery partition to remain byte-identical (the previous
known-good version), and emits an update receipt recording the previous,
updated, and recovery digests. The release smoke proves both directions with
distinguishable versions (the current pair carries the catalog-policy
self-test marker, the previous pair does not): the updated image must boot the
new version from its primary without touching recovery, and corrupting the
updated primary loader must boot the preserved previous version through the
firmware fallback — an executable rollback receipt.

## Network: the link layer

aiueos sends and receives real Ethernet frames (ADR-0020). This is the **link
layer only** — a node cannot reach murakumo.cloud, or anything else, until
ARP/IPv4, TCP and TLS are built on top of it.

```sh
AIUEOS_TEST_NET=1 ./os/aiueos/scripts/smoke-qemu-uefi.sh
```

The driver is a modern-PCI virtio-net with two virtqueues (0 receive,
1 transmit), following the virtio-blk/rng pattern already in `kernel/pci.c`.
`prepare_queue` gained an index parameter: every device before this one used
exactly one queue, so it selected queue 0 by construction. The receive buffer is
posted *before* `DRIVER_OK`, so a reply cannot arrive with no buffer to land in.

The first packet aiueos ever sends is an **ARP request**, chosen because it is
the smallest exchange that proves a real peer answered — 14 bytes of Ethernet,
28 of ARP, no IP stack anywhere. Under QEMU's SLIRP the gateway 10.0.2.2
answers; no host network access is involved.

Whether the bytes that came back are the reply we asked for is a **decision**, so
it is a Kotoba object, not C: `kotoba/net-arp-reply-valid.kotoba` compiles to
`x86_64-aiueos-kernel-v1`, exports `kotoba_aiueos_net_arp_reply_valid`, and is
admitted by the same fail-closed verifier as every other object (`imports=0
relocations=1`). It checks every field of the reply rather than the opcode
alone — a device model that echoed our own broadcast back would pass an
opcode-only check, so admission requires opcode 2 (a reply, not the 1 we sent)
*and* sender IPv4 10.0.2.2.

A NIC is optional and stays optional. It is attached only under
`AIUEOS_TEST_NET=1`, so every other gate boots the machine it booted before, and
the kernel reports `AIUEOS_VIRTIO_NET_ABSENT no-nic-attached` rather than
failing when none is present. Measured both ways: a no-NIC boot is green with
`NET_ABSENT`, a NIC boot green with
`AIUEOS_VIRTIO_NET_OK modern-pci rx/tx arp-reply kotoba-admitted`.

The driver spins rather than taking an MSI-X interrupt, unlike blk and rng, and
specifically must not `hlt`: it claims no vector, so nothing would wake a
sleeper. Measured by building it — a variant that waited with `sti; hlt; cli`,
copied from the rng driver, wedges the boot immediately after
`AIUEOS_APIC_TIMER_OK`, and reverting to the spin reproduced the pass. A
completion that never arrives still fails the gate rather than parking the boot
forever.

## Network: IPv4

The ARP reply's MAC is now kept (ADR-0021), and aiueos sends an ICMP echo
request to 10.0.2.2 and admits the reply — a full IPv4 round trip, with both
checksums verified in Kotoba.

```sh
AIUEOS_TEST_NET=1 ./os/aiueos/scripts/smoke-qemu-uefi.sh
# AIUEOS_VIRTIO_NET_OK modern-pci rx/tx arp-reply kotoba-admitted
# AIUEOS_IPV4_OK icmp-echo-reply kotoba-admitted
```

ICMP echo is the smallest thing that exercises a full round trip — header
construction, both checksums, and a peer that has to route the datagram back —
without smuggling in anything above IPv4 to prove IPv4 works.

Two Kotoba objects carry the decisions: `ipv4-checksum.kotoba` (RFC 1071, as a
bounded tail recursion) and `ipv4-icmp-reply-valid.kotoba`, which admits a frame
only if it is IPv4 with IHL 5, protocol ICMP, **both** checksums verifying, from
the peer that was asked, type 0 (a reply — 8 is what was sent), and carrying the
identifier and sequence *this boot* used. That last check is the point: an echo
reply answering an earlier request is a real datagram and still not evidence
that this exchange completed.

A kernel memory base must **name** a region, not compute one. Passing
`(+ frame 14)` to a bounded load is rejected outright, so summing a sub-range
carries a base offset alongside the frame — which is what keeps a bounded load
bounded, since a computed base could put the 2048-byte trap bound anywhere.

Failure is not silent: with a NIC present and the link layer already OK, a
failed exchange prints `AIUEOS_IPV4_FAIL no-admitted-echo-reply`. It does not
fail the boot — whether a peer answers ICMP is a property of the network.

**What this is not.** No fragmentation or reassembly (IHL ≠ 5 is rejected, not
parsed). No routing table, no ARP expiry, no address configuration — 10.0.2.15
and 10.0.2.2 are SLIRP's fixed topology, written into the driver, and DHCP is
what would stop hardcoding them. No TCP, no TLS, no HTTPS: a node still cannot
reach murakumo.cloud.

## Network: TCP

One connection that completes (ADR-0022): three-way handshake, send a payload,
admit the peer's echo, close.

```sh
AIUEOS_TEST_NET=1 ./os/aiueos/scripts/smoke-qemu-uefi.sh
# AIUEOS_TCP_OK handshake echo close kotoba-admitted
```

The peer is QEMU's `guestfwd` piping the stream through `cat`, so a connection
to 10.0.2.100:9000 echoes. That is what makes the gate mean something: sequence
numbers, ACKs and both checksums all have to be right or nothing comes back, and
the OS cannot satisfy it by echoing itself. No external network is involved.

`tcp-segment-valid.kotoba` re-checks the IPv4 envelope, the IPv4 header
checksum, the TCP checksum, the source, the ACK field and the flags byte,
assuming nothing a caller might have checked. Flags are compared for **exact
equality, not masked** — a SYN-ACK and a SYN-ACK-PSH are different events, and
the caller has to say which one it expects. Failure names its stage
(`AIUEOS_TCP_FAIL <reason>`, six of them) because there are four admissions in a
row and a re-run costs many minutes on a loaded host.

**This is a probe, not a stack.** No retransmission timers, no congestion
control, no window management beyond a fixed advertised window, no out-of-order
reassembly, no multiple connections, no options. The receive ring holds one
buffer, so two segments arriving back to back drops the second and recovery
depends on the peer resending. The echoed bytes are never compared with what was
sent — the checksum proves the segment is intact, not that it carries our data.
HTTPS will need more than this.

## Network: address configuration

aiueos performs a real DHCPv4 exchange and records the lease (ADR-0076). DNS
and cloud-TCP then **send from that address** (ADR-0081). ARP/ICMP/guestfwd-TCP
keep using compiled-in `10.0.2.15` so the four-boot tamper gate stays a
demonstration.

```sh
AIUEOS_TEST_NET=1 ./os/aiueos/scripts/smoke-qemu-uefi.sh
# AIUEOS_DHCP_OK offer-ack kotoba-admitted address=10.0.2.15 mask=255.255.255.0 \
#   router=10.0.2.2 server=10.0.2.2 lease=86400
# AIUEOS_DHCP_CONSUMED src=10.0.2.15 dns=10.0.2.3
# AIUEOS_DNS_PROBE / AIUEOS_TCP_CLOUD_PROBE / AIUEOS_TLS_PROBE / AIUEOS_HTTP_PROBE
# AIUEOS_BARE_METAL_P2 leftover named from serial (ADR-0082)
```

QEMU's user-mode network carries its own DHCP server, so the guest broadcasts a
DISCOVER and something outside it answers — twice, since a REQUEST has to be
acknowledged before a lease exists. Nothing in the guest simulates a server.

**Two Kotoba objects carry the decisions**, and one of them is unlike every
admission below it. Every field the ARP, ICMP and TCP admissions read sits at a
constant offset; a DHCP reply ends in a chain of `(code, length, value)` records
that is variable-length, self-describing, terminated by a byte inside itself,
and written entirely by whoever answered. A walk that believes a length byte
reads past the end of the frame, which is the classic DHCP client defect. So
`dhcp-reply-valid.kotoba` proves at every step that a record's header *and its
whole value* lie below a limit derived from the IPv4 total length, and proves
the whole field parses **before** looking up any option — a field that does not
parse is reported as exactly that, not as whichever option the walk could not
reach. `dhcp-option-u32.kotoba` extracts one four-byte option, re-walking under
the same bound rather than being told where to look.

**It returns a reason code, not a boolean** — `0` admits, `1..12` name the
clause that refused. That is new for this ABI and is spelled out at the
kotoba-native entry, in `contracts/dhcp-reply-valid-v1.edn` and at the C call
site, because `if (validate(...))` admits exactly what it rejects. A boolean
would have made "the transaction id belongs to somebody else" and "an option
claims 255 bytes that are not there" the same event; they are the two ends of
what the object exists to tell apart.

**Both directions, with the broken thing being the reported thing.**

```sh
./os/aiueos/scripts/smoke-qemu-dhcp.sh
# four boots: one unmodified, three with the reply broken one way each
# AIUEOS_DHCP_SMOKE_OK admitted=1 refused=3 distinct-reasons=5,9,8
```

The tampering recomputes the UDP checksum afterwards. Without that every
tampered datagram would have been refused at the checksum instead, and all three
runs would have been red for a reason nobody chose.

**UDP appears here and this is not a UDP stack**: no socket, no port table, no
demultiplexer, no receive path for anything but this exchange. **And this is a
probe, not a client**: one exchange at boot and then nothing — no renewal
timers, no rebinding, no DECLINE, no RELEASE, no retransmission, one interface,
and a compile-time transaction id where a real client picks a random one.

**DNS and cloud-TCP consume the lease (ADR-0081).** ARP/ICMP/guestfwd-TCP still
send from compiled-in `10.0.2.15` so the four-boot tamper gate stays a
demonstration. Those two probes take their source from `aiueos_dhcp_address()`.
That is not a DNS resolver. TLS 1.3 + HTTP GET are ADR-0082 on this profile.
P2 is green when the guest serial prints `AIUEOS_HTTP_PROBE result=ok` with a CID
(measured 2026-08-22 on QEMU UEFI).

`murakumo-join-plan.kotoba` and `tcp-seq-acceptable.kotoba` are still written
and unlinked — the second because listing an export for an object no kernel
calls would reserve ABI for work not done (ADR-0074, ADR-0076). **Read a
`.kotoba` file's presence as a decision that was written, not one the kernel
runs**; `build-uefi.sh` names every object it links.

## USB removable-media boot

The GPT release image above is what gets written to a USB stick, but producing
a bootable image and being *reached the way a stick is reached* are different
claims, and only the second one makes a stick boot on someone else's machine.
A physical stick goes through the firmware's USB stack, reports itself as
removable, and is booted through the removable-media fallback path
`\EFI\BOOT\BOOTX64.EFI` — it has no NVRAM boot entry pointing at it. None of
that is exercised when the same image is attached as a fixed drive.

```sh
./os/aiueos/scripts/build-release-image.sh
nbb os/aiueos/scripts/smoke-qemu-usb-boot.cljs
nbb os/aiueos/scripts/flash-usb.cljs --device /dev/diskN            # inspect
nbb os/aiueos/scripts/flash-usb.cljs --device /dev/diskN --confirm /dev/diskN
```

`smoke-qemu-usb-boot.cljs` boots the **same image file** twice — once as a
fixed drive, once behind `qemu-xhci` as a `usb-storage` device with
`removable=on` — and requires the USB run to have booted through a USB device
path, the disk run not to have, both runs' aiueos evidence to be byte-identical,
and both to reach the same terminal state. The equality is the load-bearing
assertion: a USB run that merely also passed could still have diverged
silently, but identical evidence cannot. `AIUEOS_BOOT_TRANSPORT=usb` selects
the transport on `smoke-qemu-uefi.sh` directly if you want a single run.

Two things are deliberately excluded from that comparison, for reasons that are
themselves asserted. The firmware's pre-handoff banner *names the boot device*,
so it must differ — that difference is the positive proof, checked separately
(`PciRoot(0x0)/Pci(0x2,0x0)/USB(0x0,0x0)` on the USB run). And each application
processor writes a one-character liveness marker straight to the debug port
while the bootstrap processor writes its evidence lines, so those characters
land in a nondeterministic order relative to each other (`BAAIUEOS_…` on one
boot, `ABAIUEOS_…` on the next, same transport). That race belongs to SMP
bring-up, not to the boot medium, and the shared gate asserts the SMP evidence
itself; comparison therefore starts at each line's own `AIUEOS_` marker.

The gate does not hardcode a passing exit status, so it stays honest on a host
where the shared UEFI suite fails for an unrelated reason. On QEMU 10.0.3 the
suite fails at `AIUEOS_VIRTIO_INPUT_FAIL queue-or-envelope` — a virtio-input
device-model difference that has nothing to do with boot transport — and the
gate reports `AIUEOS_USB_BOOT_EQUIVALENT` with the shared status instead of
claiming a pass neither transport earned. When the suite passes, it reports
`AIUEOS_USB_BOOT_OK`.

`flash-usb.cljs` writes the image to a physical stick. Because that is
irreversible, it is deny-by-default: without `--confirm` it only inspects, and
`--confirm` must repeat the same device path so the destination is stated twice
independently. It refuses internal disks, non-removable devices, and partitions
(a GPT image written into a partition leaves the partition table at the wrong
offset and never boots), asks the OS whether a device is removable rather than
pattern-matching its name, requires the image to match its build receipt, and
re-reads the written bytes afterwards — cheap flash media can acknowledge a
write and store something else, which is invisible without readback. There is
deliberately no "find my USB stick" mode; the one time it guessed wrong it
would destroy a disk.

`contracts/usb-boot-v1.edn` carries these properties as checkable data.  Its
original USB image was proved under OVMF only; the subsequent K16 qualification
images have booted far enough on the physical machine to return bounded stage
codes, but a normal AIUEOS boot is still unproved.  Real-hardware firmware
quirks and the complete native runtime therefore remain qualification gates. The bare-metal
profile's network stack (ADR-0020..0087: virtio-net, DHCPv4, DNS, TCP,
TLS 1.3, HTTPS GET with CID verification) is proved under QEMU only — the one
link-layer driver is virtio-net-pci, so on a physical machine the node boots
on the offline floor (`AIUEOS_VIRTIO_NET_ABSENT`) until a physical-NIC driver
exists. ADR-0019's original "no network stack at all" was superseded by that
chain; see `contracts/usb-boot-v1.edn` `:gaps` for the current split.

The RTL8125 physical-link slice is in `kernel/rtl8125.c`. It is a
bounded **PXE handoff**, not a general Realtek driver: after UEFI has powered
and calibrated the PHY, it drains the firmware rings, installs one aligned TX
and one aligned RX descriptor, preserves the firmware PHY/MCU setup, masks
interrupts, and polls ownership with bounded callers. The host model exercises
the observed K16 MAC, an RTL8125B revision case, descriptor programming, TX
completion, RX FCS removal and rearming:

```sh
os/aiueos/scripts/smoke-rtl8125-handoff.sh
os/aiueos/scripts/smoke-micro-infer.sh
os/aiueos/scripts/smoke-job-protocol.sh
```

`AIUEOS_RTL8125_MODEL_OK` proves the register/descriptor state machine only.
The physical qualification build now selects the backend and stages progressively
stronger checks: an ARP round trip, a boot-nonce-bound UDP relay, authenticated
Murakumo enrollment through the Mac-held service token, and one claimed bounded
inference job.  The K16 computes the job with the frozen
`aiueos-char-bigram-v1` transition matrix, returns the boot- and job-bound result,
and accepts a commit only after the relay has persisted that result and posted a
ready heartbeat.  It then answers boot- and sequence-bound liveness pings; the
Mac relay renews the server-observed heartbeat only after each physical pong, so
the node cannot remain live merely because the Mac process is still running.
While live, the relay polls the queue with the K16 DID. K16-only jobs carry a
matching `target-did`; Murakumo hides them from generic pollers and rejects a
claim whose worker DID differs. It claims only an exact `aiueos-micro-infer`
job whose bounded prompt has a nonempty row in the frozen model. The same K16
executes each admitted job, and each result must be
persisted and committed before the worker accepts the next event.  Thus the
qualification job is the admission gate, not the only job the boot can run.
The result records a serialized TSC cycle delta around the native model and a
separate Mac-monotonic K16 relay round trip. The latter is rounded up to a
positive millisecond for Murakumo's run ledger; it is no longer written as the
placeholder `0`. Cycles are not converted to native wall time until the K16's
TSC frequency has been measured in the same physical boot.
Build and QEMU-negative-path coverage is available with:

```sh
os/aiueos/scripts/smoke-qemu-physical-job.sh
```

The relay accepts its DID and service token from owner-only regular files,
never from the EFI image or the LaunchAgent plist.  Its preflight authenticates
to the live queue without enrolling a node:

```sh
AIUEOS_PXE_BOOT=build/aiueos-physical-job-pxe/aiueos-k16-native-pxe.efi \
  os/aiueos/scripts/run-k16-murakumo-pxe.sh --preflight
```

After a clean physical image has been built and proved, the macOS user
LaunchAgent installer copies that exact receipt-bound image and relay into
Application Support and can restart it after login without an administrator
password.  It refuses dirty or receipt-mismatched images.  Preparing with
`AIUEOS_PXE_INSTALL_NO_LOAD=1` does not replace a currently running PXE server.

The exact scope and known answer are in
`contracts/micro-inference-qualification-v1.edn`.  This remains a physical
qualification path: AMD-IOMMU isolation is not in the RTL8125 path, and a fixed
character-bigram model is not a production LLM/GPU workload. The current
topology still terminates Murakumo HTTPS and holds the operator credential on
the Mac relay; target-bound queue routing is a direct logical dispatch to the
K16, not device-owned HTTPS/CACAO or a network-direct K16-to-Murakumo path.
None of these checks authorizes an internal-SSD write.

`verify-release-signature.py` verifies an RSA-2048 PKCS#1 v1.5 SHA-256
signature over the build receipt using only the Python standard library
(public-key operation only, fixed-work encoded-message comparison, RSA-2048
enforced), mirroring the in-kernel Kotoba RSA admission used for the
application catalog. The signing key never enters the repository or CI: real
release signatures are produced offline (`openssl dgst -sha256 -sign`), while
the release smoke proves the mechanism with an ephemeral key and requires a
tampered receipt to be rejected. Registering the production release key and
binding its policy into the boot receipt remain owner-side work.

The kernel owns a durable crash receipt: one bounded record in a dedicated
virtio-blk sector far above every aiuefs extent, carrying magic, version,
pending/consumed state, a reason code, the journal sequence at crash time, and
a Kotoba FNV checksum; writes and consumption both require readback. A
test-only synthetic panic (compile-gated, normal kernel context after the
storage plane is proven) persists the record and terminates deterministically;
the next boot consumes it, reports
`AIUEOS_CRASH_RECEIPT_OK reason=42 journal-context consumed readback`, and
must still pass the complete evidence gate. The journal-recovery smoke gates
both boots. Crash I/O takes no queue lock — it runs from the boot task before
user processes exist, and masking the APIC timer there was observed to block
the MSI-X wake.

Fault-context capture is owned by the unexpected-exception dispatcher. Any
exception arriving before the deliberate end-of-boot probe (armed by an
explicit flag, so a stray early `ud2` can no longer masquerade as the success
path) writes the same crash record through a dedicated fault transport:
try-lock only — if another context holds the block queue mid-operation the
receipt is skipped rather than corrupting queue state — and completion is
polled from the used ring with no interrupt dependence; the faulting kernel
terminates immediately afterwards. The journal-recovery smoke gates a
synthetic unexpected fault: the fault boot must show the polled receipt
written with readback, and the next boot must consume it, report
`AIUEOS_CRASH_RECEIPT_OK reason=6 fault-context consumed readback`, and still
pass the complete evidence gate.

The release also carries a `newc` initramfs (`EFI/AIUEOS/INITRD.IMG`,
deterministic bytes from `make-initramfs.py`: fixed mode/mtime, sequential
inodes) holding early-component and recovery materials — the signed user
application and its RSA signatures. Volume admission in the loader requires
both the kernel and the initramfs to match their compiled-in SHA-256 digests
(the recovery fallback admits both from the same volume), and the versioned
boot-info ABI moves to v2 to hand the archive's base and size to the kernel.
Before replacing the firmware page tables the kernel walks the archive with a
bounded `newc` parser — per-entry magic, hex-only size fields, 4-byte
alignment, in-bounds extents, at most 64 entries within 1 MiB, and the
TRAILER!!! terminator — and requires exactly the expected entry count. The
smoke gates the evidence marker and a corrupted-initramfs loader rejection;
the release verifier checks the archive byte-for-byte on every medium and the
receipt records its digest.

The carried recovery materials are proven usable, not just present: during the
same early walk the kernel copies the recovery application ELF and its RSA
signature into bounded kernel-owned buffers and admits them through the
identical Kotoba SHA-256 + RSA-2048 public-key policy used for object-store
application admission. A negative gate rebuilds the archive with a corrupted
recovery signature and a recomputed archive digest, so the loader admits the
archive and the kernel policy layer must be the one that rejects it —
defense in depth past the loader's whole-archive digest.

The recovery materials also drive an actual restore path. When an object-store
application fails digest or signature admission, the kernel restores it from
the initramfs — but only when the carried ELF hashes to the catalog entry's
digest and the carried signature verifies under the same RSA policy; the
catalog stays the authority over acceptable content, and its own corruption
remains fail-closed. Restored data and signature sectors are written through
virtio-blk with per-sector readback, re-admitted from the in-memory copy, and
reported as explicit restore evidence. The smoke's payload- and
signature-corruption gates now require the restore and the complete evidence
gate instead of a fatal stop.

Alongside the UEFI path, `os/aiueos/multiboot/` builds a Multiboot (v1) kernel
that boots through QEMU's own built-in Multiboot loader — no GRUB install and
no ESP:

```sh
./os/aiueos/scripts/build-multiboot.sh
./os/aiueos/scripts/smoke-qemu-multiboot.sh
```

QEMU enters the image in 32-bit protected mode per the Multiboot spec; a
trampoline (`multiboot/entry.S`) identity-maps the first GiB with 2 MiB pages,
enables PAE/LME/paging, far-jumps to long mode, and enables SSE (clearing
CR0.EM, setting CR0.MP, CR4.OSFXSR and CR4.OSXMMEXCPT) — the freestanding C is
compiled at `-O2`, which may vectorize byte loops into SSE instructions that
would `#UD` and, with no IDT on this path, triple-fault. It emits a byte on the
0xE9 debug port at each step so any hang localizes. The 64-bit landing verifies
the bootloader magic, walks the variable-stride Multiboot memory map (bounded,
requiring a usable region), discovers the ACPI RSDP the firmware-independent
way (there is no UEFI configuration table on this path: it scans the
0xE0000-0xFFFFF BIOS window for a signature- and 20-byte-checksum-valid Root
System Description Pointer — QEMU's built-in Multiboot loader still materializes
a genuine ACPI 1.0 RSDP there), walks the tables it references through the
kernel's validated ACPI parser (now handling both the ACPI 1.0 RSDT with
32-bit table pointers and the ACPI 2.0 XSDT with 64-bit pointers; the MADT
walk, CPU enumeration, and the >=2-CPU/IOAPIC invariant are shared), installs a
minimal IDT and brings up the Local APIC periodic timer through the shared
apic.c — waiting for a real vector-32 hardware tick (the trampoline identity-maps
the first 4 GiB with 1 GiB pages so the ~0xFEE00000 LAPIC MMIO is reachable) —
and runs the same compiler-emitted Kotoba probe
object the UEFI path admits before a deterministic QEMU exit. The linked
x86_64 image is wrapped verbatim in an ELFCLASS32/EM_386 container
(`wrap-multiboot32.py`) because QEMU's Multiboot loader requires a 32-bit ELF;
the machine code is unchanged and the image is byte-reproducible. This is a
narrow Multiboot slice: it deliberately does not stand up virtio, GOP, or the
rest of the evidence gate, which the UEFI path owns.

The same kernel also carries a Multiboot2 header, so GRUB boots it end to end:

```sh
./os/aiueos/scripts/build-grub-multiboot.sh
./os/aiueos/scripts/smoke-qemu-grub-multiboot.sh
```

`grub-mkrescue` (from `x86_64-elf-grub`/`xorriso`/`mtools` locally, or
`grub-pc-bin`/`grub-efi-amd64-bin` on CI) builds a GRUB rescue ISO whose
`grub.cfg` loads the kernel with the `multiboot2` command; OVMF boots the ISO,
GRUB enters the kernel in 32-bit protected mode with the MB2 magic, and the
64-bit landing walks the MB2 memory-map tag and runs the Kotoba probe. GRUB's
multiboot2 ELF loader wants section headers, so the GRUB path takes the linked
64-bit image directly while QEMU's built-in MB1 loader takes the 32-bit-wrapped
one; both carry the same MB1 and MB2 headers (8-byte aligned in the file) and
code. The GRUB path reaches the same evidence as the QEMU-direct MB1 path and then
some — it takes the ACPI RSDP from a Multiboot2 ACPI tag (no BIOS scan),
validates the tables through the shared ACPI parser, and brings up the Local
APIC timer, so both loader entries prove long mode, SSE, ACPI, and interrupt
handling before the Kotoba probe. Additionally, the kernel's Multiboot2 header
carries a framebuffer request tag, so GRUB (told to load its video backends and
set a graphics mode in `grub.cfg`) hands over a linear framebuffer; the kernel
validates its geometry, writes a bounded direct-RGB test pattern (24- or
32-bpp), and requires it to read back — a GOP-equivalent scanout surface the
QEMU-direct path cannot provide (no firmware framebuffer). The GRUB smoke adds
a `-device VGA` so OVMF's GOP has a real mode to offer.

This recovery selection lives in the reference C loader; re-expressing it in
the compiler-emitted C-free loader and a GRUB-driven Multiboot2 path remain
separate Phase 5 gaps.

Requirements are Zig 0.14 or newer and `qemu-system-x86_64` with an edk2/OVMF
firmware image. Override firmware discovery with `OVMF_CODE=/path/to/code.fd`.

The hard-flip path builds a separate kernel payload directly from Kotoba. It
invokes no C compiler or linker, admits privileged x86-64 intrinsics only for
the kernel target, reproduces the ELF byte-for-byte, rejects dynamic/foreign/C
artifacts, and emits a dependency receipt:

```sh
./os/aiueos/scripts/build-kotoba-native-kernel.sh /path/to/kotoba/compiler
./os/aiueos/scripts/smoke-qemu-kotoba-native.sh /path/to/kotoba/compiler
```

`build-kotoba-native-boot.sh` asks the Kotoba compiler to embed that ELF in a
position-independent PE32+ UEFI application. The compiler-generated loader
uses only AllocatePages, CopyMem, GetMemoryMap, and
ExitBootServices before entering the kernel. The hard-flip boot chain has no C,
CRT, foreign object, linker, interpreter, import table, or dynamic dependency.
Its final memory map resides in a compiler-owned bounded 16 KiB RW region.
The kernel derives that region from boot-info rather than trusting a loaded
pointer as authority, admits a contiguous five-page UEFI Conventional Memory
extent inside the identity-mapped physical window, derives five non-overlapping
authorities, and zeros all 4,096 bytes of each before use. The first page is a
boot-lifetime five-slot ownership bitmap; the next three are PML4, PDPT, and PD;
the fifth proves release and reuse. Kotoba writes and reads back a 512-entry
2 MiB-page identity map for the first GiB, loads its PML4 into CR3, reads CR3
back, invalidates one mapped address, and continues executing under that map.
The success path
itself rejects a duplicate claim and a double-free, then requires the released
slot to be claimed again before emitting `MPRC`. This is persistent across
allocator operations during one boot, not durable across reboot, and it is not
yet a general map-indexed allocator. The first-GiB map is deliberately RW and
executable, so this slice does not claim W^X. Real QEMU
mutations reject a complete map with no admitted extent, an overlapping second
authority, a claim implementation that accepts the wrong ownership state, a
non-present PML4 link, and a retained firmware CR3.

The scheduler maintains an eight-slot descriptor table; two services are live
in the boot evidence with stable IDs, generations, restart counts, and
heartbeats across preemption and CR3 switches. A compiler-emitted Kotoba
lifecycle planner now emits spawn, restart, and terminate actions. Those
actions allocate or release task slots and reconstruct a single generic task
entry from descriptor state; the C scheduler no longer selects `task_a` or
`task_b`. Boot also spawns and terminates a temporary third descriptor before
timer preemption. The QEMU smoke injects one deterministic failure and requires
the restarted service to become live again. A single-entry bounded mailbox then
transfers a scalar envelope from the restarted service to the other service
across distinct CR3 roots. Kotoba capability admission checks the sender owner
domain; a foreign-domain send is rejected. After scheduler convergence, a
compiler-emitted Kotoba serializer records both service IDs, generations, and
restart counts in the journal-first object-store transaction. The recovery
smoke clears the materialized object, boots again, requires redo before append,
and checks the exact registry bytes; latest-slot corruption must also restore
the prior registry and rewrite the alternate slot. On either recovery path the
validated registry states are decoded independently of the live scheduler,
the bootstrap service tasks are terminated, and Kotoba spawn actions recreate
their descriptors with the persisted generation and restart counts. Boot waits
for both restored heartbeats before starting ring-3 processes.

The EFI application is deliberately a small native bootstrap substrate. Kotoba
programs use the freestanding target contract in the separately versioned
`kotoba-lang/compiler` repository; moving
the remaining bootstrap into compiler-emitted PE/COFF is tracked by the ADR.

Kotoba user processes persist bounded object-store transactions without a
Linux host. Capability calls enqueue domain-owned writes, and the kernel task
commits them journal-first through native virtio-blk MSI-X. Domains 4 and 5 use
journal pairs 44/45 and 46/47, object sectors 42/43, readback receipts, and
boot-time highest-sequence replay.

This tree was first developed at `kotoba-lang/kotoba/os/aiueos` and imported
from commit `bfcf31458ecc51d8a3e7f5896a32e719885f984b`. That compatibility copy
is not deleted by this change; aiueos is authoritative from this import onward.
