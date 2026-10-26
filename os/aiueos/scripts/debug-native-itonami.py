#!/usr/bin/env python3
"""Run the matching native Itonami QEMU gate without a login or guest input.

The selected gate owns fresh QEMU processes and uses private QMP standard I/O.
This controller only selects a source-bound gate and saves its result, including
the failure log, in a new mode-0700 observation directory.
"""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys


HERE = Path(__file__).resolve().parent
GATES = {
    "/os/aiueos/native/user-elf-v2-live-goal-entry.kotoba":
        ("debug-native-user-v2-unattended.py", "PASS", True),
    "/os/aiueos/native/user-elf-v2-durable-goal-entry.kotoba":
        ("debug-native-user-v2-durable-unattended.py",
         "PASS_NATIVE_DRAFT_COLD_RESTORE_TO_USER_V2_CPL3", True),
    "/os/aiueos/native/user-elf-v2-local-model-entry.kotoba":
        ("debug-native-local-model-confirm-qemu.py",
         "PASS_NATIVE_LOCAL_MODEL_DENY_APPROVE_CPL3_AFTER_RESTORE", False),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="complete Mac native kernel build")
    parser.add_argument("--output", type=Path, help="new private observation directory")
    parser.add_argument("--goal", help="lowercase Goal for live or durable profiles")
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args()
    if args.goal is not None and (not re.fullmatch(r"[a-z ]{1,51}", args.goal)
                                  or not args.goal.strip()):
        parser.error("--goal requires 1-51 lowercase letters or spaces with a letter")
    build = args.build_dir.resolve(strict=True)
    state_path = build / "build-state.json"
    if not state_path.is_file():
        parser.error("build_dir requires build-state.json")
    state = json.loads(state_path.read_text())
    if state.get("state") != "MACOS_NATIVE_IMAGE_CANDIDATE":
        parser.error("build is not a completed Mac native image candidate")
    entry = state.get("kernelEntry")
    if entry not in GATES:
        parser.error("no unattended Itonami gate for this kernel entry")
    script, expected, accepts_goal = GATES[entry]
    if args.goal is not None and not accepts_goal:
        parser.error("local-model gate has a fixed Goal fixture; omit --goal")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = (args.output or build.parent / f"{build.name}-itonami-debug-{stamp}-{os.getpid()}").resolve()
    if output.exists():
        parser.error("output must be a new private directory")
    output.mkdir(mode=0o700)
    guest = output / "guest"
    command = [sys.executable, "-I", str(HERE / script), str(build),
               "--output", str(guest)]
    if args.goal is not None:
        command.extend(("--goal", args.goal))
    for flag, value in (("--qemu", args.qemu), ("--firmware", args.firmware)):
        if value is not None:
            command.extend((flag, str(value.resolve(strict=True))))
    (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               env={**os.environ, "PYTHONOPTIMIZE": "0"},
                               start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=600)
        exit_code = process.returncode
    except subprocess.TimeoutExpired:
        # The guest gate may have a QEMU child. Kill the private process group
        # so an abandoned diagnostic VM cannot survive a timed-out controller.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = process.communicate()
        exit_code = 124
        stderr += b"\nunattended gate exceeded 600 seconds\n"
    (output / "stdout.log").write_bytes(stdout)
    (output / "stderr.log").write_bytes(stderr)
    gate_path = guest / "debug-summary.json"
    gate = None
    if gate_path.is_file():
        try:
            gate = json.loads(gate_path.read_text())
        except (OSError, ValueError):
            pass
    passed = (exit_code == 0 and isinstance(gate, dict)
              and gate.get("status") == expected
              and gate.get("qmpTransport") == "private-stdio"
              and gate.get("vncUsed") is False
              and gate.get("networkUsed") is False)
    screenshots = [str(path) for path in sorted(guest.rglob("*.png"))]
    report = {"schema": "aiueos.native-itonami-unattended-debug.v1",
              "status": "PASS" if passed else "FAIL", "kernelEntry": entry,
              "build": str(build), "buildReceipt": str(state_path),
              "gate": script, "gateStatus": gate.get("status") if isinstance(gate, dict) else None,
              "gateReceipt": str(gate_path) if gate_path.is_file() else None,
              "exitCode": exit_code, "stdout": str(output / "stdout.log"),
              "stderr": str(output / "stderr.log"),
              "screenshots": screenshots,
              "lastGuestState": str(guest / "boot-2/last-state.json")
              if (guest / "boot-2/last-state.json").is_file() else None,
              "qmpTransport": "private-stdio", "vncUsed": False,
              "networkUsed": False, "humanPasswordRequired": False,
              "syntheticKeysProveHumanConsent": False}
    receipt = output / "debug-run.json"
    receipt.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "receipt": str(receipt),
                      "gateReceipt": report["gateReceipt"],
                      "screenshots": screenshots,
                      "stderr": report["stderr"]}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
