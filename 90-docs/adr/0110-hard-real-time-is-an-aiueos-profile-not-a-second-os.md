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

## RTOS comparison

| Concern | QNX Neutrino | VxWorks | AIUEOS RT v1 decision |
|---|---|---|---|
| Ready-thread choice | Highest-priority READY thread preempts immediately; FIFO, round-robin and sporadic policies are per thread | Priority-based preemption; FIFO, round-robin, sporadic and adaptive scheduling are available | Fixed-priority preemption; lower numeric value is more urgent |
| Equal priority | FIFO runs until block/preemption/yield; RR adds a quantum | FIFO by default with optional round-robin | FIFO by default; RR only rotates after quantum expiry |
| Priority inversion | Message delivery inherits the receiver's priority; synchronization protocols are available | Priority inheritance/ceiling mechanisms are available | Effective priority is mandatory scheduler input; RT v1 admits only priority ceiling |
| Partitioning | Adaptive partitioning constrains CPU budgets | Time/space partitioning and ARINC 653 profiles are available | Single static admission domain in v1; partitions require a later evidence version |
| Qualification | Product/certification profile dependent | Cert Edition supplies safety-certification artifacts | No qualification by name: exact-image receipt, RTA and physical measurements are mandatory |

QNX references: [thread scheduling](https://qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.sys_arch/topic/kernel_SchedulingAlgorithms.html),
[message priority inheritance](https://qnx.com/developers/docs/7.1/com.qnx.doc.neutrino.sys_arch/topic/ipc_Priority_inheritance_messages.html), and
[adaptive partitioning](https://qnx.com/developers/docs/7.1/com.qnx.doc.adaptivepartitioning.userguide/topic/aps_details_Other_schedulers_.html).
VxWorks references: [VxWorks datasheet](https://www.windriver.com/resource/vxworks-datasheet) and
[VxWorks 653 / ARINC 653](https://www.windriver.com/resource/safety-critical-software-development-for-integrated-modular-avionics).

The QNX and VxWorks feature sets are comparisons, not compatibility claims.
AIUEOS does not adopt their APIs or certification status.

| System | Scheduler and equal-priority rule | Inversion control | Scope relevant to AIUEOS |
|---|---|---|---|
| FreeRTOS | Configurable preemptive fixed-priority scheduling; time slicing can rotate equal-priority tasks | Mutex priority inheritance; no priority-ceiling contract in the ordinary kernel API | Excellent minimal MCU baseline, but build-time options can change scheduler semantics |
| μITRON 4.0 | Priority precedence with explicit dispatch-disabled and non-task contexts; round-robin is constructed through rotation service calls | Mutex priority inheritance and priority ceiling are both specified | Strong model for a small static profile and precise service-call context rules |
| Zephyr | Highest-priority READY thread; FIFO for the longest-waiting peer, optional time slicing and optional EDF tie-breaking | Mutex priority inheritance with a configurable inheritance limit | Broad configurable OS with cooperative and preemptive thread classes, SMP and several ready-queue implementations |
| AIUEOS RT v1 | Fixed-priority preemption; FIFO by default and RR only at quantum expiry | Priority ceiling only; effective priority is scheduler authority | Static admitted task set, one core, one ready-queue ABI, no cooperative class or EDF |

References: [FreeRTOS scheduling](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/04-Task-scheduling),
[FreeRTOS mutexes](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/04-Mutexes),
[μITRON 4.0 specification](https://www.tron.org/wp-content/themes/dp-magjam/pdf/specifications/en_US/WG024-S001-04.03.00_en.pdf),
[Zephyr scheduling](https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html), and
[Zephyr mutex priority inheritance](https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html).

This comparison fixes additional v1 choices. The admitted task set and base
priorities are immutable after start. Deadlines remain admission and overrun
evidence, not an EDF ordering key. Cooperative tasks and build-time changes to
preemption semantics are refused by the RT receipt. This keeps the measured
scheduler identical to the scheduler described by the release contract.

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
the current kernel a qualified RTOS. A C-free fixed-priority dispatch policy is
generated as a deterministic, import-free AMU object and its receipt digest
must match the policy embedded in the kernel. The PLC RT QEMU profile now
invokes that object from the native APIC interrupt/context-switch path and
proves P-256 signed PLC ELF admission and a priority-5 task preempting the
priority-255 kernel task across 100 absolute-tick releases, including budget
replenishment and return to the kernel after each scan. General static task-set
integration and actual calibrated physical timing, RTA/WCET measurements and
bounded drivers remain required before the kernel can produce a qualified RT
receipt; their new receipt gates do not substitute for that evidence.
