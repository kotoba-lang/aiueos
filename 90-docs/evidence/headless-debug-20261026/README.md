# Native Itonami desktop: passwordless QMP debug readback

On 2026-10-26, the macOS host ran the local-draft AIUEOS image under
QEMU TCG with `-display none`, `-nic none`, and a local Unix QMP socket.
No VNC login or manual guest action was used.

The independent cold-process gate reported
`PASS_NATIVE_LOCAL_DRAFT_COLD_RESTORE`: commit generation 1, restore,
uncommitted power loss, commit generation 2, and seven corrupt/missing-media
refusals. See [native-draft-restart.json](native-draft-restart.json).

The live QMP observer read the same kernel's desktop context and framebuffer.
Its expected goal, focus, draft availability, and generation all matched;
see [qmp-pass.json](qmp-pass.json). A deliberately wrong expected focus
returned exit 1 and a `FAIL` receipt with expected 1 and actual 2;
see [qmp-negative-control.json](qmp-negative-control.json). The framebuffer
captured in the workspace showed the Itonami card with `Goal local:`.

The kernel SHA-256 in all receipts is
`38872271cda98dfdd5482ef06451f99b3dd9ac15e1bb5fd9b9aec8a8af993801`.
This proves the local diagnostic desktop path, not a registered Itonami dapp,
full agent loop, physical hardware, or Linux-free build producer.
