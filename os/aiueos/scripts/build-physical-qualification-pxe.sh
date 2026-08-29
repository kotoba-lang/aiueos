#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos-physical-qualification-pxe"}
core_out="$out/core"
efi="$out/aiueos-k16-native-pxe.efi"
receipt="$out/aiueos-k16-native-pxe-receipt.json"

source_commit=$(git -C "$repo" rev-parse HEAD)
source_dirty=false
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
  source_dirty=true
fi
if [ "$source_dirty" = true ] &&
   [ "${AIUEOS_ALLOW_DIRTY_QUALIFICATION_BUILD:-0}" != 1 ]; then
  echo "error: qualification release must be built from a clean tracked tree" >&2
  exit 1
fi

mkdir -p "$out"
AIUEOS_OUT="$core_out" \
AIUEOS_PHYSICAL_QUALIFICATION=1 \
AIUEOS_PHYSICAL_NETWORK_QUALIFICATION=${AIUEOS_PHYSICAL_NETWORK_QUALIFICATION:-0} \
AIUEOS_PERSISTENT_BOOT=${AIUEOS_PERSISTENT_BOOT:-0} \
AIUEOS_EMBEDDED_RELEASE=1 \
AIUEOS_NETBOOT_QUALIFICATION=1 \
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0} \
  "$aiueos/scripts/build-uefi.sh" >/dev/null
cp "$core_out/esp/EFI/BOOT/BOOTX64.EFI" "$efi"

AIUEOS_SOURCE_COMMIT="$source_commit" AIUEOS_SOURCE_DIRTY="$source_dirty" \
AIUEOS_PERSISTENT_BOOT=${AIUEOS_PERSISTENT_BOOT:-0} python3 - \
  "$efi" "$core_out/esp/EFI/AIUEOS/KERNEL.ELF" \
  "$core_out/esp/EFI/AIUEOS/INITRD.IMG" "$receipt" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import sys

efi, kernel, initramfs, receipt = map(Path, sys.argv[1:])

def artifact(path):
    payload = path.read_bytes()
    return {"bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}

document = {
    "schema": "aiueos.physical-qualification-pxe-receipt.v1",
    "source": {
        "commit": os.environ["AIUEOS_SOURCE_COMMIT"],
        "dirty": os.environ["AIUEOS_SOURCE_DIRTY"] == "true",
    },
    "artifact": artifact(efi),
    "embedded": {
        "kernel": artifact(kernel),
        "initramfs": artifact(initramfs),
    },
    "return": {
        "bootnext": ("current-pxe-persistent" if
                     os.environ["AIUEOS_PERSISTENT_BOOT"] == "1" else
                     "current-pxe-one-shot"),
        "result": "uefi-nvram-16-bytes",
        "watchdog": ("disabled" if
                     os.environ["AIUEOS_PERSISTENT_BOOT"] == "1" else
                     "90-second-recovery"),
    },
    "safety": {
        "internal-disk-writes": False,
        "boot-order-writes": False,
        "ssd-install": False,
    },
}
receipt.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n",
                   encoding="ascii")
PY

printf '%s\n' "$efi"
