# ADR-0100 — Guest compositor gates run on nbb, not a JVM

Date: 2026-08-25

## Status

Accepted for a **discriminating JVM-off measurement slice** of root
`adr-2608221625-aiueos-chromeos-cloud-desktop` Desktop leftover
(README Desktop / compositor unit). **Guest session is green only when
`nbb --classpath src scripts/compositor-guest.cljs guest-session`
prints `AIUEOS_COMPOSITOR_GUEST_SESSION_OK`:** KERNEL.ELF serial has
`AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2`,
and hosted JVM serial `AIUEOS_COMPOSITOR_WM_OK` does **not** count.

The same nbb runner is the evidence for `guest-ime`, `guest-wm`,
`guest-paint`, `guest-input`, `guest-gpu-two`, `guest-scanout-two`, and
`guest-broker`. Serial classifiers live in `aiueos.compositor.guest`
(portable `.cljc`). `clojure -M:compositor guest-*` is leftover
`:jvm-gate-runner` (exit 1). Hosted `smoke` / `gpu` / `wm` / `ime` /
`kanji` / `kami` / `serve` stay JVM.

This file records the attempt. The receipt from the nbb command is the
measurement. QEMU/firmware/serial unanswered is leftover `:unmeasured`
(exit 3, not a silent pass).

Not executable, and stated here rather than at the end:

- **This is not a native Kotoba component runtime, and not P5 physical boot.**
  Leftover after this slice is still `:native-compositor-absent`.
- **This does not drop amu's compile-time JVM, and does not drop Chicory
  `aiueos.execute`.** Those are toolchain / hosted Wasm. KERNEL.ELF
  already boots without a JVM.
- **CACAO write, chain-to-anchor, and physical boot remain.** The
  Chrome OS-shaped desktop goal is **not complete**.
- **Older guest ADRs (0090–0098) named `clojure -M:compositor guest-*`.**
  This ADR supersedes the *measurement host* only. Serial lines and
  Kotoba objects stay. Those gates must not start requiring a new
  KERNEL.ELF marker.

## Context

ADR-0098 leftover named native component runtime. The product that
boots already has no JVM. What still required a JVM for *guest*
compositor proof was `clojure -M:compositor guest-*` (`ProcessBuilder`
in `aiueos.compositor`). Classification was already portable string
match; the runner was not.

`aiueos.compositor` still requires `aiueos.compositor.desktop` /
`phone-bind` (kami.webgpu.ir, grant, JDK HTTP). nbb cannot load that
ns. Classifiers move to `aiueos.compositor.guest`.

## Decision

1. Evidence command is `nbb --classpath src scripts/compositor-guest.cljs <profile>`.
2. One classifier implementation (`aiueos.compositor.guest`). nbb and
   JVM tests both require it.
3. Same UEFI smoke script. Same extra env (`AIUEOS_GUEST_INPUT`,
   `AIUEOS_GUEST_SCANOUT_TWO`). No new `.sh`. Do not `qemu_exit`.
4. `clojure -M:compositor guest-*` prints leftover `:jvm-gate-runner`
   and exits 1. Hosted JVM WM/IME stay red for guest classification.
5. SPA `#desktop` names the nbb commands. One document. jp-go-dds. No
   second HTML. No liquid-glass. No Three.js.

## Consequences

Hosted leftover print stays `:native-compositor-absent` (native
component runtime, P5). Goal not complete. amu compile JVM and Chicory
hosted execute remain.

## Measurement

Recorded by `nbb --classpath src scripts/compositor-guest.cljs guest-session`
on this branch. **2026-08-25 this Mac:** printed
`AIUEOS_COMPOSITOR_GUEST_SESSION_OK` and serial
`AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2`.
`clojure -M:compositor guest-session` printed leftover `:jvm-gate-runner`
and exited 1. QEMU != P5.
