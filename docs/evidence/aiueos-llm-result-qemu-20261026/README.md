# Native `llm-v1` result component in an AIUEOS guest

`os/aiueos/native/llm_v1_result_shape.kotoba` adds a proposed, bounded
transport for the kit's completion and error arms. A shared native UTF-8
validator is used by both request and result validation. A 64-byte result header
declares the arm, separate text/keyword/message lengths, usage counts and
retryable flag. The validator checks the paired request header, capacity,
output-token budget, exact lengths, zero padding and UTF-8. The host must
still provide distinct owned regions, the model and a valid grant. This is a
kernel component; it does not lower the nested kit value into a user ELF.

The signed Mac Amu F2 selfhost compiler SHA-256
`5635e728fc49e8d0c7ca3685b5c22ac72bd2d85fae3799a0af45204b8e5f0755`
built a reproducible 20-module EFI/kernel image. A private-QMP observer SHA-256
`dc630e3d468ecb2d449fa5fcd056871ed8334e3e58dd51cffe3cc69de81c4896`
read 12/12 header, 9/9 request and 6/6 result checks from guest-owned memory.
The result checks covered a valid completion, unknown tag, output-token budget,
malformed UTF-8, a valid error and invalid retryable value. The final restored
`ok` text, `stop` reason and canonical result header were read back. The
positive kernel SHA-256 was
`1490d983ae719ef167523e59e8fb7619f611f14a1bf713f65d0fce84f04ab411`.

`os/aiueos/scripts/check-llm-v1-result-qemu-macos-native.py` builds and runs
that image and a separate valid 16-module non-LLM image headlessly. The latter
returned 42 but failed the LLM receipt assertion. The local gate returned
`PASS_MAC_NATIVE_LLM_RESULT_GUEST_AND_NEGATIVE`; its pinned receipt is
`workspaces/codex/jun444-llm-result-20261026/result-gate-2/llm-result-qemu.json`.
The same observer also refused an earlier valid 18-module header/request-only
image at the **result** receipt assertion: all six result words were absent.
That supplemental control used the pinned prior gate image at
`workspaces/codex/jun444-aiueos-llm-20261026/qmp-current-gate-1/positive-build`.

No VNC login, password, Linux guest, JVM, Node or C compiler is needed for
these runs. The result format is a proposed native transport, not proof of the
canonical nested ABI, wire 11/CPL3 dispatch, provider execution, a real model,
trusted human consent, an Itonami agent turn, release qualification or physical
hardware. The OS release score remains unchanged.
