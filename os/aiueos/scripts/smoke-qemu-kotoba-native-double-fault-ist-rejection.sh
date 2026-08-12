#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: smoke-qemu-kotoba-native-double-fault-ist-rejection.sh /path/to/compiler}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/aiueos-native-double-fault-ist-reject.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
source="$tmp/kernel-double-fault-ist-alias.kotoba"

needle='(- eleventh-start 4096) 4096)) 1)'
[ "$(grep -F -c "$needle" "$aiueos/native/kernel.kotoba")" = 1 ] || {
  echo "error: IST page source is ambiguous" >&2; exit 1;
}
sed 's/(- eleventh-start 4096) 4096)) 1)/(- seventh-start 4096) 4096)) 1)/' \
  "$aiueos/native/kernel.kotoba" >"$source"

AIUEOS_NATIVE_KERNEL_SOURCE="$source" \
AIUEOS_NATIVE_OUT="$tmp/native" \
AIUEOS_NATIVE_BOOT_OUT="$tmp/boot" \
AIUEOS_NATIVE_EXPECT_STATUS=49 \
AIUEOS_NATIVE_EXPECT_MARKER=M \
  "$aiueos/scripts/smoke-qemu-kotoba-native.sh" "$compiler"

echo "AIUEOS_KOTOBA_NATIVE_DOUBLE_FAULT_IST_REJECTION_OK marker=M status=49 reason=receipt-ist-alias"
