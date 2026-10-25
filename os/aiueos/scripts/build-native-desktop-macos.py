#!/usr/bin/env python3
"""Build AIUEOS on Apple Silicon and boot its exact media without a login.

Python stages, pins and observes. Amu/Kotoba native programs emit the font,
kernel, EFI and GPT/FAT16 media; QEMU QMP stdio checks the guest desktop.
The output is a diagnostic VM candidate, never an installer or release.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys


HERE = Path(__file__).resolve().parent
OS = HERE.parent
OWNER_ENV = {
    "capability-link-frame": "AIUEOS_LINK_FRAME_SOURCE_PATH",
    "capability-dma-map": "AIUEOS_DMA_MAP_SOURCE_PATH",
    "capability-mmio-map": "AIUEOS_MMIO_MAP_SOURCE_PATH",
    "capability-net-transport": "AIUEOS_NET_TRANSPORT_SOURCE_PATH",
    "org-ietf-tcp": "AIUEOS_ORG_IETF_TCP_SOURCE_PATH",
}
FUEL = 9007199254740991


def pin(path):
    raw = path.read_bytes()
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def verified_file(path, expected=None):
    if path.is_symlink() or not path.is_file():
        raise ValueError("input must be a regular non-symlink file: " + str(path))
    actual = pin(path)
    if expected and actual["sha256"] != expected:
        raise ValueError("input digest mismatch: " + path.name)
    return actual


def verify_macho(path, expected):
    actual = verified_file(path, expected)
    raw = path.read_bytes()
    if len(raw) < 32 or struct.unpack_from("<IiiI", raw) != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError("expected signed arm64 Mach-O: " + path.name)
    subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(path)],
                   check=True, capture_output=True)
    linked = subprocess.run(["/usr/bin/otool", "-L", str(path)],
                            check=True, capture_output=True).stdout.splitlines()
    if linked != [f"{path}:".encode()]:
        raise ValueError("undeclared dylib dependency: " + path.name)
    return actual


def main(args):
    if platform.system() != "Darwin" or platform.machine() not in ("arm64", "aarch64"):
        raise ValueError("the integrated Mac gate requires Apple Silicon")
    if args.amu_source.is_symlink() or args.owner_repo_root.is_symlink():
        raise ValueError("source roots must not be symlinks")
    amu = args.amu_source.resolve(strict=True)
    owners = args.owner_repo_root.resolve(strict=True)
    if not amu.is_dir() or not owners.is_dir():
        raise ValueError("source roots must be directories")
    compiler = args.compiler_image.resolve(strict=True)
    packager = args.packager_image.resolve(strict=True)
    qemu = args.qemu.resolve(strict=True)
    firmware = args.firmware.resolve(strict=True)
    font_source = args.font.resolve(strict=True)
    compiler_pin = verify_macho(compiler, args.compiler_sha256)
    packager_pin = verify_macho(packager, args.packager_sha256)
    qemu_pin = verified_file(qemu, args.qemu_sha256)
    firmware_pin = verified_file(firmware, args.firmware_sha256)
    font_pin = verified_file(font_source)
    if not os.access(qemu, os.X_OK):
        raise ValueError("QEMU is not executable")
    source_root_pins = {}
    env = {"PATH": "/usr/bin:/bin:/opt/homebrew/bin", "LANG": "C"}
    for name, variable in OWNER_ENV.items():
        root = owners / name / "kotoba"
        if root.is_symlink() or not root.is_dir():
            raise ValueError("missing canonical owner source: " + name)
        env[variable] = str(root)
        source_root_pins[name] = root.name
    scripts = {
        name: HERE / name for name in (
            "build-kotoba-native-desktop.sh", "build-native-desktop-macos.py",
            "build-native-font-macos.py", "build-native-kernel-macos.py",
            "build-native-media-macos.py", "check-native-desktop-qemu.py",
            "check-native-media-format.py",
        )
    }
    script_pins = {name: verified_file(path) for name, path in scripts.items()}
    owner_manifest = verified_file(OS / "native/source-dependencies.json")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False, mode=0o700)
    # QEMU's snapshot overlay uses the host temporary directory. Keep it
    # inside this private task output so no global /var/tmp grant is needed.
    env["TMPDIR"] = str(output)
    env["TMP"] = str(output)
    env["TEMP"] = str(output)
    state = {
        "schema": "aiueos.macos-native-desktop-qemu-candidate.v1",
        "state": "STAGING", "host": [platform.system(), platform.machine()],
        "compiler": compiler_pin, "packager": packager_pin,
        "qemu": qemu_pin, "firmware": firmware_pin, "fontSource": font_pin,
        "ownerManifest": owner_manifest, "ownerNames": sorted(source_root_pins),
        "scripts": script_pins, "fuel": FUEL, "calls": [],
        "linuxJVMNodeOrCCompiler": False, "releaseQualified": False,
        "physicalHardwareQualified": False,
    }

    def save():
        (output / "native-desktop-macos.json").write_text(json.dumps(state, indent=2) + "\n")

    def call(name, script, argv):
        result = subprocess.run([sys.executable, str(script), *map(str, argv)],
                                cwd=output, env=env, capture_output=True, timeout=3600)
        stdout = output / (name + ".stdout")
        stderr = output / (name + ".stderr")
        stdout.write_bytes(result.stdout)
        stderr.write_bytes(result.stderr)
        state["calls"].append({"name": name, "exit": result.returncode,
                               "stdout": pin(stdout), "stderr": pin(stderr)})
        save()
        if result.returncode or result.stderr:
            state["state"] = "BLOCKED_" + name.upper()
            save()
            raise ValueError(f"{name} exited {result.returncode}: "
                             + result.stderr.decode(errors="replace")[-800:])
        print(name, "PASS", flush=True)

    save()
    try:
        font = output / "font"
        kernel = output / "kernel"
        media = output / "media"
        guest = output / "qemu"
        call("font", scripts["build-native-font-macos.py"], [
            "--amu-source", amu, "--compiler-image", compiler,
            "--compiler-sha256", args.compiler_sha256,
            "--packager-image", packager, "--packager-sha256", args.packager_sha256,
            "--font", font_source, "--output", font])
        font_state = json.loads((font / "native-font.json").read_text())
        padded = font / "aiueos-padded.fnt"
        if (font_state["state"] != "MACOS_NATIVE_FONT_CANDIDATE"
                or font_state["compiler"] != compiler_pin
                or font_state["packager"] != packager_pin
                or pin(padded) != font_state["padded"]):
            raise ValueError("font build receipt mismatch")

        call("kernel", scripts["build-native-kernel-macos.py"], [
            "--compiler-image", compiler, "--compiler-sha256", args.compiler_sha256,
            "--kernel-source", OS / "native/desktop-entry.kotoba",
            "--fuel", FUEL, "--payload", padded,
            "--payload-sha256", font_state["padded"]["sha256"], "--output", kernel])
        kernel_state = json.loads((kernel / "build-state.json").read_text())
        efi = kernel / "BOOTX64.EFI"
        image = kernel / "KERNEL.ELF"
        if (kernel_state["state"] != "MACOS_NATIVE_IMAGE_CANDIDATE"
                or kernel_state["sourceModuleCount"] != 31
                or kernel_state["fuel"] != FUEL
                or kernel_state["compiler"] != compiler_pin
                or pin(efi) != kernel_state["efi"] or pin(image) != kernel_state["kernel"]
                or kernel_state["bootPayload"] != font_state["padded"]):
            raise ValueError("kernel or EFI build receipt mismatch")

        call("media", scripts["build-native-media-macos.py"], [
            "--amu-source", amu, "--compiler-image", compiler,
            "--compiler-sha256", args.compiler_sha256,
            "--packager-image", packager, "--packager-sha256", args.packager_sha256,
            "--efi", efi, "--efi-sha256", kernel_state["efi"]["sha256"],
            "--output", media])
        media_state = json.loads((media / "native-media.json").read_text())
        disk = media / "awai-native.img"
        if (media_state["state"] != "MACOS_NATIVE_MEDIA_CANDIDATE"
                or media_state["compiler"] != compiler_pin
                or media_state["packager"] != packager_pin
                or media_state["efi"] != kernel_state["efi"]
                or pin(disk) != media_state["image"]):
            raise ValueError("media build receipt mismatch")

        call("qemu", scripts["check-native-desktop-qemu.py"], [
            "--media", disk, "--efi", efi, "--kernel", image, "--font", padded,
            "--qemu", qemu, "--firmware", firmware, "--output", guest])
        observed = json.loads((guest / "native-desktop-qemu.json").read_text())
        if (observed["status"] != "PASS_NATIVE_DESKTOP_INPUT_QEMU"
                or len(observed["events"]) != 12
                or observed["kernel"] != kernel_state["kernel"]
                or observed["efi"] != kernel_state["efi"]
                or observed["media"] != media_state["image"]
                or observed["font"] != font_state["padded"]
                or observed["firmware"] != firmware_pin
                or observed["observer"] != script_pins["check-native-desktop-qemu.py"]):
            raise ValueError("QEMU guest observation is not bound to this build")
        if (pin(compiler) != compiler_pin or pin(packager) != packager_pin
                or pin(qemu) != qemu_pin or pin(firmware) != firmware_pin
                or pin(font_source) != font_pin
                or pin(OS / "native/source-dependencies.json") != owner_manifest
                or any(pin(scripts[name]) != identity for name, identity in script_pins.items())):
            raise ValueError("tool or source changed during integrated build")
        state.update(state="MACOS_NATIVE_DESKTOP_QEMU_CANDIDATE",
                     font=font_state["padded"], kernel=kernel_state["kernel"],
                     efi=kernel_state["efi"], media=media_state["image"],
                     guestEvents=len(observed["events"]),
                     guestInputQualified=True, nativeFormatEmission=True,
                     qualification="Mac native build and QEMU desktop input; release and physical gates separate")
        save()
        print(state["state"], state["media"]["sha256"])
    except Exception as error:
        if not state["state"].startswith("BLOCKED_"):
            state["state"] = "BLOCKED_INTEGRATED_BUILD"
        state["error"] = str(error)
        save()
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--amu-source", type=Path, required=True)
    parser.add_argument("--owner-repo-root", type=Path, required=True)
    parser.add_argument("--compiler-image", type=Path, required=True)
    parser.add_argument("--compiler-sha256", required=True)
    parser.add_argument("--packager-image", type=Path, required=True)
    parser.add_argument("--packager-sha256", required=True)
    parser.add_argument("--font", type=Path, default=OS / "fonts/aiueos-16.fnt")
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--qemu-sha256", required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--firmware-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    main(parser.parse_args())
