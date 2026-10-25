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

A second attachment to the still-running :5902 VM sent `2` through QMP and
passed all four state assertions with no VNC login or manual key press.
Its [live receipt](qmp-live-20261026.json) records `focus=2`, `keys=3`,
`framesStarted=frames=4`, `draftEnabled=1`, `generation=0`, and exact goal
`Goal local:\n`. The [captured image](qmp-live-20261026.png) shows the
Itonami card and blank local goal editor. The shell sandbox needed permission
to connect to the local QMP socket; no guest authentication was involved.

The kernel SHA-256 in all receipts is
`38872271cda98dfdd5482ef06451f99b3dd9ac15e1bb5fd9b9aec8a8af993801`.
This proves the local diagnostic desktop path, not a registered Itonami dapp,
full agent loop, physical hardware, or Linux-free build producer.
