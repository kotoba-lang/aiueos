#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
compiler=${1:?usage: build-kotoba-native-boot.sh /path/to/compiler}
expected=6d14f1ab0dc8c296a25a3b41a774423a829239eb
actual=$(git -C "$compiler" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2; exit 1;
}
native_out=${AIUEOS_NATIVE_OUT:-"$repo/build/aiueos-native"}
out=${AIUEOS_NATIVE_BOOT_OUT:-"$repo/build/aiueos-native-boot"}
efi="$out/esp/EFI/BOOT/BOOTX64.EFI"
second="$out/BOOTX64.reproduced.EFI"
receipt="$out/receipt.json"
AIUEOS_NATIVE_OUT="$native_out" \
  "$aiueos/scripts/build-kotoba-native-kernel.sh" "$compiler" >/dev/null
mkdir -p "$(dirname -- "$efi")"
"$compiler/bin/kotoba-compiler" package-aiueos-boot "$native_out/KERNEL.ELF" --output "$efi"
"$compiler/bin/kotoba-compiler" package-aiueos-boot "$native_out/KERNEL.ELF" --output "$second"
cmp "$efi" "$second"; rm -f "$second"
python3 "$aiueos/scripts/verify-kotoba-native-boot.py" \
  "$efi" "$native_out/KERNEL.ELF" "$expected" "$receipt"
foreign=$(find "$out" -type f \( -name '*.c' -o -name '*.o' -o -name '*.obj' -o -name '*.a' -o -name '*.so' \) -print -quit)
[ -z "$foreign" ] || { echo "error: foreign/C artifact entered native boot output: $foreign" >&2; exit 1; }
