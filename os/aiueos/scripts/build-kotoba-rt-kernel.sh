#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: build-kotoba-rt-kernel.sh /path/to/amu}
expected=1e94e5efef7ba72519f107604979c2cd2884090e
actual=$(git -C "$compiler" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2; exit 1;
}
out=${AIUEOS_KOTOBA_RT_OUT:-"$repo/build/aiueos-kotoba-rt"}
source="$aiueos/native/rt-kernel.kotoba"
kernel="$out/KERNEL.ELF"
second="$out/KERNEL.reproduced.ELF"
receipt="$out/receipt.json"
mkdir -p "$out"
"$compiler/bin/kotoba-compiler" compile "$source" \
  --target x86_64-aiueos-kernel-v1 --artifact image --fuel 32768 --output "$kernel"
"$compiler/bin/kotoba-compiler" compile "$source" \
  --target x86_64-aiueos-kernel-v1 --artifact image --fuel 32768 --output "$second"
cmp "$kernel" "$second"
rm -f "$second"
python3 "$aiueos/scripts/verify-kotoba-rt-kernel.py" \
  "$kernel" "$source" "$expected" "$receipt"
foreign=$(find "$out" -type f \( -name '*.c' -o -name '*.o' -o -name '*.obj' \
  -o -name '*.a' -o -name '*.so' \) -print -quit)
[ -z "$foreign" ] || {
  echo "error: C/foreign artifact entered Kotoba RT output: $foreign" >&2; exit 1;
}
