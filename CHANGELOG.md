# Changelog

All notable changes to **aiueos** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/); this crate is pre-1.0 (Phase-0).

## [Unreleased]

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
