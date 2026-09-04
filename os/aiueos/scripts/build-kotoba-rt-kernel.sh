#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: build-kotoba-rt-kernel.sh /path/to/amu}
expected=6cc43b74aeba0b9e3dd7fb7fe09d0f8d2d580fbf
actual=$(git -C "$compiler" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2; exit 1;
}
out=${AIUEOS_KOTOBA_RT_OUT:-"$repo/build/aiueos-kotoba-rt"}
source="$aiueos/native/rt-kernel.kotoba"
root_source="$aiueos/native/rt-system.kotoba"
compile_script="$aiueos/scripts/aiueos/compile_kotoba_rt_system.clj"
kernel="$out/KERNEL.ELF"
second="$out/KERNEL.reproduced.ELF"
receipt="$out/receipt.json"
mkdir -p "$out"
compile_system() {
  (cd "$compiler" && clojure \
    -Sdeps "{:paths [\"$compiler/src\" \"$aiueos/scripts\"]}" \
    -M -m aiueos.compile-kotoba-rt-system \
    "$source" "$aiueos/kotoba/sha256.kotoba" \
    "$aiueos/kotoba/ecdsa-p256.kotoba" \
    "$aiueos/kotoba/plc-user-elf-valid.kotoba" \
    "$root_source" "$1")
}
compile_system "$kernel"
compile_system "$second"
cmp "$kernel" "$second"
rm -f "$second"
python3 "$aiueos/scripts/verify-kotoba-rt-kernel.py" \
  "$kernel" "$source" "$expected" "$receipt" \
  "$root_source" "$aiueos/kotoba/sha256.kotoba" \
  "$aiueos/kotoba/ecdsa-p256.kotoba" \
  "$aiueos/kotoba/plc-user-elf-valid.kotoba"
foreign=$(find "$out" -type f \( -name '*.c' -o -name '*.o' -o -name '*.obj' \
  -o -name '*.a' -o -name '*.so' \) -print -quit)
[ -z "$foreign" ] || {
  echo "error: C/foreign artifact entered Kotoba RT output: $foreign" >&2; exit 1;
}
