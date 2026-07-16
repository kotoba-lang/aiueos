#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}

# The release-image smoke uses the same explicitly test-only input event and
# catalog-policy self-test as the direct UEFI smoke. Normal release builds
# leave both compile gates disabled.
AIUEOS_INPUT_SMOKE_SYNTHETIC=1 AIUEOS_CATALOG_POLICY_SELFTEST=1 \
  "$aiueos/scripts/build-release-image.sh" >/dev/null

# Fail-closed: a single flipped byte in the embedded ISO kernel must be
# rejected by the release verifier before any boot evidence is claimed.
python3 - "$out/aiueos-x86_64.iso" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1])
corrupt = source.with_name("corrupt-release.iso")
data = bytearray(source.read_bytes())
data[23 * 2048 + 300 * 512] ^= 1
corrupt.write_bytes(data)
PY
if python3 "$aiueos/scripts/make-release-image.py" verify \
  --iso "$out/corrupt-release.iso" \
  --efi "$out/esp/EFI/BOOT/BOOTX64.EFI" \
  --kernel "$out/esp/EFI/AIUEOS/KERNEL.ELF" >/dev/null 2>&1; then
  echo "error: corrupted release ISO was not rejected by the verifier" >&2
  exit 1
fi
rm -f "$out/corrupt-release.iso"
echo "AIUEOS_RELEASE_ISO_REJECTION_OK corrupted-kernel-byte"

# GPT raw-disk boot. The journal smoke mutates the virtio-blk data disk, so a
# pristine copy is staged before each boot.
cp "$out/aiueos-x86_64-data.img" "$out/virtio-blk-smoke.img"
AIUEOS_DISK_IMAGE="$out/aiueos-x86_64-gpt.img" \
  AIUEOS_PRESERVE_BLK_IMAGE=1 \
  "$aiueos/scripts/smoke-qemu-uefi.sh"

# El Torito UEFI boot from the release ISO.
cp "$out/aiueos-x86_64-data.img" "$out/virtio-blk-smoke.img"
AIUEOS_CDROM_IMAGE="$out/aiueos-x86_64.iso" \
  AIUEOS_PRESERVE_BLK_IMAGE=1 \
  "$aiueos/scripts/smoke-qemu-uefi.sh"
echo "AIUEOS_RELEASE_MEDIA_SMOKE_OK gpt iso"
