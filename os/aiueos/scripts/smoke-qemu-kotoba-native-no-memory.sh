#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: smoke-qemu-kotoba-native-no-memory.sh /path/to/compiler}
if [ -n "${AIUEOS_NATIVE_NO_MEMORY_WORK:-}" ]; then
  tmp=$AIUEOS_NATIVE_NO_MEMORY_WORK
  mkdir -p "$tmp"
else
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/aiueos-native-no-memory.XXXXXX")
  trap 'rm -rf "$tmp"' EXIT HUP INT TERM
fi
source="$tmp/kernel-no-conventional.kotoba"

# Mechanical mutation: make the bounded descriptor walk report exhaustion at
# its first step. The allocator must emit only M and take its deterministic
# no-page exit. Check both sides so source drift cannot make this vacuous.
needle='(if (> (+ offset descriptor-size) map-size)'
replacement='(if (> (+ offset descriptor-size) -1)'
grep -F "$needle" "$aiueos/native/kernel.kotoba" >/dev/null
sed "s/$needle/$replacement/" \
  "$aiueos/native/kernel.kotoba" >"$source"
grep -F "$needle" "$source" >/dev/null && {
  echo "error: allocator mutation did not apply" >&2; exit 1;
}

if [ "${AIUEOS_NATIVE_NO_MEMORY_BUILD_ONLY:-0}" = 1 ]; then
  AIUEOS_NATIVE_KERNEL_SOURCE="$source" \
  AIUEOS_NATIVE_OUT="$tmp/native" \
  AIUEOS_NATIVE_BOOT_OUT="$tmp/boot" \
    "$aiueos/scripts/build-kotoba-native-boot.sh" "$compiler"
  echo "AIUEOS_KOTOBA_NATIVE_NO_MEMORY_BUILD_OK out=$tmp/boot"
  exit 0
fi

AIUEOS_NATIVE_KERNEL_SOURCE="$source" \
AIUEOS_NATIVE_OUT="$tmp/native" \
AIUEOS_NATIVE_BOOT_OUT="$tmp/boot" \
AIUEOS_NATIVE_EXPECT_STATUS=39 \
AIUEOS_NATIVE_EXPECT_MARKER=M \
  "$aiueos/scripts/smoke-qemu-kotoba-native.sh" "$compiler"
