# Changelog

All notable changes to **aiueos** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/); this crate is pre-1.0 (Phase-0).

## [Unreleased]

### nbb guest compositor gates (ADR-0100)
- Guest KERNEL.ELF serial gates run on nbb:
  `nbb --classpath src scripts/compositor-guest.cljs <profile>`.
  Classifiers live in portable `aiueos.compositor.guest`. Hosted JVM
  `clojure -M:compositor wm` / `ime` stay red. JVM `clojure -M:compositor
  guest-*` is leftover `:jvm-gate-runner`. Serial lines unchanged.
  Leftover `:native-compositor-absent` (native component runtime, P5).
  **P5 UNVERIFIED**.

### Guest session restore (ADR-0098)
- KERNEL.ELF restores packed front window 2 when Kotoba
  `kotoba_aiueos_session_restore(2) == 2`, refuses packed 0 and packed 3,
  and `kotoba_aiueos_wm_hit` uses that front. Restore is Kotoba; C does
  not hardcode front. Gate host as of ADR-0100:
  `nbb --classpath src scripts/compositor-guest.cljs guest-session`.
  Named red is hosted JVM `AIUEOS_COMPOSITOR_WM_OK` and restore that
  always returns 2 (`:always-front`). Default `gpu` / `guest-broker`
  boots stay green without requiring `GUEST_SESSION_OK`. Leftover
  `:native-compositor-absent` (native component runtime, P5).
  **P5 UNVERIFIED**.

### Guest permission broker (ADR-0096)
- KERNEL.ELF admits clipboard and refuses file-picker when Kotoba
  `kotoba_aiueos_broker_admit(1, 1) == 1` and
  `kotoba_aiueos_broker_admit(2, 1) == 0`. Admit is Kotoba; C copies
  the clipboard scratch only when admitted. Gate:
  `clojure -M:compositor guest-broker`. Named red is hosted JVM
  `AIUEOS_COMPOSITOR_WM_OK` and picker on a clipboard-only grant
  (`:always-grant`). Default `gpu` / `guest-scanout-two` boots stay
  green without requiring `GUEST_BROKER_OK`. Leftover
  `:native-compositor-absent` (native component runtime, P5).
  **P5 UNVERIFIED**.

### Guest scanout-two (ADR-0095)
- KERNEL.ELF `SET_SCANOUT` scanout 1 onto resource 2 when Kotoba
  `kotoba_aiueos_scanout_bind(2, enabled) == 2`. Bind count is Kotoba;
  C does not hardcode `2`. QEMU `virtio-vga` uses `max_outputs=2`.
  Gate: `clojure -M:compositor guest-scanout-two`. Named red is hosted
  JVM `AIUEOS_COMPOSITOR_WM_OK` and one scanout when Kotoba admits two
  (`:one-scanout`). QEMU 10.1 enables extra heads only after a UI
  frontend `ui_info`; the gate owns a unix session bus and `gdbus`
  `SetUIInfo` on Console_1. Default `gpu` / `guest-gpu-two` boots stay
  `-display none` and stay green without requiring
  `GUEST_SCANOUT_TWO_OK`. Leftover
  `:native-compositor-absent` (native component runtime, P5). **P5 UNVERIFIED**.

### Guest gpu-two (ADR-0094)
- KERNEL.ELF creates and flushes two virtio-gpu 2D resources when Kotoba
  `kotoba_aiueos_wm_hit(2, 2, 100, 80) == 2`. Count is Kotoba; C does not
  hardcode `2`. Gate: `clojure -M:compositor guest-gpu-two`. Named red is
  hosted JVM `AIUEOS_COMPOSITOR_WM_OK` and one resource when Kotoba admits
  two (`:one-resource`). Default `gpu` / `guest-input` / `guest-paint` boots
  stay green without requiring `GUEST_GPU_TWO_OK`. Leftover
  `:native-compositor-absent` (permission broker, native component runtime,
  one virtio-gpu scanout). **Measured 2026-08-24:**
  `guest-gpu-two` leftover `[]` with
  `AIUEOS_GUEST_GPU_TWO_OK resources=2 flush=2 kotoba-n=2`. **P5 UNVERIFIED**.

