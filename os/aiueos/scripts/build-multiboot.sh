#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}
mb="$out/multiboot"
kernel64="$mb/MULTIBOOT.x86_64.ELF"
kernel="$mb/MULTIBOOT.ELF"
probe=${AIUEOS_KOTOBA_KERNEL_OBJECT:-"$aiueos/kotoba/kernel-probe.o"}
# kernel/apic.c is reused verbatim below and no longer carries its own rdmsr /
# wrmsr, so this path needs the same two MSR objects the UEFI path links. They
# are the first Kotoba objects other than the probe to reach the Multiboot
# kernel; without them apic.o has two undefined symbols and the link fails.
kotoba_msr_read_object=${AIUEOS_KOTOBA_MSR_READ_OBJECT:-"$aiueos/kotoba/msr-read.o"}
kotoba_msr_write_object=${AIUEOS_KOTOBA_MSR_WRITE_OBJECT:-"$aiueos/kotoba/msr-write.o"}
# kernel/acpi.c is reused verbatim here too, and its checksum and header checks
# have been Kotoba objects since ADR-0024 -- but this script was never taught
# about them, so this link has been failing with two undefined symbols
# (kotoba_aiueos_acpi_checksum_ok, kotoba_aiueos_acpi_table_valid) since then.
# Same shape as the apic.c/MSR omission the comment above records.
kotoba_acpi_checksum_object=${AIUEOS_KOTOBA_ACPI_CHECKSUM_OBJECT:-"$aiueos/kotoba/acpi-checksum-ok.o"}
kotoba_acpi_table_valid_object=${AIUEOS_KOTOBA_ACPI_TABLE_VALID_OBJECT:-"$aiueos/kotoba/acpi-table-valid.o"}
# NOT linked here on purpose: kotoba/idt-gate-build.o. This path does not
# include kernel/main.c -- it has its own multiboot/main.c, which keeps a
# second copy of the same descriptor packing in C. Converting that copy would
# spend 257 of an unreplenished 512 fuel per boot (install_idt_and_time_lapic
# sets all 256 vectors, then vector 32 again), against 7 on the kernel path, so
# it waits on a fuel-tier decision in kotoba-native's `bounded-memory?`.

command -v zig >/dev/null 2>&1 || {
  echo "error: Zig is required to build the Multiboot kernel" >&2
  exit 1
}
mkdir -p "$mb"

# The compiler-emitted Kotoba probe object is admitted by the same fail-closed
# verifier the UEFI path uses; a hosted or import-bearing object is rejected.
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$probe" \
  10d91712fccd887e68f9caa25413c8fa2c783968e72b1bead4025c6a294ffa42
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_msr_read_object" \
  9ad2ca19ad21f176d5a79b8ecf3b3ef7a4eec1bc31a620a5207da732f5ea360c \
  kotoba_aiueos_msr_read
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_msr_write_object" \
  217f1ca51d19d5c2364c1fba0aa14e0554682920fb07898a3aead524d7102d15 \
  kotoba_aiueos_msr_write
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_acpi_checksum_object" \
  ca592d688a60f29e60edd8eeeb905429ca75687921bc19bdb8042e7823f3a08c \
  kotoba_aiueos_acpi_checksum_ok
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_acpi_table_valid_object" \
  441d326c311144b6a6b512e5a84c597c1052d903a0b7964d34ef8195baf2d241 \
  kotoba_aiueos_acpi_table_valid

zig cc -target x86_64-freestanding-none \
  -c -o "$mb/entry.o" "$aiueos/multiboot/entry.S"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$mb/main.o" "$aiueos/multiboot/main.c"
# Reuse the kernel's validated ACPI parser and Local APIC timer verbatim (no
# other kernel globals; apic.c does now depend on the two MSR objects linked
# below) so the Multiboot path uses the same checks and interrupt bring-up.
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$mb/acpi.o" "$aiueos/kernel/acpi.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$mb/apic.o" "$aiueos/kernel/apic.c"
zig ld.lld -T "$aiueos/multiboot/linker.ld" -o "$kernel64" \
  "$mb/entry.o" "$mb/main.o" "$mb/acpi.o" "$mb/apic.o" "$probe" \
  "$kotoba_msr_read_object" "$kotoba_msr_write_object" \
  "$kotoba_acpi_checksum_object" "$kotoba_acpi_table_valid_object"

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
