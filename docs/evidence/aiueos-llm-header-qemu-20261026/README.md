# Native `llm-v1` header guest gate

`os/aiueos/native/llm_v1_header.kotoba` validates a bounded 64-byte request
header. `llm_v1_header_probe.kotoba` calls that validator in a C-free AIUEOS
kernel. The Mac selfhost compiler produces a reproducible 17-module EFI/kernel
image. The Amu QMP observer reads 12 guest checks and the restored header from
owned memory. A separate, valid 16-module kernel returns the same result value
but has no LLM receipt, so the observer must reject it.

`os/aiueos/scripts/check-llm-v1-header-qemu-macos-native.py` builds and runs
both images headlessly. It accepts a pinned signed Mac compiler, a pinned Amu
observer, QEMU and OVMF. Set the five `AIUEOS_*_SOURCE_PATH` variables used by
`build-native-kernel-macos.py` to the matching owner-source roots. A successful
run writes `llm-header-qemu.json` and the individual build/QMP receipts under
the output directory. No VNC login, keyboard input, password, Linux guest,
JVM, Node or C compiler is needed for this gate.

The local run on 2026-10-26 returned
`PASS_MAC_NATIVE_LLM_HEADER_GUEST_AND_NEGATIVE`. The positive guest had 12/12
checks; the valid-image negative was refused at the receipt assertion.
Compiler SHA-256: `5635e728fc49e8d0c7ca3685b5c22ac72bd2d85fae3799a0af45204b8e5f0755`.
Observer SHA-256: `002bae29b0a705643eba9d0cf881e4f4ab409c7238788465db99c2c4e7e76353`.
Positive kernel SHA-256: `c11eb22af8613757c0bbd6fe9b25712a0115869b2f17e77778fdd511a42d0e9c`.
Negative kernel SHA-256: `713ac2d4fab7947029cc280e3ab01713baa47329e25033a04865183cbef9c619`.

This is a kernel-side component test. Wire 11 dispatch, CPL3 kit admission,
model selection/execution, trusted human approval, Itonami agent/tool turns,
and durable restore remain open. It does not change the OS release score.
