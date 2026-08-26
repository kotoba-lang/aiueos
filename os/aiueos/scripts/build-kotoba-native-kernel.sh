#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: build-kotoba-native-kernel.sh /path/to/compiler}
expected=1de9dafe71c12b68233fb815d5cd36208a9b22a4
actual=$(git -C "$compiler" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2; exit 1;
}
out=${AIUEOS_NATIVE_OUT:-"$repo/build/aiueos-native"}
kernel="$out/KERNEL.ELF"
source=${AIUEOS_NATIVE_KERNEL_SOURCE:-"$aiueos/native/kernel.kotoba"}
second="$out/KERNEL.reproduced.ELF"
receipt="$out/receipt.json"
mkdir -p "$out"
"$compiler/bin/kotoba-compiler" compile "$source" \
  --target x86_64-aiueos-kernel-v1 --artifact image --fuel 32768 --output "$kernel"
"$compiler/bin/kotoba-compiler" compile "$source" \
  --target x86_64-aiueos-kernel-v1 --artifact image --fuel 32768 --output "$second"
cmp "$kernel" "$second"
rm -f "$second"
python3 "$aiueos/scripts/verify-kotoba-native-kernel.py" \
  "$kernel" "$source" "$expected" "$receipt"
case ${AIUEOS_TIMING_PROFILE:-best-effort} in
  best-effort) ;;
  hard-real-time)
    python3 "$aiueos/scripts/verify-rt-kernel-receipt.py" "$receipt" ;;
  *)
    echo "error: unknown AIUEOS_TIMING_PROFILE" >&2; exit 1 ;;
esac
foreign=$(find "$out" -type f \( -name '*.c' -o -name '*.o' -o -name '*.obj' -o -name '*.a' -o -name '*.so' \) \
  -print -quit)
[ -z "$foreign" ] || {
  echo "error: foreign/C artifact entered native output: $foreign" >&2; exit 1;
}
