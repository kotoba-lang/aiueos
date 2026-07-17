#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}
mb="$out/multiboot"
kernel64="$mb/MULTIBOOT.x86_64.ELF"
kernel="$mb/MULTIBOOT.ELF"
probe=${AIUEOS_KOTOBA_KERNEL_OBJECT:-"$aiueos/kotoba/kernel-probe.o"}

command -v zig >/dev/null 2>&1 || {
  echo "error: Zig is required to build the Multiboot kernel" >&2
  exit 1
}
mkdir -p "$mb"

# The compiler-emitted Kotoba probe object is admitted by the same fail-closed
# verifier the UEFI path uses; a hosted or import-bearing object is rejected.
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$probe" \
  10d91712fccd887e68f9caa25413c8fa2c783968e72b1bead4025c6a294ffa42

zig cc -target x86_64-freestanding-none \
  -c -o "$mb/entry.o" "$aiueos/multiboot/entry.S"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$mb/main.o" "$aiueos/multiboot/main.c"
zig ld.lld -T "$aiueos/multiboot/linker.ld" -o "$kernel64" \
  "$mb/entry.o" "$mb/main.o" "$probe"

# QEMU's Multiboot loader wants an ELFCLASS32/EM_386 container; wrap the linked
# x86_64 load image verbatim (the trampoline switches to long mode itself).
python3 "$aiueos/scripts/wrap-multiboot32.py" "$kernel64" "$kernel"

[ "$(dd if="$kernel" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] || {
  echo "error: $kernel is not an ELF image" >&2
  exit 1
}
[ "$(dd if="$kernel" bs=1 skip=4 count=1 2>/dev/null | od -An -tu1 | tr -d ' \n')" = 1 ] || {
  echo "error: $kernel is not a 32-bit ELF container" >&2
  exit 1
}
python3 - "$kernel" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
window = data[:8192]
magic = struct.pack("<I", 0x1BADB002)
index = window.find(magic)
if index < 0 or index % 4 != 0:
    raise SystemExit("multiboot header not found aligned within first 8 KiB")
flags, checksum = struct.unpack_from("<II", window, index + 4)
if (0x1BADB002 + flags + checksum) & 0xFFFFFFFF != 0:
    raise SystemExit("multiboot header checksum is invalid")
print("multiboot header at file offset %d flags=0x%x" % (index, flags))
PY
echo "$kernel"