### Guest input (ADR-0093)
- KERNEL.ELF copies the desktop envelope from a virtio-keyboard
  used-ring event, not the `#ifdef AIUEOS_INPUT_SMOKE_SYNTHETIC` fill.
  Gate: `clojure -M:compositor guest-input`. Named red is hosted JVM
  `AIUEOS_COMPOSITOR_WM_OK` and C synthetic fill (`:synthetic-smoke`).
  Default `gpu` / `guest-ime` / `guest-wm` / `guest-paint` boots keep
  the synthetic ifdef so they stay green without this serial line.
  Injector is QMP `input-send-event`, not HMP `sendkey`. Leftover
  `:native-compositor-absent` (permission broker, native component
  runtime, one virtio-gpu resource). **Measured 2026-08-23:**
  `guest-input` leftover `[]` with
  `AIUEOS_GUEST_INPUT_OK eventq-used=1 synthetic=0`. **P5 UNVERIFIED**.

### Guest paint (ADR-0092)
- KERNEL.ELF paints both boot-desktop rects back-then-front from
  Kotoba `kotoba_aiueos_wm_hit` and samples the overlap pixel.
  Gate: `clojure -M:compositor guest-paint`. Named red is hosted JVM
  `AIUEOS_COMPOSITOR_WM_OK` and a key-order paint (window 1 on top at
  overlap). `guest-wm` / `guest-ime` / `gpu` stay green without this
  serial line. Leftover `:native-compositor-absent`. virtio-input still
  synthetic. **Measured 2026-08-23:** `guest-paint` leftover `[]` with
  `AIUEOS_GUEST_PAINT_OK boot-overlap=2 raised-overlap=1 key-order=0`.
  P5 UNVERIFIED.

### Guest WM (ADR-0091)
- KERNEL.ELF Kotoba `kotoba_aiueos_wm_hit` z-hits two overlapping boot
  rects. Gate: `clojure -M:compositor guest-wm`. Named red is hosted JVM
  `AIUEOS_COMPOSITOR_WM_OK`. Leftover after this slice was
  `:one-guest-scanout`. P5 UNVERIFIED.

### Guest IME (ADR-0090)
- KERNEL.ELF Kotoba `kotoba_aiueos_ime_commit(107, 97)` returns U+304B.
  Gate: `clojure -M:compositor guest-ime`. Named red is hosted JVM
  `AIUEOS_COMPOSITOR_IME_OK`. Latin echo is leftover `:latin-leak`.
  `ime` / `kanji` / `kami` / `gpu` stay green without this serial line.
  Leftover `:native-compositor-absent`. virtio-input still synthetic.
  **Measured 2026-08-23:** `guest-ime` leftover `[]` with
  `AIUEOS_GUEST_IME_OK committed=u+304b latin-leak=0`. P5 UNVERIFIED.

### Hosted kami.webgpu presenter (ADR-0089)
- `#kami-viewport` calls `kami.webgpu/init!` then `draw!`. Gate:
  `clojure -M:compositor kami`. Named red is `clear-only-desktop`
  (sky-only `beginRenderPass`). IR is `kami.webgpu.ir/render-ir` with
  ≥1 instance. `kanji` / `ime` stay green without a kami frame.
  Leftover `:native-compositor-absent` after ADR-0090. P5 UNVERIFIED.

### Hosted IME kanji (ADR-0088)
- Space converts `か` to first candidate `加`; Enter commits. Gate:
  `clojure -M:compositor kanji`. Named red is `kana-only-desktop`
  (Space commits kana). `clojure -M:compositor ime` stays kana-only.
  Leftover `:native-compositor-absent` after ADR-0090. Not mozc. Hosted
  IME stays. P5 UNVERIFIED.

### Bare-metal net (P2, green on QEMU UEFI)
- Guest TLS 1.3 (0x1301) + HTTPS GET of empty raw CID with SHA-256 admit
  (ADR-0082). Gate: `clojure -M:bare-metal cloud` EXIT=0 leftover `[]`.
  Hosted `cloud-live` does not count. CertificateVerify is ADR-0087
  (`clojure -M:bare-metal cert-verify`). Chain-to-anchor still leftover.
  **Measured 2026-08-23:** `cert-verify` EXIT=0 leftover `[]` with
  `AIUEOS_CERTVERIFY_PROBE result=ok scheme=ecdsa_secp256r1_sha256`, and
  `cloud` still EXIT=0 leftover `[]` on the same firmware (ADR-0087).

The Phase-0 substrate plus the runtime/robotics/agent work built on top of it.

### Capability OS core
- Component **manifests** as kotoba (EDN) with strict, fail-loud validation —
  unknown `:aiueos/*` keys, bad kind/trust, out-of-range limits, non-integer
  args, empty entry, and malformed topic maps are all hard errors.
- **Capability graph** + **policy reasoner**: imports must resolve (exporter /
  kernel primitive / grant), effect-vs-trust lockdown (`:ai-generated` denied
  network/secrets/persist), and the driver **DMA→IOMMU** rule.
