<p align="center">
  <img src="docs/assets/header.png" alt="aiueos" width="480">
</p>

# aiueos

**An operating system.** Same category as Linux, Windows and macOS: it owns
firmware handoff, page tables, interrupts, scheduling, address spaces,
syscalls and device drivers, and it boots on real hardware architecture from
a signed image it builds itself. It is written in Kotoba, and the code that
makes decisions is compiled from Kotoba source rather than hand-written in C.

**It does not decide who may do what.** That moved to
[`kotoba-lang/grant`](https://github.com/kotoba-lang/grant) on 2026-08-21
(root ADR-2608219500). Aiueos asks; grant answers; aiueos enforces the answer.
Before that split this repository named an authority and a machine at once,
and the two were nearly the same size.

## Where it actually is

An operating system is a claim with a lot of surface, so here is the measured
state rather than the ambition. The production profile is **C-free bare
metal**: the loader and kernel are compiler-emitted, and an effect counts as
native only when its artifact receipt has empty `c_sources`,
`foreign_objects`, `imports` and `dynamic_dependencies` (ADR-0013).

| | Status |
|---|---|
| Firmware to ring-0 Kotoba | **working** — compiler-emitted PE32+ `BOOTX64.EFI` and ELF64 `KERNEL.ELF`, bounded segment admission, final UEFI memory map, `ExitBootServices`, CR3 and port-I/O evidence |
| CPU and memory | **partial** — a bounded six-page C-free slice: Kotoba-derived physical allocator, a 512-entry first-GiB identity map loaded into CR3, W^X and guard pages, a Kotoba-built vector-14 IDT and real CPU fault receipts (ADR-0039/0040). A general allocator, dynamic mapping, ACPI, APIC, IRQ dispatch and SMP are not C-free yet |
| Kernel execution | **not yet** — context switch, preemptive scheduler, ring 3, syscall entry/exit, capability handle table all still reference-profile only |
| Hardware | **not yet** — PCI, DMA, IOMMU, MSI-X, virtio, NVMe, USB HID are reference C with QEMU evidence, not compiler-emitted |
| Boot and release | **working** — deterministic GPT disk and El Torito ISO from one builder, byte-identical recovery ESP with proven firmware fallback, update and rollback receipts, RSA-2048 release-signature verification, durable crash receipts, initramfs, Multiboot2/GRUB |
| Desktop | **partial** — hosted WM (ADR-0085) stacks two `window-session-state` surfaces in the same DADS `#desktop`; raise changes z-order; `clojure -M:compositor wm`. Guest 2D create/flush is `clojure -M:compositor gpu` (ADR-0084). hosted IME romaji→kana is `clojure -M:compositor ime` (ADR-0086). hosted kanji (Space converts か→加) is `clojure -M:compositor kanji` (ADR-0088). hosted kami.webgpu presenter (`init!`/`draw!` on `#kami-viewport`) is `clojure -M:compositor kami` (ADR-0089). Guest IME is KERNEL.ELF Kotoba `k`+`a`→U+304B (`nbb --classpath src scripts/compositor-guest.cljs guest-ime`, ADR-0090). Guest WM is KERNEL.ELF Kotoba z-hit of two overlapping boot rects (`nbb --classpath src scripts/compositor-guest.cljs guest-wm`, ADR-0091). Guest paint is KERNEL.ELF filling those rects in z-order (`nbb --classpath src scripts/compositor-guest.cljs guest-paint`, ADR-0092). Guest input is KERNEL.ELF consuming a virtio-keyboard used-ring event (`nbb --classpath src scripts/compositor-guest.cljs guest-input`, ADR-0093). Guest gpu-two is KERNEL.ELF creating two virtio-gpu 2D resources when Kotoba admits n=2 (`nbb --classpath src scripts/compositor-guest.cljs guest-gpu-two`, ADR-0094). Guest scanout-two is KERNEL.ELF binding scanout 1 to resource 2 (`nbb --classpath src scripts/compositor-guest.cljs guest-scanout-two`, ADR-0095). Guest broker is KERNEL.ELF Kotoba clipboard admit / picker refuse (`nbb --classpath src scripts/compositor-guest.cljs guest-broker`, ADR-0096). Guest session restore is KERNEL.ELF Kotoba packed front 2 (`nbb --classpath src scripts/compositor-guest.cljs guest-session`, ADR-0098). Leftover `:native-compositor-absent` (native component runtime, P5). **P5 UNVERIFIED**. Not a finished Chrome OS-shaped desktop |
| Bare-metal net (P2) | **green on QEMU UEFI** — guest TLS 1.3 + HTTPS GET of empty raw CID with SHA-256 admit (ADR-0082). CertificateVerify ECDSA P-256 is `clojure -M:bare-metal cert-verify` (ADR-0087). Hosted `cloud-live` / session smoke / host curl do not count. Chain-to-anchor still leftover |

**Every gate above except P5's claim is QEMU/OVMF.** P5 real-machine boot is
**UNVERIFIED** (ADR-0084): this Mac is the QEMU host; attached USB is an
Ubuntu installer + data volume, not an aiueos image; USB OVMF is forbidden
as P5 (root ADR-2608221625 / ADR-0019). QEMU ≠ P5. Parity with Linux,
Windows or macOS is not claimed.

## What it is not

- **Not a desktop, not a phone, not an embedded target.** The only ISA with a
  bare-metal kernel is x86-64. The AArch64 artifacts under `resources/hvt/`
  are hypervisor-guest spikes, not a second port. There is no Android, iOS,
  Raspberry Pi or IoT profile, and nothing here claims one.
- **Not POSIX.** The first syscall ABI is capability-handle based. POSIX is an
  optional service, not the kernel authority (ADR-0013).
- **Not the authority.** `grant` decides admission, policy, signing, surfaces
  and profiles. What is here is the machine that carries out the decision --
  and the last place that could ignore it, which is why so much of it is still
  in the TCB inventory.

## The C boundary, stated as it is

`os/aiueos/kernel/` carries about 7,000 lines of C and assembly. That is not a
crt0 shim; it is a real mechanism kernel, and ADR-0015 says so rather than
repeating the older "minimal entry shim" story.

The reviewable property is **decision-free C**: C and assembly own registers,
MMIO, GDT/IDT, paging primitives, APIC/SMP bring-up, virtqueue plumbing and
context switch. Every *decision* -- SHA-256 and RSA-2048 verification,
ELF/catalog/journal admission, capability encode/admit/derive/revoke planning,
scheduler dispatch planning, pointer/length window admission, syscall-range
validation -- is compiler-emitted Kotoba. The C substrate contains no digest,
signature, admission or capability logic.

That reference kernel is an oracle and a porting specification. It is **not**
cumulative evidence that the C-free kernel implements the same mechanism;
only the C-free ledger in ADR-0013 is.

## Runtime dependencies

No Rust: the runtime crate and `bin/aiueos.rs` were retired and reimplemented
in CLJC, and `safe.rs` was dropped as redundant. There are zero `.rs` files
here.

No JVM **in what boots**: the C-free rule forbids libc, a CRT, a JVM, Linux
and any hosted supervisor in the bare-metal profile, and the artifact receipt
is what enforces it.

A JVM **in the hosted profiles and in the toolchain**: `aiueos.execute` hosts
compiled Kotoba Wasm through Chicory and is JVM-only, as is
`aiueos.launcher`. Fuel metering rides Chicory's `withUnsafeExecutionListener`
-- documented unsafe, experimental, interpreter-path-only -- so treat it as a
working prototype on an unofficial API rather than a guarantee. Memory limits
use a stable API.

## Position in the stack

```text
kotoba semantics -> amu -> kotoba-native freestanding ABI -> aiueos boot images
                                                     grant -> aiueos (decides)
                                                     aiueos -> kototama (executes)
```

`kototama` executes Components and `murakumo` places them in a fleet. Aiueos
supplies the named providers and the machine underneath them; it does not own
another system's compiler or fleet scheduler, and since the split it does not
own the grant vocabulary either.

## Native dependency and evidence boundary

The production dependency is deliberately one-way:

```text
Kotoba semantics
  -> kotoba-native instruction selection and freestanding ABI
  -> Amu compiler/package emission and qualification
  -> aiueos loader, kernel, effects, and machine evidence
```

`kotoba-native` does not own an OS provider, Amu does not own process or
hardware isolation, and aiueos does not redefine language or instruction
semantics. An effect is production-native only when the same exact-pinned
chain reaches the compiler-emitted `BOOTX64.EFI` and `KERNEL.ELF`, carries an
empty foreign-code receipt, and passes positive plus fail-closed QEMU evidence.
Hosted `aiueos.vm`, experimental `aiueos.hvt`, and the older C/assembly kernel
are useful oracles, but none qualify the C-free bare-metal profile.

The ownership decision and gap ledger are
[`ADR-0013`](90-docs/adr/0013-native-os-ownership-and-boot.md); the current
C-free W^X/guard and CPU-fault evidence is
[`ADR-0039`](90-docs/adr/0039-c-free-page-fault-hardware-receipts.md), with
bounded recovery in
[`ADR-0040`](90-docs/adr/0040-c-free-recoverable-page-fault-frame.md). Amu
records the consumer/compiler boundary in ADR-0240; the root handoff is
ADR-2608110400.

## Reference stack topology and C mechanism boundary

aiueos is the stack's **capability broker**: dependency-minimal by invariant
(deps.edn carries `security` + Chicory only; enforcement layers like
`kototama` import aiueos, never the reverse), consuming the Kotoba compiler
as **verified artifacts** (freestanding ELF objects), not as a library. The
historical bare-metal split — C/asm owns mechanism only, every decision (crypto
verification, admission, capability planning, dispatch planning) is
compiler-emitted Kotoba — is stated honestly, with measured line counts, in
[`90-docs/adr/0015-stack-topology-and-honest-c-boundary.md`](90-docs/adr/0015-stack-topology-and-honest-c-boundary.md)
(root authority: `com-junkawasaki/root` ADR-2607241100).

## What is here

The decision namespaces that used to be listed under this heading moved to
[`kotoba-lang/grant`](https://github.com/kotoba-lang/grant) on 2026-08-21 --
`contract`, `graph`, `policy`, `authority`, `surface`, `manifest`, `signing`,
`audit`, `broker`, `cli`, `decide` and fourteen more. They are reached here as
`grant.*` and pinned by git SHA in `deps.edn`; grant's README is where they are
documented now.

`aiueos.topic` stayed: the in-process pub/sub bus is a substrate, not a
decision. Which topics a component may publish to is decided in
`grant.manifest`; carrying the messages is this repository's job.

What is left here executes, boots and drives hardware.


- `src/aiueos/execute.cljc` **actually executes** a compiled `.kotoba` Wasm
  component (ADR-2607022900), via [Chicory](https://github.com/dylibso/chicory).
  Verifies through `grant.broker/verify-one` first and refuses to run anything
  denied; the 7 non-hardware kernel capabilities (`log-write`/`clock-monotonic`/
  `random-bytes`/`topic-*`) get real Clojure-backed host functions, the
  device-access quartet (`pci-config`/`dma-map`/`irq-subscribe`/`mmio-map`) stays
  a deterministic stub pending real hardware access (native shim or
  `java.lang.foreign`, unresolved). Enforces `:aiueos/quota {:host-calls N
  :publishes N}` (ADR-0006) — a per-run host-function call-count cap; exceeding
  it aborts the run mid-execution (`:aiueos.execute/quota-exceeded`, offending
  call's own effect never lands). Also enforces `:aiueos/limits :fuel`
  (ADR-0001) — **real instruction-level metering**, via Chicory's
  `Instance.Builder/withUnsafeExecutionListener` (fires per Wasm instruction
  executed). Chicory has no first-class gas-metering API, and this hook is
  explicitly documented `unsafe`/`experimental`/possibly removed later (its
  supported execution-limit mechanism is a wall-clock thread-interrupt timeout,
  not this) — treat fuel enforcement as a working prototype on an unofficial
  API, not a permanent guarantee. It also only fires in Chicory's interpreter
  path; a future switch to Chicory's AOT compiler would bypass it entirely.
  Also enforces `:aiueos/limits :memory-pages` (ADR-0001) — via a **stable**
  Chicory API (`Instance.Builder/withMemoryLimits`, not marked
  unsafe/experimental like the fuel listener). Reads the module's own declared
  initial page count (never overridden) and caps only the maximum
  `memory.grow` can reach; unlike quota/fuel/topic-forbidden, this does NOT
  abort the run — `memory.grow` past the cap returns Wasm's own `-1` failure
  sentinel to the guest. Also enforces `:aiueos/publishes`/`:aiueos/subscribes`
  (the topic-id allow-set `grant.manifest/normalize` derives) — a granted
  component's `topic_publish`/`topic_poll`/`topic_take`/`topic_count` calls are
  restricted to its declared topic ids (`nil` = unrestricted). Every result
  also carries an ADDITIVE `:aiueos/run-receipt` (`grant.broker/run-receipt`,
  ADR-2607022900 follow-up 8): `:succeeded`/`:failed`/`:denied` status,
  `:started-at`/`:finished-at` (epoch ms), and the same audit events.
  **JVM-only** — needs `clojure -M:test` (Chicory was never in babashka's class
  allowlist, and babashka has since been retired outright).
- `src/aiueos/launcher.cljc` is a real, runnable CLI: the retired Rust
  `bin/aiueos.rs`'s argv-parsing/file-I/O role, reimplemented as JVM Clojure.
  Ties `grant.cli` + `grant.manifest` + `grant.policy`/`grant.broker` +
  `aiueos.execute`/`aiueos.audit` together. `verify`/`run`/`admit`/`inspect`/
  `surface`/`audit`/`up` are wired today (`run`/`admit` actually execute a
  granted component's declared `:aiueos/wasm`, not just decide; `up` boots the
  components due at a given ADR-0006 cycle (`--cycle N`, default 0) in
  `grant.graph/priority-boot-order`, stopping at the first
  denied/quota-or-fuel-exceeded DUE component). Try it: `clojure -M -m
  aiueos.launcher up <system>.edn --cycle 3 --edn`. **`:aiueos/schedule`'s
  `:deadline-cycles` is NOT enforced** — see `grant.manifest/due-this-cycle?`'s
  docstring for why. **JVM-only**, same reason as `aiueos.execute`. Not wired:
  four adapter-only commands (`sign`/`check`/`compile`/`hash`). `image` and
  `vm` are wired for the experimental Linux-hosted PID-1 profile (ADR-0011).
- The contract EDN under `resources/aiueos/` moved to grant with the code that
  reads it. The keys did not change: `:aiueos.policy/*`, `:aiueos.broker/*` and
  `:aiueos/*` are on the wire, read by `kototama`'s adapter and by stored
  decisions, so only the namespaces were renamed. They still load from the
  same classpath paths, now shipped by the grant dependency.
- `test/aiueos/*_test.cljc` checks every CLJC validator/reasoner/contract above.

The one exception worth naming on the language side: the retired `safe.rs`
(safe-kotoba subset gate) was NOT ported because it's redundant — that check
already lives in `kotoba-lang/kotoba`'s `kototama`/`kotoba-clj` layer.

## Examples / deployment docs

- `examples/**/*.edn` and `examples/**/*.clj` — runnable manifests/policies
  across the robot, browser, computer-use, driver, and signed-component
  surfaces.
- `docs/deployment-profiles.md`, `docs/incident-exercises/`, `docs/issues/` —
  deployment-profile-specific security claims and IR drill writeups.
- `90-docs/adr/` — architecture decisions for the capability OS and its
  surfaces.

## Verify

```bash
clojure -M:test   # full suite, including aiueos.execute-test (Chicory, JVM-only)
./os/aiueos/scripts/smoke-qemu-journal-recovery.sh # no-Linux OVMF gate
```

`scripts/tasks.edn` additionally registers the boot/flash gates:
`multiboot-build`, `multiboot-smoke`, `grub-multiboot-smoke`, `usb-boot-smoke`,
`usb-flash` — run through `nbb scripts/run-task.cljs <task>`.

**Two entrypoints this README used to document are unavailable.** babashka was
retired as this workspace's script host by ADR-2607173000, and both bodies were
babashka-hosted `(require …)` + `run-tests` / subprocess forms that the
conversion could not express, so they were dropped (ADR-2608131600). The
recovered forms are in `scripts/tasks-complex.edn`.

- **`bb test:cljc`** — the pure CLJC authority contract tests, i.e. everything
  except `aiueos.execute-test`. `clojure -M:test` above still runs those
  assertions; what is gone is the ability to run them *without* the JVM.
- **`bb decide`** — the decision subprocess described above. `aiueos.decide` and
  `grant.cli` are unchanged, so a host adapter can still reach the same
  contract, but there is no packaged task entrypoint for it today.

See `90-docs/adr/0013-native-os-ownership-and-boot.md` for the repository
boundary and `os/aiueos/README.md` for native build requirements.

## Linux-hosted VM bundle

The VM image needs a Linux JRE and its ELF loader/shared libraries; a macOS
`jlink` runtime cannot run in the guest. Build the cross-host bundle with:

```bash
scripts/build-linux-bundle.sh aarch64   # or x86_64
```

Then provide a matching Linux kernel and system graph:

```bash
AIUEOS_ARCH=aarch64 \
AIUEOS_KERNEL=/path/to/Linux/Image \
AIUEOS_SYSTEM=/path/to/system.aiueos.edn \
scripts/vm-smoke.sh
```

The smoke image embeds `/jre`, `/aiueos.jar`, the Linux dynamic loader/libs,
and an argfile-based `/init`. PID 1 prints `AIUEOS_BOOT_OK` after the component
graph starts and powers the disposable VM off. `image build` rejects missing
JRE/JAR/runtime-root inputs and rejects a non-ELF guest Java executable.

This is the ADR-0011 Linux-hosted profile, not the bare-metal kernel described
by the product integration ADR in `kotoba-lang/kotoba`.


## Hosted daily shell (P1)

Root contract: [`adr-2608221625-aiueos-chromeos-cloud-desktop`](https://github.com/com-junkawasaki/root/blob/main/90-docs/adr/2608221625-aiueos-chromeos-cloud-desktop.edn).
This is the JVM hosted profile. It is **not** the bare-metal compositor, and
`clojure -M:cloud-live check` does **not** green this gate.

```bash
clojure -M:session smoke
```

Expected markers:

- `AIUEOS_SESSION_URL=http://127.0.0.1:<port>/#session`
- `AIUEOS_SESSION_SPA=admitted`
- `AIUEOS_SESSION_KOTOBASE=` … `"outcome":"admitted"` on a real `kotobase.net` GET
- `AIUEOS_SESSION_INFER=` … `"alias":"murakumo-main"` and a completion snippet
- `AIUEOS_SESSION_OK`

The OS UI engine contract is `kotoba-lang/browser`; DADS is the component and
token layer inside that surface. The committed HTML/JavaScript document is a
hosted verification adapter and is explicitly not counted as the native
Kotoba-clj/WASM browser guest.

Exit 0 means the DADS SPA was served and both live legs were admitted **from
the session process**. Exit 1 is a refusal or a non-DADS document. Exit 3
means a leg could not be answered.

```bash
clojure -M:session serve   # open the printed URL on a phone-sized viewport
```

## Mac VM phone-bind (P1b / P1c proving slice)

Root contract: [`adr-2608221625-aiueos-chromeos-cloud-desktop`](https://github.com/com-junkawasaki/root/blob/main/90-docs/adr/2608221625-aiueos-chromeos-cloud-desktop.edn).
This slice does **not** claim a compositor, bare-metal TLS, itonami, or
real-machine qualification.

A VM has no chassis sticker. The hypervisor helper on the Mac prints the
setup URL and QR payload on the **host** terminal and writes `setup.json`
next to the VM. QEMU runs with `-display none`; guest VGA/keyboard is not a
passing path. User-mode/slirp stands in for Ethernet DHCP. Enrollment is
The hosted fixture uses `grant.enroll` (not a second identity stack). The
product account flow sends `Passkey` or `phone-scan` into one single-use
challenge and then proves the device-owned key. The scanned payload contains
neither the device enrollment token nor an account/passkey private key. The local check-in ledger is
labelled `non-authoritative`; production still names `https://kotobase.net`.

On Apple Silicon this uses `qemu-system-aarch64` + HVF + edk2 firmware, the
same ISA `aiueos.vm` defaults to. It is **not** the x86_64 C-free kernel
gate.

```bash
# from this repository (worktree or clone)
clojure -M:phone-bind smoke
```

Expected markers on stdout:

- `AIUEOS_SETUP_URL=http://127.0.0.1:<port>/#setup`
- `AIUEOS_QR=aiueos:2;did=...;model=aiueos-qemu-hosted;endpoint=...;auth=passkey,phone-scan;claim-secret=none`
- `AIUEOS_BIND_OK`

Exit 0 means an unbound headless VM was bound by a simulated **phone HTTP**
client (no guest keyboard), a bind receipt was written, and a QMP power
cycle left the device claimed. Exit 1 is a refusal. Exit 3 means QEMU or
firmware could not be answered (not a pass).

```bash
clojure -M:phone-bind pre-enroll   # P1c: grant in the image, zero QR, copy refused
clojure -M:phone-bind serve        # leave the phone SPA up; open the printed URL
```

The SPA is the DADS document at `apps/session` (fragments `#session` `#desktop` `#setup`
`#manage` `#devices`). Phone-bind serves that one HTML. `clojure -M:session smoke`
is P1 (kotobase + murakumo from the session process). `clojure -M:test` of
unrelated suites is **not** this gate.

The complete onboarding boundary is
`os/aiueos/contracts/device-onboarding-v1.edn`. Account sync, Murakumo
readiness, Kekkai reachability, and Kotobase/CARv2 storage replication are
independent gates. The current contract neither authorizes an internal SSD
write nor claims that the native Kekkai or storage adapter already exists.

## Desktop / compositor (hosted WM + guest 2D argv + kami.webgpu presenter)

Root contract: compositor unit of [`adr-2608221625`](https://github.com/com-junkawasaki/root/blob/main/90-docs/adr/2608221625-aiueos-chromeos-cloud-desktop.edn). HTTP to `#session` with `-display none` and no compositor process is **red**. QMP `query-pci` is **not** guest 2D. A single notes iframe is **not** a window manager.

The same `apps/session` DADS SPA is the shell. A compositor process owns `window-session-state` surfaces, persists them in `state/desktop.edn`, and restores after kill/relaunch. A wiped file is refused (`empty-desktop`), not an empty success. Hosted WM (ADR-0085) stacks two overlapping surfaces with DADS title bars; `raise` changes z-order; pointer hit-test is front-to-back. QEMU for `smoke` is started with `-device virtio-gpu-pci` and still `-display none` so P1b phone bind needs no local keyboard.

```bash
clojure -M:compositor smoke   # hosted SPA + surfaces + PCI listing
clojure -M:compositor gpu     # KERNEL.ELF CREATE+FLUSH (not PCI listing)
clojure -M:compositor wm      # hosted WM: ≥2 surfaces, z-order, DADS, input routing
clojure -M:compositor ime     # hosted IME: ka→か, off-path latin leak is red
clojure -M:compositor kanji   # hosted IME: Space converts か→加; kana-only Space is red
clojure -M:compositor kami    # hosted kami.webgpu init!/draw!; sky-clear is red
nbb --classpath src scripts/compositor-guest.cljs guest-ime  # KERNEL.ELF Kotoba k+a→U+304B
nbb --classpath src scripts/compositor-guest.cljs guest-wm   # KERNEL.ELF Kotoba z-hit of two overlapping rects
nbb --classpath src scripts/compositor-guest.cljs guest-paint # KERNEL.ELF paints both rects in Kotoba z-order
nbb --classpath src scripts/compositor-guest.cljs guest-input # KERNEL.ELF consumes a virtio-keyboard used-ring event
nbb --classpath src scripts/compositor-guest.cljs guest-gpu-two # KERNEL.ELF two virtio-gpu 2D resources when Kotoba n=2
nbb --classpath src scripts/compositor-guest.cljs guest-scanout-two # KERNEL.ELF scanout 1 → resource 2 when Kotoba n=2
nbb --classpath src scripts/compositor-guest.cljs guest-broker # KERNEL.ELF Kotoba clipboard-only broker admit
nbb --classpath src scripts/compositor-guest.cljs guest-session # KERNEL.ELF packed front 2 restore
```

Expected `smoke` markers:

- `AIUEOS_COMPOSITOR_URL=http://127.0.0.1:<port>/#desktop`
- `AIUEOS_COMPOSITOR_SPA=admitted`
- `AIUEOS_COMPOSITOR_SURFACES=admitted`
- `AIUEOS_COMPOSITOR_RESTORE=admitted`
- `AIUEOS_COMPOSITOR_WIPE=refused-as-required`
- `AIUEOS_COMPOSITOR_DISPLAY=none`
- `AIUEOS_COMPOSITOR_GPU=virtio-gpu-pci`
- `AIUEOS_COMPOSITOR_OK`

`smoke` exit 0 means the SPA was served, surfaces restored, wipe is red, and QMP `query-pci` named virtio-gpu. That is **not** 2D.

`gpu` exit 0 means guest serial has `AIUEOS_VIRTIO_GPU_CREATE result=ok` and `AIUEOS_VIRTIO_GPU_FLUSH result=ok` (ADR-0084). GET_DISPLAY_INFO without those lines is leftover `:gpu-2d-create-flush-absent`. Exit 1 is a refusal. Exit 3 means QEMU/firmware/serial could not be answered.

`wm` exit 0 means two surfaces stack, one-surface is red, raise changes the front, overlap hit ≠ map key order, DADS title bars are in the SPA, and pointer routing names the focused guest (ADR-0085). IME is not required for `wm`.

`ime` exit 0 means IME-on consumes `ka` (no latin to the guest), Enter commits `か`, and IME-off delivers `ka` (ADR-0086 named red). Hosted leftover after guest IME is `:native-compositor-absent`.

`kanji` exit 0 means Space converts `か` to `加` without delivering to the guest, Enter commits `加`, and Space that commits kana is red (`kana-only-desktop`, ADR-0088). That is **not** a finished desktop.

`kami` exit 0 means the SPA calls `kami.webgpu/init!` then `draw!` on `#kami-viewport` with a `render-ir` of ≥1 instance (ADR-0089). A sky-only `beginRenderPass` clear is leftover `:clear-only-desktop`. Exit 3 means the browser could not be answered. Native compositor leftover remains. That is **not** a finished desktop.

`guest-ime` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_IME_OK committed=u+304b latin-leak=0` from Kotoba `kotoba_aiueos_ime_commit` (ADR-0090). Hosted `clojure -M:compositor ime` / `AIUEOS_COMPOSITOR_IME_OK` is red. virtio-input is still synthetic. Leftover `:native-compositor-absent`. That is **not** a finished desktop.

`guest-wm` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_WM_OK two-surfaces z-hit=2 miss-front=1 raise=1 one-surface=0` from Kotoba `kotoba_aiueos_wm_hit` (ADR-0091). Hosted `clojure -M:compositor wm` / `AIUEOS_COMPOSITOR_WM_OK` is red. virtio-input synthetic remains. Leftover `:native-compositor-absent`. That is **not** a finished desktop.

`guest-paint` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_PAINT_OK boot-overlap=2 raised-overlap=1 key-order=0` from painting both boot rects in Kotoba z-order and sampling the overlap pixel (ADR-0092). Hosted `clojure -M:compositor wm` is red. A key-order paint is leftover `:key-order-paint`. Default gpu/guest-paint boots still use synthetic input. Leftover `:native-compositor-absent`. That is **not** a finished desktop.

`guest-input` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_INPUT_OK eventq-used=1 synthetic=0` from a virtio-keyboard used-ring event (ADR-0093). Hosted `clojure -M:compositor wm` is red. C filling keycode 30 is leftover `:synthetic-smoke`. HMP `sendkey` is not this gate. QMP inject is not a laptop HID and not P5. Leftover `:native-compositor-absent` (permission broker, native component runtime, one virtio-gpu scanout). That is **not** a finished desktop.

`guest-gpu-two` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_GPU_TWO_OK resources=2 flush=2 kotoba-n=2` from Kotoba-admitted count and two CREATE/FLUSH paths (ADR-0094). Hosted `clojure -M:compositor wm` is red. C hardcoding resource count is leftover `:one-resource`. Leftover `:native-compositor-absent`. That is **not** a finished desktop.

`guest-scanout-two` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_SCANOUT_TWO_OK scanouts=2 resource-0=1 resource-1=2 kotoba-n=2` from Kotoba-admitted bind count and SET_SCANOUT on scanout 1 (ADR-0095). Hosted `clojure -M:compositor wm` is red. One scanout when Kotoba admits two is leftover `:one-scanout`. Leftover `:native-compositor-absent`. That is **not** a finished desktop.

`guest-broker` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_BROKER_OK clipboard=1 picker=0 kotoba-clip=1 kotoba-pick=0` from Kotoba `kotoba_aiueos_broker_admit` (ADR-0096). Hosted `clojure -M:compositor wm` is red. Picker admitted on a clipboard-only grant is leftover `:always-grant`. Leftover `:native-compositor-absent`. That is **not** a finished desktop.

`guest-session` exit 0 means KERNEL.ELF serial has `AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2` from Kotoba `kotoba_aiueos_session_restore` (ADR-0098). Hosted `clojure -M:compositor wm` is red. Restore that always returns 2 is leftover `:always-front`. Leftover `:native-compositor-absent` (native component runtime, P5). That is **not** a finished desktop.

```bash
clojure -M:compositor serve   # same SPA; compositor owns surfaces; Ctrl-C to stop
```

`clojure -M:phone-bind smoke` stays headless **without** the GPU device. Display-present (動線 D) is extra, not the only bind path. Native compositor remains leftover. P5 remains UNVERIFIED. kami-engine as the daily desktop, CACAO write, and physical boot remain. The Chrome OS-shaped desktop goal is not complete.


## Bare-metal cloud reach (P2) — green on QEMU UEFI

Root contract: P2 of [`adr-2608221625`](https://github.com/com-junkawasaki/root/blob/main/90-docs/adr/2608221625-aiueos-chromeos-cloud-desktop.edn). This is QEMU **UEFI + KERNEL.ELF**, not the hosted JVM profile.

```bash
clojure -M:bare-metal cloud
```

The guest consumes its DHCP lease, resolves `kotobase.net`, completes TLS 1.3 (cipher 0x1301), GET `/ipfs/<empty-raw-cid>`, and admits the body SHA-256 (ADR-0082). **Exit 0 is guest HTTP GET + CID verify.** Handshake without HTTP is leftover `:http-absent`. A TLS record without Finished is `:tls-handshake-incomplete`. CertificateVerify (ECDSA P-256 against the leaf) is a separate gate: `clojure -M:bare-metal cert-verify` (ADR-0087). HTTP+CID without that serial line is leftover `:cert-verify-hashed-only`. Chain to a trust anchor is still leftover.

`clojure -M:cloud-live check` and `clojure -M:session smoke` do **not** green this gate. A Mac-side fetch is `:host-fetch-does-not-count`.


## Grant-limited guest in the shell (P3)

Root contract: P3 of [`adr-2608221625`](https://github.com/com-junkawasaki/root/blob/main/90-docs/adr/2608221625-aiueos-chromeos-cloud-desktop.edn). Same `apps/session` DADS SPA. `:app/notes` runs through `grant` + Chicory Wasm (`examples/apps/notes.wat`). A deny is HTTP 403 with `:unresolved-capability`, not a generic 500. POSIX `:fs/open` is not the store; kotobase write without a credential is `:write-unauthorized`.

```bash
clojure -M:session guest
```

Expected markers:

- `AIUEOS_GUEST_URL=http://127.0.0.1:<port>/#session`
- `AIUEOS_GUEST_SPA=admitted`
- `AIUEOS_GUEST_DENY=` … `"reason":"unresolved-capability"` and HTTP 403
- `AIUEOS_GUEST_ALLOW=` … `"decision":"grant"`, `"visible":true`, `"component":"app/notes"`, log `hi`
- `AIUEOS_GUEST_ALLOW_LIST=` lists the guest under `guests`
- `AIUEOS_GUEST_OK`

Exit 0 means the SPA listed the guest, grant allow ran it, and grant deny was the named red. This is **not** the full Chrome OS-shaped desktop: P2 guest HTTPS to kotobase is green on QEMU; CertificateVerify is green on QEMU (ADR-0087); P4 itonami is green on hosted JVM; P5 a real machine is UNVERIFIED; kanji and CACAO write remain. Guest virtio-gpu 2D is `clojure -M:compositor gpu` (ADR-0084), not this guest-in-shell gate.

## Maturity

Tracked M0-M6 in `docs/coverage.edn` (template borrowed from
`kotoba-lang/kotoba-lang`'s `docs/lang/coverage.edn`).

Contract maturity and native-product maturity are separate. The C-free
production chain currently boots through UEFI, validates a bounded final
memory map, and has its first physical allocator slice; paging, interrupts,
scheduling, CPL3/syscalls, capability tables, and native effect providers
remain subsequent aiueos gates. The detailed, test-backed truth is the Phase
table in ADR-0013, not the amount of reference C code present in this tree.
