#!/usr/bin/env python3
"""Exercise a fresh native Itonami Y/N choice and local model in two QEMU boots.

No VNC, guest agent, network, host model service or human key press is used.
QMP keys verify the guest decision path; they do not prove a human approved it.
"""

import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import time


HERE = Path(__file__).resolve().parent
ENTRY = "/os/aiueos/native/user-elf-v2-local-model-entry.kotoba"
ELF_SHA256 = "6d5ec74da6197788a5e778b8903cc40f001602c4c19cb4701d2620a948208e6d"
PROMPT_TITLE = "AIUEOS local model Goal? y/n"
if not __debug__:
    raise RuntimeError("the QEMU assertion gate must not run with Python -O")


def load_base():
    path = HERE / "debug-native-user-v2-durable-unattended.py"
    spec = importlib.util.spec_from_file_location("durable_guest", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def snapshot(base, qmp, directory, context):
    row = base.state(qmp, directory, context)
    work = struct.unpack("<2048I", (directory / "desktop-context.bin").read_bytes())
    surface = struct.unpack("<2048I", (directory / "surface.bin").read_bytes())
    title_words = surface[64:96]
    title_length = title_words.index(0) if 0 in title_words else len(title_words)
    row.update(pending=work[87], approved=work[88], modelToken=work[89],
               title="".join(chr(cp) for cp in title_words[:title_length]))
    return row


def wait(base, qmp, directory, context, name, predicate, timeout=65):
    deadline = time.monotonic() + timeout
    current = None
    while time.monotonic() < deadline:
        current = snapshot(base, qmp, directory, context)
        (directory / "last-state.json").write_text(json.dumps(current, indent=2) + "\n")
        if (current["magic"] == 0x31545844 and current["profile"] == 4
                and current["draftEnabled"] == 1 and current["frames"] >= 1
                and current["frames"] == current["framesStarted"]
                and predicate(current)):
            (directory / (name + ".json")).write_text(json.dumps(current, indent=2) + "\n")
            return current
        time.sleep(.2)
    (directory / "registers-last.txt").write_text(qmp.hmp("info registers"))
    raise AssertionError((name, current))


def endpoint(base, qmp, directory, code, request, elf, expected):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        registers = qmp.hmp("info registers")
        (directory / "registers-last.txt").write_text(registers)
        rip_match = re.search(r"\bRIP=([0-9a-f]+)", registers, re.I)
        cpl_match = re.search(r"\bCPL=([0-3])", registers)
        data = qmp.dump(request, 4096, "request-after.bin", directory)
        result = struct.unpack_from("<Q", data)[0]
        if rip_match and cpl_match:
            rip, cpl = int(rip_match.group(1), 16), int(cpl_match.group(1))
            if cpl == 3 and code <= rip < code + 4096 and result == expected:
                qmp.call("stop")
                code_bytes = qmp.dump(code, 4096, "code-final.bin", directory)
                final = qmp.dump(request, 4096, "request-final.bin", directory)
                assert code_bytes[:330] == elf[4096:4426]
                assert struct.unpack_from("<I", final, 316)[0] == expected
                assert struct.unpack_from("<Q", final, 8)[0] == 255
                return {"rip": rip, "cpl": cpl, "result": result,
                        "modelTokenInput": expected, "codeMatchesPinnedElf": True}
        time.sleep(.2)
    raise AssertionError("approved local model token did not reach user-v2 CPL3")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args()
    base = load_base()
    build = args.build_dir.resolve(strict=True)
    receipt = build / "build-state.json"
    if not receipt.is_file():
        parser.error("build_dir requires build-state.json")
    built = json.loads(receipt.read_text())
    if built.get("state") != "MACOS_NATIVE_IMAGE_CANDIDATE" or built.get("kernelEntry") != ENTRY:
        parser.error("build is not the local-model confirmation candidate")
    args.kernel, args.efi = build / "KERNEL.ELF", build / "BOOTX64.EFI"
    font, slot = build / "source/boot-font.bin", build / "source/boot-user-slot.bin"
    for name, path, expected in (("kernel", args.kernel, built["kernel"]),
                                 ("efi", args.efi, built["efi"]),
                                 ("font", font, built["bootFont"]),
                                 ("slot", slot, built["bootUserSlot"])):
        if path.is_symlink() or not path.is_file() or base.pin(path) != expected:
            parser.error(f"{name} differs from build receipt")
    graph = built.get("sourceGraph", {})
    for relative in ("native/user-elf-v2-local-model-entry.kotoba",
                     "native/desktop.kotoba", "native/draft_store.kotoba",
                     "native/micro_infer.kotoba"):
        source = HERE.parent / relative
        if (source.is_symlink() or not source.is_file()
                or base.pin(source) != graph.get("/os/aiueos/" + relative)):
            parser.error(f"{relative} differs from build receipt")
    user_source = HERE.parent / "native/itonami-local-model-user-v2.kotoba"
    user_elf = build / "boot-user.elf"
    if (user_source.is_symlink() or not user_source.is_file()
            or base.pin(user_source) != built.get("bootUserSource")
            or user_elf.is_symlink() or not user_elf.is_file()
            or base.pin(user_elf) != built.get("bootUserElf")):
        parser.error("Amu user source or ELF differs from build receipt")
    kernel, efi, slot_bytes = args.kernel.read_bytes(), args.efi.read_bytes(), slot.read_bytes()
    elf_size = struct.unpack_from("<Q", slot_bytes, 16)[0]
    elf = slot_bytes[64:64 + elf_size]
    if (len(slot_bytes) != 524288 or struct.unpack_from("<QQ", slot_bytes) !=
            (0x0031444c50554941, 2) or elf_size != 8280
            or any(slot_bytes[64 + elf_size:])
            or base.pin(slot)["sha256"] != built["bootUserSlot"]["sha256"]
            or base.hashlib.sha256(elf).hexdigest() != ELF_SHA256
            or elf != user_elf.read_bytes()
            or efi.count(kernel) != 1 or efi.count(slot_bytes) != 1
            or efi.count(font.read_bytes()) != 1):
        parser.error("kernel, EFI or user-v2 model token payload differs")
    header = struct.unpack_from("<IIQQQQQQ", kernel, 120)
    if header[:4] != (1, 6, 1048576, 0x200000):
        parser.error("unexpected native context layout")
    context, code, request = header[3], header[3] + 0x20000, header[3] + 0x21000
    qemu_name = args.qemu or shutil.which("qemu-system-x86_64")
    if qemu_name is None:
        parser.error("QEMU missing; pass --qemu")
    args.qemu = Path(qemu_name).resolve(strict=True)
    args.firmware = (args.firmware or args.qemu.parent.parent / "share/qemu/edk2-x86_64-code.fd").resolve(strict=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = (args.output or build.parent / f"{build.name}-model-debug-{stamp}-{os.getpid()}").resolve()
    if output.exists():
        parser.error("output must be a new private directory")
    output.mkdir(mode=0o700)
    disk = output / "draft.raw"
    blank = bytearray(128 * 512)
    struct.pack_into("<4I", blank, 0, 0x49415741, 0x54465244, 1, 128)
    disk.write_bytes(blank)
    goal, expected_token = "awai a", ord("i")
    boots = []
    for ordinal in (1, 2):
        directory = output / f"boot-{ordinal}"
        process, qmp, log = base.boot(args, directory, disk)
        try:
            expected_goal = goal if ordinal == 2 else ""
            current = wait(base, qmp, directory, context, "boot",
                           lambda s: s["goal"] == base.PREFIX + expected_goal
                           and s["restore"] == ordinal - 1 and s["pending"] == 0
                           and s["approved"] == 0 and s["modelToken"] == 0)
            base.key(qmp, "2")
            current = wait(base, qmp, directory, context, "open",
                           lambda s: s["focus"] == 2 and s["keys"] > current["keys"])
            if ordinal == 1:
                for index, letter in enumerate(goal, 1):
                    before = current["keys"]
                    base.key(qmp, letter)
                    current = wait(base, qmp, directory, context, f"edit-{index}",
                                   lambda s, i=index, p=before:
                                   s["goal"] == base.PREFIX + goal[:i] and s["keys"] > p)
                before = current["keys"]
                base.key(qmp, "f5")
                committed = wait(base, qmp, directory, context, "commit",
                                 lambda s: s["commit"] == 1 and s["generation"] == 1
                                 and s["keys"] > before)
                boots.append({"boot": 1, "committed": committed})
            else:
                assert current["generation"] == 1
                visible = base.screenshot(qmp, directory, font.read_bytes(), goal, "restored-goal")
                base.key(qmp, "ret")
                pending = wait(base, qmp, directory, context, "pending",
                               lambda s: s["pending"] == 1 and s["approved"] == 0
                               and s["modelToken"] == 0 and s["title"] == PROMPT_TITLE)
                assert qmp.dump(request, 4096, "request-pending.bin", directory)[256:320] == bytes(64)
                assert qmp.dump(code, 4096, "code-pending.bin", directory)[:330] == bytes(330)
                approval_ppm = directory / "approval.ppm"
                approval_png = directory / "approval.png"
                qmp.call("screendump", {"filename": str(approval_ppm)})
                base.import_file("desktop_pixels", "check-native-desktop-qemu.py").ppm_png(
                    approval_ppm, approval_png)
                base.key(qmp, "n")
                denied = wait(base, qmp, directory, context, "denied",
                              lambda s: s["pending"] == 0 and s["approved"] == 0
                              and s["submitted"] == 0 and s["modelToken"] == 0
                              and s["title"] == "Itonami")
                assert qmp.dump(request, 4096, "request-denied.bin", directory)[256:320] == bytes(64)
                assert qmp.dump(code, 4096, "code-denied.bin", directory)[:330] == bytes(330)
                registers = qmp.hmp("info registers")
                assert re.search(r"\bCPL=0\b", registers)
                base.key(qmp, "ret")
                wait(base, qmp, directory, context, "pending-again",
                     lambda s: s["pending"] == 1 and s["title"] == PROMPT_TITLE)
                base.key(qmp, "y")
                user = endpoint(base, qmp, directory, code, request, elf, expected_token)
                after = snapshot(base, qmp, directory, context)
                assert after["approved"] == 1 and after["modelToken"] == expected_token
                boots.append({"boot": 2, "restored": current, "pending": pending,
                              "denied": denied, "screenshot": visible,
                              "approvalScreenshot": {"image": str(approval_png),
                                                     "pin": base.pin(approval_png)},
                              "userEndpoint": user})
        finally:
            base.close(process, qmp, log)
        if ordinal == 1:
            first = disk.read_bytes()
            assert first != bytes(blank) and first[:512] == blank[:512]
            assert first[2048:] == blank[2048:]
        else:
            assert disk.read_bytes() == first, "model operation changed the Goal draft disk"
    # A changed code byte in the sealed slot must fail before the guest copies
    # either user page or computes the local model. Keep the original EFI and
    # the marked disk immutable across this independent third boot.
    original_efi = args.efi
    changed = bytearray(efi)
    changed[efi.index(slot_bytes) + 64 + 4096 + 20] ^= 1
    assert sum(a != b for a, b in zip(efi, changed)) == 1
    modified_efi = output / "modified-slot.EFI"
    modified_efi.write_bytes(changed)
    args.efi = modified_efi
    directory = output / "modified-slot"
    process, qmp, log = base.boot(args, directory, disk)
    try:
        wait(base, qmp, directory, context, "boot",
             lambda s: s["goal"] == base.PREFIX + goal and s["restore"] == 1)
        base.key(qmp, "2")
        wait(base, qmp, directory, context, "open", lambda s: s["focus"] == 2)
        base.key(qmp, "ret")
        wait(base, qmp, directory, context, "pending",
             lambda s: s["pending"] == 1 and s["title"] == PROMPT_TITLE)
        base.key(qmp, "y")
        samples, last_rip = 0, None
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline and samples < 12:
            registers = qmp.hmp("info registers")
            rip_match = re.search(r"\bRIP=([0-9a-f]+)", registers, re.I)
            cpl_match = re.search(r"\bCPL=([0-3])", registers)
            page = qmp.dump(request, 4096, "request-refused.bin", directory)
            code_page = qmp.dump(code, 4096, "code-refused.bin", directory)
            current = snapshot(base, qmp, directory, context)
            if rip_match and cpl_match:
                rip, cpl = int(rip_match.group(1), 16), int(cpl_match.group(1))
                assert cpl != 3, "modified user ELF reached CPL3"
                if (cpl == 0 and page[:320] == bytes(320)
                        and code_page[:330] == bytes(330) and current["modelToken"] == 0):
                    samples = samples + 1 if rip == last_rip else 1
                    last_rip = rip
                else:
                    samples = 0
            time.sleep(.2)
        assert samples >= 12, "modified slot refusal endpoint unavailable"
        modified = {"cpl": 0, "stableRip": last_rip, "samples": samples,
                    "userPagesUnchanged": True, "modelNotRun": True}
    finally:
        base.close(process, qmp, log)
        args.efi = original_efi
    assert disk.read_bytes() == first, "modified slot boot changed the draft disk"
    report = {"schema": "aiueos.native-local-model-confirm-qemu.v1",
              "status": "PASS_NATIVE_LOCAL_MODEL_DENY_APPROVE_CPL3_AFTER_RESTORE",
              "build": str(build), "kernel": base.pin(args.kernel), "efi": base.pin(args.efi),
              "slot": base.pin(slot), "font": base.pin(font),
              "userSource": base.pin(user_source), "userElf": base.pin(user_elf),
              "qemu": base.pin(args.qemu), "firmware": base.pin(args.firmware),
              "observer": base.pin(Path(__file__)), "userElfSha256": ELF_SHA256,
              "goal": goal, "expectedToken": expected_token, "boots": boots,
              "modifiedSlotRefusal": modified,
              "scratchDisk": base.pin(disk), "qmpTransport": "private-stdio",
              "vncUsed": False, "networkUsed": False, "humanPasswordRequired": False,
              "syntheticKeysProveHumanConsent": False,
              "externalOperation": False, "agentLoop": False,
              "D2WorkspaceCodec": False, "signedHeadAuthority": False,
              "releaseAdmission": False, "physicalHardware": False}
    path = output / "debug-summary.json"
    path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "receipt": str(path),
                      "token": expected_token, "screen": boots[1]["screenshot"]["image"]}))


if __name__ == "__main__":
    sys.exit(main())