- **Fail-loud policy files**: unknown `:aiueos/*` policy keys, an unknown trust in
  `:aiueos/forbid`, and non-map `grants`/`forbid` are hard errors (a typo can't
  silently drop a grant or a lockdown).
- **Broker**: verify → safe-check → compile/load → run, every decision audited.
- **Safe-kotoba subset** gate (no eval/require/slurp/reflection/dotted host
  classes) before compiling source.
- **Staged boot** (`aiueos up`, Stage 0–4): link → topological order → verify →
  launch; boot order derived from the capability graph.
- **Duplicate component id** and **device-binding exclusivity** (one driver per
  `bus:vendor:device`) are rejected.

### Runtime + robotics
- Broker-mediated **`aiueos:host` ABI**, capability-gated per call:
  `log` / `clock` / `random` / `publish` / `poll` / `take` / `count`.
- **Topic bus**: latest-value (`poll`) + per-topic **FIFO** queue (`take`) +
  publish `count`; the ROS-topic analogue.
- **Per-topic isolation**: `:aiueos/publishes` / `:aiueos/subscribes` confine a
  component to declared topic ids; a call to an undeclared topic traps.
- **Named topics linked to ids** via `:aiueos/topics {:name id}` — publishes/
  subscribes are derived from the `:topic/<name>` exports/imports.
- **Periodic control loop** (`aiueos up --rounds N`): one bus threaded across N
  rounds; `clock()` returns the monotonic cycle.
- Fuel + linear-memory limits enforced; runaways trap.
- **Per-cycle IO quota** (`:aiueos/quota`, ADR-0006): host-call / publish rate caps
  enforced in the host ABI — an over-budget call traps like an ungranted capability.
- **Cooperative scheduler** (`:aiueos/schedule`, ADR-0006): deterministic
  period-skipping (run every N cycles) + priority ordering *within* dependency
  depth, so an urgent node runs earlier without ever preceding its provider.

### Self-owned VMM ("hvt tender", ADR-0014 V0)
- **`aiueos.hvt`** — the monitor side of aiueos virtualization, complementing
  `aiueos.vm` (which *launches QEMU*) and `aiueos.vfio` (raw access to a device
  QEMU exposes). Creates a VM through Linux **KVM** (`/dev/kvm`), maps guest
  RAM, loads a guest image, runs the vcpu, and services its exits — the Solo5
  `hvt` shape ADR-2607022400 named and ADR-0011 Phase 1 deferred. Every syscall
  (`open`/`ioctl`/`mmap`) goes through `java.lang.foreign` (FFM), "clj on clj,"
  no new Rust/C (honors the 2026-07-10 owner rule without a waiver).
