#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}
esp="$out/esp"
efi="$esp/EFI/BOOT/BOOTX64.EFI"
object="$out/uefi-main.obj"
identity_source="$out/kernel-identity.c"
identity_object="$out/kernel-identity.obj"
kernel_dir="$esp/EFI/AIUEOS"
kernel="$kernel_dir/KERNEL.ELF"
kernel_object="$out/kernel-main.o"
kernel_entry_object="$out/kernel-entry.o"
kernel_paging_object="$out/kernel-paging.o"
kernel_acpi_object="$out/kernel-acpi.o"
kernel_vtd_object="$out/kernel-vtd.o"
kernel_apic_object="$out/kernel-apic.o"
kernel_memory_object="$out/kernel-memory.o"
kernel_pci_object="$out/kernel-pci.o"
kernel_scheduler_object="$out/kernel-scheduler.o"
kernel_syscall_object="$out/kernel-syscall.o"
kernel_process_object="$out/kernel-process.o"
kernel_loader_object="$out/kernel-loader.o"
kernel_rsa2048_object="$out/kernel-rsa2048.o"
kernel_smp_object="$out/kernel-smp.o"
kernel_trampoline_object="$out/kernel-ap-trampoline.o"
kernel_ioapic_object="$out/kernel-ioapic.o"
kernel_framebuffer_object="$out/kernel-framebuffer.o"
kotoba_kernel_object=${AIUEOS_KOTOBA_KERNEL_OBJECT:-"$aiueos/kotoba/kernel-probe.o"}
kotoba_journal_object=${AIUEOS_KOTOBA_JOURNAL_OBJECT:-"$aiueos/kotoba/journal-plan.o"}
kotoba_fnv_object=${AIUEOS_KOTOBA_FNV_OBJECT:-"$aiueos/kotoba/fnv1a.o"}
kotoba_journal_valid_object=${AIUEOS_KOTOBA_JOURNAL_VALID_OBJECT:-"$aiueos/kotoba/journal-record-valid.o"}
kotoba_transaction_valid_object=${AIUEOS_KOTOBA_TRANSACTION_VALID_OBJECT:-"$aiueos/kotoba/object-transaction-valid.o"}
kotoba_transaction_route_object=${AIUEOS_KOTOBA_TRANSACTION_ROUTE_OBJECT:-"$aiueos/kotoba/object-transaction-route.o"}
kotoba_mutable_valid_object=${AIUEOS_KOTOBA_MUTABLE_VALID_OBJECT:-"$aiueos/kotoba/mutable-object-valid.o"}
kotoba_superblock_valid_object=${AIUEOS_KOTOBA_SUPERBLOCK_VALID_OBJECT:-"$aiueos/kotoba/superblock-valid.o"}
kotoba_journal_build_object=${AIUEOS_KOTOBA_JOURNAL_BUILD_OBJECT:-"$aiueos/kotoba/journal-record-build.o"}
kotoba_mutable_build_object=${AIUEOS_KOTOBA_MUTABLE_BUILD_OBJECT:-"$aiueos/kotoba/mutable-object-build.o"}
kotoba_cap_valid_object=${AIUEOS_KOTOBA_CAP_VALID_OBJECT:-"$aiueos/kotoba/virtio-cap-valid.o"}
kotoba_extent_valid_object=${AIUEOS_KOTOBA_EXTENT_VALID_OBJECT:-"$aiueos/kotoba/pci-extent-valid.o"}
kotoba_region_valid_object=${AIUEOS_KOTOBA_REGION_VALID_OBJECT:-"$aiueos/kotoba/pci-region-valid.o"}
kotoba_syscall_range_object=${AIUEOS_KOTOBA_SYSCALL_RANGE_OBJECT:-"$aiueos/kotoba/syscall-range-valid.o"}
kotoba_copy_in_object=${AIUEOS_KOTOBA_COPY_IN_OBJECT:-"$aiueos/kotoba/copy-in.o"}
kotoba_capability_object=${AIUEOS_KOTOBA_CAPABILITY_OBJECT:-"$aiueos/kotoba/capability-plan.o"}
kotoba_service_lifecycle_object=${AIUEOS_KOTOBA_SERVICE_LIFECYCLE_OBJECT:-"$aiueos/kotoba/service-lifecycle.o"}
kotoba_service_registry_object=${AIUEOS_KOTOBA_SERVICE_REGISTRY_OBJECT:-"$aiueos/kotoba/service-registry-build.o"}
kotoba_service_registry_state_object=${AIUEOS_KOTOBA_SERVICE_REGISTRY_STATE_OBJECT:-"$aiueos/kotoba/service-registry-state.o"}
kotoba_user_object_journal_object=${AIUEOS_KOTOBA_USER_OBJECT_JOURNAL_OBJECT:-"$aiueos/kotoba/user-object-journal-build.o"}
kotoba_user_object_journal_valid_object=${AIUEOS_KOTOBA_USER_OBJECT_JOURNAL_VALID_OBJECT:-"$aiueos/kotoba/user-object-journal-valid.o"}
kotoba_user_object_journal_value_object=${AIUEOS_KOTOBA_USER_OBJECT_JOURNAL_VALUE_OBJECT:-"$aiueos/kotoba/user-object-journal-value.o"}
kotoba_sha256_object=${AIUEOS_KOTOBA_SHA256_OBJECT:-"$aiueos/kotoba/sha256.o"}
kotoba_user_elf=${AIUEOS_KOTOBA_USER_ELF:-"$aiueos/kotoba/user-smoke.elf"}
kotoba_fnv_sha=
if [ -z "${AIUEOS_KOTOBA_FNV_OBJECT:-}" ]; then
  kotoba_fnv_sha=9d447888daf2c5065b3caf98ee348b426296c95781d0651989bd2025ac7ba52d
