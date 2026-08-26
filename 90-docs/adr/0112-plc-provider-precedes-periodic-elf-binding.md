# ADR-0112 — PLC provider precedes periodic ELF binding

## Decision

Land a kernel-side transaction-provider vertical slice for the two-input,
two-output motor example before changing the signed user ELF loader or the
general scheduler. It implements the capability IDs already emitted by the
Structured Text compiler:

- 16 reads the scan's immutable input image;
- 17 writes one previously unstaged shadow output;
- 19 records the single watchdog checkpoint; and
- 18 atomically publishes every declared output only while the scan remains
  inside both its budget and deadline.

Any malformed capability call, missing stage, missing or duplicate watchdog,
budget overrun, deadline overrun, or program fault suppresses commit, writes
the configured safe values, and latches the fault. A later scan cannot clear
that latch implicitly.

The RT-only QEMU build waits for APIC timer tick 10 and releases the motor
example against an absolute next-release value. It then runs the failure
vectors in the same freestanding kernel. The normal kernel build does not take
this early-exit test path.

## Evidence boundary

`AIUEOS_PLC_RT_QEMU_OK` proves a native UEFI boot, APIC-driven release, input
snapshot isolation, atomic output publication, and fail-safe behavior for the
provider. It does not prove that one APIC tick is one millisecond, a 10 ms
physical cycle, WCET, response-time admission, a physical I/O driver, or that
the generated and signed PLC ELF is already scheduled periodically. The marker
therefore says `cycle=10ticks` and `timing=logical-unqualified`.

The next slice must bind the exact admitted PLC receipt and signed ELF to a
static fixed-priority task, route that task's runtime calls to capabilities
16--19, replenish it at absolute scan boundaries, and prove preemption and
overrun handling without weakening the existing signature checks.
