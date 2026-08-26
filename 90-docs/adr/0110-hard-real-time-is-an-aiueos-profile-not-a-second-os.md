# ADR-0110 — Hard real-time is an AIUEOS profile, not a second OS

- Status: accepted
- Date: 2026-08-26

## Decision

Keep one AIUEOS identity and source tree. Add an orthogonal
`:aiueos/timing-profile :hard-real-time` as a separate native artifact. Do not
rank timing as a stronger security profile:
`regulated` and `hard-real-time` answer different questions and may be combined.

The hard-real-time artifact contains only the native C-free kernel and the
statically admitted AMU RT subset. JVM, GC, hosted adapters, dynamic allocation
and post-start page faults are outside its trusted execution boundary. The RT
artifact boots and schedules independently. Any future external supervisory
plane is outside the RT artifact and may communicate only through a bounded,
copying IPC boundary; it cannot supply clocks, scheduling or fallback runtime.

## Admission boundary

Naming the profile is a claim, not a hint. The native release gate refuses it
unless evidence is versioned and bound to the AMU artifact and response-time
analysis, the kernel uses fixed-priority preemption with a priority-ceiling
protocol, every syscall and driver path is bounded, and physical-hardware
measurements fit the image's declared interrupt, dispatch, jitter and
task-budget maxima. QEMU evidence is useful functional evidence but cannot
establish those maxima.

The Linux/JVM PID 1 is not part of this path and does not interpret, launch or
serve as fallback for the RT artifact. Its source, process model and boot
configuration are outside the RT trusted execution boundary. Only the native
C-free UEFI-to-kernel path may satisfy this profile.

The first profile is deliberately uniprocessor. SMP adds shared-cache,
cross-core interrupt and locking bounds and requires a new evidence version.

## Why not a second OS

The existing firmware transition, APIC timer, ring-3 entry, address spaces,
capability direction, release receipts and AMU native artifacts are the same
mechanisms an RT variant needs. Forking the OS now would duplicate the most
security-sensitive code and let fixes drift. Artifact separation provides the
necessary timing boundary without source or product fragmentation.

Extract a separate OS only if an external safety-certification boundary,
independent release authority, or incompatible kernel ABI makes shared change
control itself unacceptable. That is an organizational and certification
decision, not a scheduler implementation detail.

## Current claim

This ADR, the native RT contract and the explicit best-effort receipt prevent
the existing native image from being mistaken for an RTOS. They do not make
the current kernel a qualified RTOS. The current C-free kernel has no native
preemptive scheduler and therefore cannot produce the required RT receipt.