fi
kotoba_journal_sha=
if [ -z "${AIUEOS_KOTOBA_JOURNAL_OBJECT:-}" ]; then
  kotoba_journal_sha=c24c7bdab170d65624c1ee2cb939b949c94750b651f59b5aa7d4bc192ec62df6
fi
kotoba_kernel_sha=
if [ -z "${AIUEOS_KOTOBA_KERNEL_OBJECT:-}" ]; then
  kotoba_kernel_sha=10d91712fccd887e68f9caa25413c8fa2c783968e72b1bead4025c6a294ffa42
fi
input_smoke_cflags=
if [ "${AIUEOS_INPUT_SMOKE_SYNTHETIC:-0}" = 1 ]; then
  input_smoke_cflags=-DAIUEOS_INPUT_SMOKE_SYNTHETIC=1
fi

command -v zig >/dev/null 2>&1 || {
  echo "error: Zig is required to build the freestanding UEFI application" >&2
  exit 1
}

mkdir -p "$(dirname -- "$efi")" "$kernel_dir"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_kernel_object" "$kotoba_kernel_sha"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_object" \
  "$kotoba_journal_sha" kotoba_aiueos_journal_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_fnv_object" \
  "$kotoba_fnv_sha" kotoba_aiueos_fnv1a
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_valid_object" \
  f06982540d9516409888a759659b7dc75a30972960f567535b47a57d97399c95 \
  kotoba_aiueos_journal_record_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_valid_object" \
  ee9079df77755d7d540c4e974265da10f51c1c239f5cce7edaa24edf0b047b77 \
  kotoba_aiueos_object_transaction_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_route_object" \
  b2d8c72642733d6ce84ac21516aa523d598fcd99f56cb84a1bca06a4b7ea547b \
  kotoba_aiueos_object_transaction_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_valid_object" \
  53513e67ae900ce2de971aea92ccecc976d361beeaedc8a633b14ef1f873fc73 \
  kotoba_aiueos_mutable_object_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_superblock_valid_object" \
  5b6ae0a1fe186c8530a7b63dd80abd31fd460c3c3e0f441e0ef45340a4ca28a0 \
  kotoba_aiueos_superblock_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_build_object" \
  1f1dedd438d523c7f92bde90f8bf07c92768fd9dd7cfc73a27f9dc895eb3bca7 \
  kotoba_aiueos_journal_record_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_build_object" \
  d593a4db905cac264c67732983bf0b62de783011b46e505257a51d94d820eafd \
  kotoba_aiueos_mutable_object_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cap_valid_object" \
  f03487d441ca9af4da636bcf6a9c983e23de86eb60ab70fe7533fa558f4262d4 \
  kotoba_aiueos_virtio_cap_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_extent_valid_object" \
  345d52917447ddedd21fcee1e7c1143395132828deade02e29896a3829bafdbb \
  kotoba_aiueos_pci_extent_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_region_valid_object" \
  824abbe8509d43eb5276a612bd38e9b472ebba1b4bd71f416671062e4b523123 \
  kotoba_aiueos_pci_region_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_syscall_range_object" \
  c65aa4b0b2b47891f2b1340a289157625262156733d85195d0449a2050aa18b8 \
  kotoba_aiueos_syscall_range_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_copy_in_object" \
  f3b8ae90a2d77ca821c82dfd03f0b6ffc080ffe2b78195a334a4265fbec518e4 \
  kotoba_aiueos_copy_in
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_capability_object" \
  006f509119d39298a1a64093f9b49f48f808445d251e96505c4c03e3abc068bb \
  kotoba_aiueos_capability_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_lifecycle_object" \
  cd6d9c57cd4dd94839ef1a255c6d82b6c1b231c08aa1f7de86ab8c0029720816 \
  kotoba_aiueos_service_lifecycle
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_object" \
  70eee5d4dd599ea2049261e92a656931768b355eefc0fb6d83deee192a3a05f0 \
  kotoba_aiueos_service_registry_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_state_object" \
  d73f13de0d86a4af46e33516b8b0f6358b5d477307c61d40624b971f34c15f3e \
  kotoba_aiueos_service_registry_state
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_object" \
  994d8a296d17afa67a8c9267cafa6079edca5068aeed46e78d8f455a40df1cfd \
  kotoba_aiueos_user_object_journal_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_valid_object" \
  0f2015e53ed083741687abfbaff72edf8a525947b9fc753cacc7a1bf10faf46f \
  kotoba_aiueos_user_object_journal_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_value_object" \
  bd1de2777d75e02968939d2b7bc74e84dc16a8a9431fe36bd2c2170d6866fad3 \
  kotoba_aiueos_user_object_journal_value
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_sha256_object" \
  ad28e7d83d6e582df2dacf802e915fc9532fc99e141e174e7bf8642191db2c29 \
  kotoba_aiueos_sha256
