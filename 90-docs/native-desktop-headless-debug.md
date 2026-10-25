# Native desktop debugging without screen sharing

The native AIUEOS diagnostic desktop can be debugged through QEMU's local
QMP Unix socket. VNC, a VNC password, and a person pressing guest keys are
unnecessary. Keep QMP on a local Unix socket and `-nic none`; the QMP socket
controls the VM and must stay in a private task workspace.

For routine agent debugging, use the headless QEMU gates and this QMP observer
first. They read the guest's completed frame, goal, focus, draft generation,
and framebuffer directly. Do not request a Screen Sharing login or a manual
key press to establish those facts. Keep the VNC password on any separately
shared interactive session; QMP does not require disabling it.

## Reproducible save and restart check

Run [`check-native-draft-restart-qemu.py`](../os/aiueos/scripts/check-native-draft-restart-qemu.py)
with a newly built `local-draft` media, matching EFI and kernel, QEMU and
OVMF firmware, and a fresh output directory. The complete command and
artifact roles are in [AWAI-90 progress](native-workspace-restore-progress.md#reproduce-the-component-gates).
The observer provisions its own 64 KiB scratch draft disk, sends PS/2 keys
through QMP, checks native guest state and framebuffer, commits with F5,
boots a new QEMU process, and verifies the restored bytes and rejection
cases. It never needs a VNC listener.

## Inspect a running VM

For a VM started with `-qmp unix:qmp.sock,server=on,wait=off`, set these
paths to the running VM's socket, the exact kernel ELF in its EFI image, and
a **new** output directory:

```sh
python3 os/aiueos/scripts/debug-native-desktop-qmp.py \
  --qmp-socket "$SESSION/qmp.sock" \
  --kernel "$BUILD/kernel/KERNEL.ELF" \
  --output "$OBSERVATION" --keys 2 \
  --expect-focus 2 --expect-draft-enabled 1 \
  --expect-generation 0 --expect-goal $'Goal local:\n'
```

The script waits for a completed native frame, sends the requested keys,
reads the guest's actual desktop surface and context, and saves `debug.json`,
`desktop.ppm`, and `desktop.png`. With no `--keys`, it only observes. Supported
debug keys are `2`, lowercase `a` through `z`, `space`, `backspace`, `esc`, and `f5`.
State expectations produce a nonzero exit and a `FAIL` receipt with actual and
expected values when they differ. The screenshot is captured on either result.
Sending F5 to an attached VM requires `--allow-draft-write` because it can
commit to that VM's draft disk. Use the isolated check above for routine
write and cold-restore tests.

The exact matching kernel is required: the observer derives the context
address from it. A successful QMP read is a diagnostic observation of that
guest, not proof of a registered Itonami dapp, D2 workspace, hardware boot,
or Linux-free compiler closure.

On 2026-10-25, the debugger attached to the running `local-draft` VM on port
5902 through its QMP socket. It read `Goal local:\n`, `draftEnabled=1`,
`frames=1`, then sent `2` and observed `focus=2`, `keys=1`, `frames=2`; the
captured framebuffer displayed the Itonami card. No VNC authentication or
manual guest action was used for that observation.

On 2026-10-26, a fresh headless run of the save/restart gate passed all
positive and rejection cases without a VNC session. A separate QMP read of
the still-running diagnostic VM sent `2`, saw `focus=2`, `keys=2`,
`framesStarted=frames=3`, `draftEnabled=1`, and captured the Itonami card.
The [headless debug receipts](evidence/headless-debug-20261026/README.md)
include a failing expected-focus control. These are VM diagnostics, not
physical-device or full Itonami-agent acceptance.
