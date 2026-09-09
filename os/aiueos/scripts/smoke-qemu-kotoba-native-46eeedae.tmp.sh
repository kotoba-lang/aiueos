#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: smoke-qemu-kotoba-native.sh /path/to/compiler}
boot_out=${AIUEOS_NATIVE_BOOT_OUT:-"$repo/build/aiueos-native-boot"}
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
qemu_timeout=${AIUEOS_QEMU_TIMEOUT:-300}
expected_status=${AIUEOS_NATIVE_EXPECT_STATUS:-33}
# The guest's own boot markers are MPRCD. Under --k16-preflight the LOADER
# prefixes its tender stages, and asserting them is the point: P (the PCI
# probe), S (the sealed budget snapshotted), T (top of the tender loop), C
# (about to call the guest), then the guest's MPRCD.
#
# Until 2026-09-09 QEMU could not reach the tender at all -- no RTL8125, so the
# preflight image branched to :exit-boot and took the ordinary halting entry.
# `exit 33 / MPRCD` was green for a run in which the changed code was never
# executed, which is this workspace's seventh question asked of itself and
# answered wrong for a week. Four defects found on hardware in that time were
# pure control flow with no packet in them.
#
# So this string is a claim about the PATH, not just the outcome. If the tender
# stops being taken, PSTC disappears and this goes red.
#
# ⚠ The preflight form is a PREFIX plus a required substring, not an equality.
# Measured 2026-09-09: two runs of the same image gave PSTCMPRCD and
# PSTCMPRCDF. The trailing F is the GUEST's nic marker (`serial/trace-byte 70`
# when nic-status is non-zero but not negative) and it depends on how QEMU
# enumerates PCI that run. An equality assertion would have pinned a detail
# that is not the claim, and gone red for the wrong reason.
if [ "${AIUEOS_NATIVE_K16_PREFLIGHT:-0}" = 1 ]; then
  expected_marker=${AIUEOS_NATIVE_EXPECT_MARKER:-PSTC+MPRCD}
else
  expected_marker=${AIUEOS_NATIVE_EXPECT_MARKER:-MPRCD}
fi
"$aiueos/scripts/build-kotoba-native-boot-46eeedae.tmp.sh" "$compiler" >/dev/null
if [ -z "${OVMF_CODE:-}" ]; then
  for candidate in /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
    /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/x64/OVMF_CODE.fd; do
    if [ -f "$candidate" ]; then OVMF_CODE=$candidate; break; fi
  done
fi
[ -f "${OVMF_CODE:-}" ] || { echo "error: OVMF firmware not found" >&2; exit 1; }
if [ -z "${OVMF_VARS:-}" ]; then
  for candidate in /usr/share/OVMF/OVMF_VARS_4M.fd \
    /usr/share/OVMF/OVMF_VARS.fd; do
    if [ -f "$candidate" ]; then OVMF_VARS=$candidate; break; fi
  done
fi
log="$boot_out/kotoba-native-debug.log"
rm -f "$log"
set -- -machine q35,accel="${AIUEOS_QEMU_ACCEL:-tcg}" -cpu "${AIUEOS_QEMU_CPU:-max}" -m 128M -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"
if [ -n "${OVMF_VARS:-}" ]; then
  vars_copy="$boot_out/OVMF_VARS.fd"
  cp "$OVMF_VARS" "$vars_copy"
  set -- "$@" -drive if=pflash,format=raw,file="$vars_copy"
fi
set -- "$@" \
  -drive "format=raw,file=fat:rw:$boot_out/esp" \
  -device isa-debugcon,iobase=0xe9,chardev=debug \
  -chardev file,id=debug,path="$log" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -display none -serial none -no-reboot
set +e
timeout "$qemu_timeout" "$qemu" "$@"
qemu_status=$?
set -e
[ "$qemu_status" = "$expected_status" ] || {
  echo "error: Kotoba-native QEMU exit was $qemu_status, expected $expected_status" >&2; exit 1;
}
python3 - "$log" "$expected_marker" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
spec = sys.argv[2]
if "+" in spec:
    # "<prefix>+<contains>": the loader's tender stages must lead, and the
    # guest's own markers must appear. What follows them is the guest's
    # business and varies with QEMU's PCI enumeration.
    prefix, contains = (part.encode("ascii") for part in spec.split("+", 1))
    if not data.startswith(prefix):
        raise SystemExit(f"error: marker {data!r} does not start with {prefix!r} "
                         "-- the tender was not taken")
    if contains not in data:
        raise SystemExit(f"error: marker {data!r} lacks {contains!r} "
                         "-- the guest did not reach its boot markers")
else:
    if data != spec.encode("ascii"):
        raise SystemExit(f"error: Kotoba-native marker was {data!r}, expected {spec!r}")
PY
if [ "$expected_marker" = MPRCD ] || [ "$expected_marker" = "PSTC+MPRCD" ]; then
  echo "AIUEOS_KOTOBA_NATIVE_QEMU_OK no-c-boot-chain memory-map-v2 allocator-pages=14 ownership-bitmap page-table-root identity-1g rtl8125-no-device-bounded guard-unmapped text-rx state-rw-nx nxe cr0-wp cr3-activated invlpg idt14-sidt-readback recovery-frame dedicated-handler-stack reuse double-free-rejected zero-before-publish exit-boot-services"
else
  echo "AIUEOS_KOTOBA_NATIVE_QEMU_REJECTION_OK marker=$expected_marker status=$expected_status"
fi