python3 "$aiueos/scripts/verify-kotoba-user-elf.py" "$kotoba_user_elf" \
  1f0e5897831d0de6bbcb15eec82a6e0c4b402b436689cec051bc6de3b5c4e905
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_object" "$aiueos/kernel/main.c"
zig cc -target x86_64-freestanding-none \
  -c -o "$kernel_entry_object" "$aiueos/kernel/entry.S"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_paging_object" "$aiueos/kernel/paging.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_acpi_object" "$aiueos/kernel/acpi.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_vtd_object" "$aiueos/kernel/vtd.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_apic_object" "$aiueos/kernel/apic.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_memory_object" "$aiueos/kernel/memory.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  $input_smoke_cflags \
  -c -o "$kernel_pci_object" "$aiueos/kernel/pci.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_scheduler_object" "$aiueos/kernel/scheduler.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_syscall_object" "$aiueos/kernel/syscall.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_process_object" "$aiueos/kernel/process.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_loader_object" "$aiueos/kernel/loader.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_rsa2048_object" "$aiueos/kernel/rsa2048.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_smp_object" "$aiueos/kernel/smp.c"
zig cc -target x86_64-freestanding-none \
  -c -o "$kernel_trampoline_object" "$aiueos/kernel/ap_trampoline.S"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_ioapic_object" "$aiueos/kernel/ioapic.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_framebuffer_object" "$aiueos/kernel/framebuffer.c"
zig ld.lld -nostdlib -static -z max-page-size=0x1000 \
  -T "$aiueos/kernel/linker.ld" -o "$kernel" \
  "$kernel_entry_object" "$kernel_object" "$kernel_paging_object" \
  "$kernel_acpi_object" "$kernel_vtd_object" "$kernel_apic_object" "$kernel_memory_object" \
  "$kernel_pci_object" "$kernel_scheduler_object" "$kernel_syscall_object" \
  "$kernel_process_object" "$kernel_loader_object" "$kernel_rsa2048_object" \
  "$kernel_smp_object" "$kernel_trampoline_object" \
  "$kernel_ioapic_object" "$kernel_framebuffer_object" "$kotoba_kernel_object" \
  "$kotoba_journal_object" "$kotoba_fnv_object" "$kotoba_journal_valid_object" \
  "$kotoba_transaction_valid_object" "$kotoba_transaction_route_object" \
  "$kotoba_mutable_valid_object" \
  "$kotoba_superblock_valid_object" "$kotoba_journal_build_object" \
  "$kotoba_mutable_build_object" "$kotoba_cap_valid_object" \
  "$kotoba_extent_valid_object" "$kotoba_region_valid_object" \
  "$kotoba_syscall_range_object" "$kotoba_copy_in_object" \
  "$kotoba_capability_object" "$kotoba_service_lifecycle_object" \
  "$kotoba_service_registry_object" "$kotoba_service_registry_state_object" \
  "$kotoba_user_object_journal_object" \
  "$kotoba_user_object_journal_valid_object" \
  "$kotoba_user_object_journal_value_object" "$kotoba_sha256_object"
python3 - "$kernel" "$identity_source" <<'PY'
import hashlib, pathlib, sys
digest = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).digest()
values = ",".join(f"0x{byte:02x}" for byte in digest)
pathlib.Path(sys.argv[2]).write_text(
    "#include <stdint.h>\nconst uint8_t aiueos_expected_kernel_sha256[32]={" + values + "};\n",
    encoding="ascii")
PY
zig cc -target x86_64-windows-gnu -std=c11 -O2 -ffreestanding \
  -c -o "$identity_object" "$identity_source"
zig cc -target x86_64-windows-gnu -std=c11 -O2 \
  -ffreestanding -fshort-wchar -fno-stack-protector -mno-red-zone \
  -c -o "$object" "$aiueos/uefi/main.c"
zig lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /timestamp:0 \
  /fixed:no "/out:$efi" "$object" "$identity_object"

magic=$(dd if="$efi" bs=1 count=2 2>/dev/null)
[ "$magic" = MZ ] || {
  echo "error: $efi is not a PE/COFF image" >&2
  exit 1
}
[ "$(dd if="$kernel" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] || {
  echo "error: $kernel is not an ELF image" >&2
  exit 1
}

echo "$efi"
