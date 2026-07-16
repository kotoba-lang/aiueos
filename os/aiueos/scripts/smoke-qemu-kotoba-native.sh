#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: smoke-qemu-kotoba-native.sh /path/to/compiler}
boot_out=${AIUEOS_NATIVE_BOOT_OUT:-"$repo/build/aiueos-native-boot"}
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
"$aiueos/scripts/build-kotoba-native-boot.sh" "$compiler" >/dev/null
if [ -z "${OVMF_CODE:-}" ]; then
  for candidate in /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
    /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/x64/OVMF_CODE.fd; do
    if [ -f "$candidate" ]; then OVMF_CODE=$candidate; break; fi
  done
fi
[ -f "${OVMF_CODE:-}" ] || { echo "error: OVMF firmware not found" >&2; exit 1; }
log="$boot_out/kotoba-native-debug.log"
rm -f "$log"
set +e
"$qemu" -machine q35,accel=tcg -cpu max -m 128M -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive "format=raw,file=fat:rw:$boot_out/esp" \
  -device isa-debugcon,iobase=0xe9,chardev=debug \
  -chardev file,id=debug,path="$log" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -display none -serial none -no-reboot
qemu_status=$?
set -e
[ "$qemu_status" = 33 ] || {
  echo "error: Kotoba-native QEMU exit was $qemu_status, expected 33" >&2; exit 1;
}
python3 - "$log" <<'PY'
from pathlib import Path
import sys
data=Path(sys.argv[1]).read_bytes()
if not data.endswith(b"N"):
    raise SystemExit("error: Kotoba-native privileged marker missing")
PY
echo "AIUEOS_KOTOBA_NATIVE_QEMU_OK no-c-boot-chain exit-boot-services cr3 port-io"