- **V0 boot spike, verified end-to-end on real KVM**: a minimal aarch64 guest
  writes `HI\n` byte-by-byte to an MMIO serial port (each `strb` traps out as
  `KVM_EXIT_MMIO`, reconstructed by the VMM) then writes a poweroff MMIO port
  for a controlled halt. `spike` returns an audit-shaped **run receipt**
  (`:serial "HI\n" :serial-ok? true :shutdown? true :steps 4 :halt
  :mmio-poweroff`). Exercised on Apple M4 → Lima vz nested-virt → aarch64
  Ubuntu `/dev/kvm`. Pure parts (ioctl-number encoding, kvm_run/…-region
  struct offsets, the aarch64 PC core-reg id, the fixed guest program) are
  unit-tested on any JVM host; the live KVM loop is gated by
  `scripts/hvt-smoke.cljs` (nbb) in a Linux/KVM VM (#110).
- Deferred to V1+ (#110): real PSCI SYSTEM_OFF clean shutdown (the bare
  MMU-off guest's `hvc` did not raise `KVM_EXIT_SYSTEM_EVENT`, so V0 halts via
  the MMIO poweroff port), direct-loading the ADR-0013 kernel image, a virtio
  device model reusing `aiueos.virtio`'s ported protocol logic, and an x86_64
  long-mode guest. macOS/HVF backend is V2 (behind the
  `com.apple.security.hypervisor` entitlement question).
- **V1 progress (2026-07-17), two hard findings** (ADR-0014 "V1 progress"):
  (1) the ADR-0013 kernel is **x86_64-only**, so direct-loading it under the
  tender needs an **x86_64 KVM host** — an aarch64 host (the dev machine) can
  only run aarch64 guests, so the kernel-boot gate waits on x86 hardware; the
  ELF-load logic itself is arch-independent and authorable now. (2) PSCI
  SYSTEM_OFF does **not** fire for a hand-written bare guest — reproduced via
  the new `guest-program-psci` diagnostic (serial → `hvc` → poweroff
  fall-through): the `hvc` blocks `KVM_RUN` in-kernel with neither a
  system-event nor the fall-through exit, so KVM injects an exception the
  vector-table-less guest spins on; forcing `KVM_ARM_VCPU_PSCI_0_2` regressed
  it further. A real PSCI shutdown needs a real-kernel guest (finding 1).
  Landed: `spike` parametrized over `{:program …}`, the PSCI diagnostic + tests
  (7 tests / 41 assertions), a `clojure -M:hvt psci` diagnostic entry
  (intentionally blocking — run under `timeout`), and `KVM_ARM_VCPU_INIT`
  return-code checking. Default poweroff path + smoke gate stay green.
- **V1 progress (2026-07-17), ELF64 direct-loader** (ADR-0014 "V1 progress"):
  the arch-independent half of kernel-direct-load, built and verified
  end-to-end. Pure `parse-elf64`/`rd-le`/`elf-load-range` (host-testable);
  `spike` generalized via `boot-plan` to accept `{:elf-bytes …}`, mapping guest
  RAM at the ELF's load base (the fixture links at `0x40000000` — an arbitrary
  non-zero GPA), copying PT_LOAD segments and setting PC = `e_entry`. Real
  fixture `resources/hvt/guest-aarch64.elf` (genuine `ld` output; reproducible
  byte-identical via `scripts/build-hvt-guest.cljs`, nbb, SHA-pinned). Verified
  on real KVM: `clojure -M:hvt elf …` boots it to `{:serial "HI\n" :shutdown?
  true}`. `scripts/hvt-smoke.cljs` now gates both the raw-word (V0) and ELF (V1)
  cases; `aiueos.hvt-test` is 11 tests / 57 assertions. The remaining
  kernel-boot gap is purely the x86_64 KVM host (Finding 1); the ELF-load
  mechanism is done.
