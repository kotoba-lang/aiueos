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

The RT-only QEMU build binds the exact PLC ELF SHA-256 from its build receipt
into the kernel artifact. A PLC-specific C-free Kotoba admission object checks
the canonical ELF segments and exact capability bitmap before the loader maps
the program RX/RW+NX at CPL3. The fixed-priority policy object is linked under
a distinct symbol and invoked directly from the APIC interrupt/context-switch
path. The priority-5 PLC task preempts the priority-255 kernel task; budget
exhaustion returns control to the kernel. The same task is then replenished at
its next absolute release and re-enters the same ELF with a fresh data image.
The program's native `syscall 5` calls reach capabilities 16--19 and commit
outputs 1 and 42 on the first scan, then 0 and 100 from different inputs on the
second. The normal kernel build does not link or enter this test profile.

## Evidence boundary

`AIUEOS_PLC_RT_QEMU_OK` proves a native UEFI boot, receipt-bound generated PLC
ELF execution at CPL3, fixed-priority preemption through the native interrupt
switch path, two absolute APIC-tick releases of the same task, budget
replenishment, input snapshot isolation, atomic output publication, and
fail-safe provider vectors. It does not prove that one APIC tick is one
millisecond, a 10 ms physical cycle, long-duration periodic stability, WCET,
response-time admission, a production signature, or a physical I/O driver.
The marker therefore says `scans=2`, `cycle=10ticks` and
`timing=logical-unqualified`.

The next slice must add production signature admission and long-duration
periodic stress, followed by timer calibration, response-time analysis and
physical I/O timing. Existing general-app signature checks remain unchanged;
the RT smoke uses a separately linked receipt/digest-bound artifact.
