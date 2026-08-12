#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: smoke-qemu-kotoba-native-double-fault-ist.sh /path/to/compiler}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/aiueos-native-double-fault-ist.XXXXXX")
trap 'if [ "${AIUEOS_KEEP_TMP:-0}" != 1 ]; then rm -rf "$tmp"; else echo "kept $tmp" >&2; fi' EXIT HUP INT TERM
source="$tmp/kernel-double-fault-ist.kotoba"

install='                                   (= (install-page-fault-idt reusable-page) 1))'
probe='(+ 424201 41)'
[ "$(grep -F -c "$install" "$aiueos/native/kernel.kotoba")" = 1 ] || {
  echo "error: IDT installation source is ambiguous" >&2; exit 1;
}
[ "$(grep -F -c "$probe" "$aiueos/native/kernel.kotoba")" = 1 ] || {
  echo "error: double-fault probe source is ambiguous" >&2; exit 1;
}
sed -e 's/                                   (= (install-page-fault-idt reusable-page) 1))/                                   (= (install-page-fault-idt reusable-page) 1)\
                                   (= (install-double-fault-idt reusable-page) 1))/' \
    -e 's/(+ 424201 41)/(kernel-probe-double-fault)/' \
    "$aiueos/native/kernel.kotoba" >"$source"

AIUEOS_NATIVE_KERNEL_SOURCE="$source" \
AIUEOS_NATIVE_OUT="$tmp/native" \
AIUEOS_NATIVE_BOOT_OUT="$tmp/boot" \
AIUEOS_NATIVE_EXPECT_STATUS=57 \
AIUEOS_NATIVE_EXPECT_MARKER=MPRCDD \
  "$aiueos/scripts/smoke-qemu-kotoba-native.sh" "$compiler"

echo "AIUEOS_KOTOBA_NATIVE_DOUBLE_FAULT_IST_OK marker=MPRCDD status=57 source=failed-pf-delivery tss-ist1 cpu-frame"