- **V1 progress (2026-07-17), virtio-mmio device model** (ADR-0014 "virtio-mmio
  device model"): the tender now emulates a device the guest can probe, the
  first reuse of `aiueos.virtio`'s host-side logic. Adds **MMIO read
  emulation** (`set-mmio-data!` answers guest register reads before re-entering
  `KVM_RUN`) and a pure, host-tested **`virtio-console` device model**
  (`virtio-console-read`/`-write` over `aiueos.virtio/mmio-reg` + magic/version/
  status/feature constants) presenting device-id 3 with `VIRTIO_F_VERSION_1`.
  A real aarch64 driver guest (`guest-virtio-aarch64.S` → `.elf`, loaded by the
  V1 ELF loader) runs the full `ACKNOWLEDGE → DRIVER → feature-negotiate →
  FEATURES_OK → DRIVER_OK` transport handshake and emits `HI\n` only on total
  success. Verified on real KVM (21-step trace, `:virtio-status 15` = DRIVER_OK).
  `hvt-smoke.cljs` now gates raw + ELF + virtio; `aiueos.hvt-test` is 14 tests /
  75 assertions. The virtqueue data path (rings/descriptor DMA) is the next
  milestone; queue-config writes are already tracked in device state.
- **V1 progress (2026-07-17), virtqueue data path** (ADR-0014 "virtqueue data
  path"): the virtio-console device now moves **data**, not just handshake
  registers — a freestanding guest driver sets up a split virtqueue in guest RAM
  and transmits `HI\n` through the transmitq; the tender reads the avail ring +
  descriptor chain out of guest RAM and pulls the bytes into the receipt's
  `:console`. Adds guest-RAM access (`gram-rd`/`gram-set-le!`), pure split-queue
  servicing (`read-descriptor`/`walk-descriptor-chain`/`virtqueue-plan`,
  host-tested with synthetic RAM, reusing `aiueos.virtio/desc-flag`), per-queue
  config tracking + `queue-config`, and SP-register setup so guests can be
  written in freestanding C (`guest-virtqueue-aarch64.c`). Verified on real KVM
  (31-step trace, `:console "HI\n"` via the virtqueue + `:serial "HI\n"` guest
  confirmation, `:virtio-status 15`). `hvt-smoke.cljs` gates 4 cases (raw / ELF
  / transport / virtqueue); `aiueos.hvt-test` is 17 tests / 87 assertions. Both
  open V1 items (kernel direct-load, virtio device model) are now substantially
  delivered. Bug recorded: gcc `-O2` post-index `strb` MMIO stores fail
  `KVM_RUN` `ENOSYS` (no decodable syndrome) — write to a fixed register address.
- **V1 progress (2026-07-17), virtqueue receiveq** (ADR-0014 "virtqueue
  receiveq"): the virtio-console is now **bidirectional** — the device→guest
  mirror of transmit. On a receiveq notify (queue 0) the device *fills* the
  driver's device-**writable** buffers instead of reading them. Adds pure
  receive servicing (`walk-writable-chain`/`fill-targets`/`virtqueue-rx-plan`,
  host-tested incl. capacity truncation) + `process-virtqueue-rx!`, queue-index
  routing (queue 1 = tx into `:console`, queue 0 = rx delivering
  `virtio-console-rx-input`), and a real receive guest
  (`guest-virtqueue-rx-aarch64.c` → `.elf`) that posts a writable buffer, is
  filled by the tender, and echoes the received bytes to serial. Verified on
  real KVM (31-step trace, `:serial "HI\n"` from the device→guest path).
  `hvt-smoke.cljs` gates 5 cases; `aiueos.hvt-test` is 20 tests / 100 assertions.
  The virtio-console device model is complete in both directions.
- **V1 progress (2026-07-17), PSCI finding corrected + vcpu power-state control**
  (ADR-0014 "PSCI finding, corrected"): by reading the `hvc` return code in `x0`,
  established that this KVM environment answers **every** PSCI function id
  (`PSCI_VERSION`/`SYSTEM_OFF`/`SYSTEM_RESET`/`CPU_OFF`) with `NOT_SUPPORTED`
  (`0xFFFFFFFF`) and resumes — so no PSCI shutdown-exit exists here (correcting
  the earlier, wrong "guest spins in a zeroed vector table" explanation). The
  `KVM_ARM_VCPU_PSCI_0_2` feature bit doesn't help and leaves the boot vcpu
  `MP_STATE_STOPPED` (the earlier "regression"). Adds real vcpu power-state
  control (`KVM_GET_MP_STATE`/`KVM_SET_MP_STATE` + `mp-state` constants) and a
  `:psci-0-2?` diagnostic option (sets the feature + forces RUNNABLE). MMIO
  poweroff remains the halt mechanism. `aiueos.hvt-test` is 18 tests / 92
  assertions.

### Security / supply chain
- **Artifact integrity**: `:aiueos/wasm-sha256` is verified before run
  (tamper detection); `aiueos hash` computes it.
- **Manifest authenticity (ed25519 signatures, ADR-0003)**: `:aiueos/signature`
  over the identity↔artifact binding, verified against the policy
  `:aiueos/signers` registry. Valid → trust elevated to `:verified` + signer
  audited; forged/unregistered → denied. `:aiueos/require-signed` rejects unsigned
  components. `aiueos sign` produces signatures (`signing` feature, default-on).
- **Audit**: append-only EDN log records grant/deny/compile/run **and runtime
  traps (reject)**; queryable with `aiueos audit --event/--component/--edn`.

### Code as data (agent admission)
- **`Broker::admit` / `aiueos admit`** (ADR-0004): the front door for a component
  an AI agent emits at runtime. Trust is **floored to `:ai-generated`** before
  verification — agent code can never grant itself trust (a signature can still
  elevate it). Returns a structured verdict `{admitted, result, reason,
  reason-code}` so an agent loop branches on a stable `:reason-code`
  (`:denied` / `:unsafe` / `:run` / …) and iterates.

### Tooling / agent surface
- Machine-readable **`--edn`** on `verify`/`inspect`/`up`/`run`/`audit` (verdicts,
  denials, and structural errors all as EDN).
- **`inspect --dot`** — Graphviz of the component dependency graph (named topics
  render as the actual dataflow edges).
- **`up --dry-run`** — link → order → verify a whole system without launching
  anything (CI / pre-boot validation, no side effects).
- `aiueos hash`, helpful errors (e.g. `inspect`/`up` on a single manifest point at
  `verify`/`run`), robust CLI arg parsing.
- A runnable **authoring example** (`examples/authoring/`) kept verified by a test.

### Build / project
- Standalone build: `kotoba-edn` is a git dependency; the CLJ compiler
  (`kototama`) is an opt-in monorepo-only feature.
- **CI** (GitHub Actions): core + exec-only + rustfmt.
- **193 tests + 3 doctests** green across the core / exec-only / full configs.

[Unreleased]: https://github.com/kotoba-lang/aiueos/commits/main
