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
kotoba_digest_equal_object=${AIUEOS_KOTOBA_DIGEST_EQUAL_OBJECT:-"$aiueos/kotoba/digest-equal.o"}
kotoba_catalog_valid_object=${AIUEOS_KOTOBA_CATALOG_VALID_OBJECT:-"$aiueos/kotoba/app-catalog-valid.o"}
kotoba_app_lookup_object=${AIUEOS_KOTOBA_APP_LOOKUP_OBJECT:-"$aiueos/kotoba/app-lookup-plan.o"}
kotoba_user_elf_valid_object=${AIUEOS_KOTOBA_USER_ELF_VALID_OBJECT:-"$aiueos/kotoba/user-elf-valid.o"}
kotoba_user_context_object=${AIUEOS_KOTOBA_USER_CONTEXT_OBJECT:-"$aiueos/kotoba/user-context-build.o"}
kotoba_mapping_plan_object=${AIUEOS_KOTOBA_MAPPING_PLAN_OBJECT:-"$aiueos/kotoba/page-mapping-plan.o"}
kotoba_process_plan_object=${AIUEOS_KOTOBA_PROCESS_PLAN_OBJECT:-"$aiueos/kotoba/process-create-plan.o"}
kotoba_teardown_plan_object=${AIUEOS_KOTOBA_TEARDOWN_PLAN_OBJECT:-"$aiueos/kotoba/process-teardown-plan.o"}
kotoba_task_plan_object=${AIUEOS_KOTOBA_TASK_PLAN_OBJECT:-"$aiueos/kotoba/task-slot-plan.o"}
kotoba_dispatch_plan_object=${AIUEOS_KOTOBA_DISPATCH_PLAN_OBJECT:-"$aiueos/kotoba/scheduler-dispatch-plan.o"}
kotoba_exit_route_object=${AIUEOS_KOTOBA_EXIT_ROUTE_OBJECT:-"$aiueos/kotoba/task-exit-route.o"}
kotoba_service_task_object=${AIUEOS_KOTOBA_SERVICE_TASK_OBJECT:-"$aiueos/kotoba/service-task-transition.o"}
kotoba_rsa2048_object=${AIUEOS_KOTOBA_RSA2048_OBJECT:-"$aiueos/kotoba/rsa2048.o"}
kotoba_user_elf=${AIUEOS_KOTOBA_USER_ELF:-"$aiueos/kotoba/user-smoke.elf"}
kotoba_fnv_sha=
if [ -z "${AIUEOS_KOTOBA_FNV_OBJECT:-}" ]; then
  kotoba_fnv_sha=c924ac51de16c3120a6fd227eb49a14ab1874e4e365e1bdf3a1bfe7fca7672f3
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
if [ "${AIUEOS_CATALOG_POLICY_SELFTEST:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_CATALOG_POLICY_SELFTEST=1"
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
  7c3a5b581c99daa5282d963efb9162dc0a2af25185523ce031270204e213e3f0 \
  kotoba_aiueos_journal_record_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_valid_object" \
  fb8d3cd1b1b9c13cfd3e6f80cac15f568d13327cac76b47b54ca60ad3fd09d86 \
  kotoba_aiueos_object_transaction_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_route_object" \
  ab98299f535a2d0752135032b960d7830cca8aee4cdfff8a2f4952d897cfe3dd \
  kotoba_aiueos_object_transaction_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_valid_object" \
  53513e67ae900ce2de971aea92ccecc976d361beeaedc8a633b14ef1f873fc73 \
  kotoba_aiueos_mutable_object_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_superblock_valid_object" \
  c7181af2d2ff2713b1e7e5979d2fb0b4bc989ace280858d7afd478c3739a980e \
  kotoba_aiueos_superblock_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_build_object" \
  9691f9dda0899b70fa2853f07da2a974cefd957bdb7c6fee3235301f6a3143dc \
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
  ab367b1ac46461f6228080ee909415b8825a429b47abb1edc8dbafc7083bba7c \
  kotoba_aiueos_copy_in
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_capability_object" \
  006f509119d39298a1a64093f9b49f48f808445d251e96505c4c03e3abc068bb \
  kotoba_aiueos_capability_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_lifecycle_object" \
  cd6d9c57cd4dd94839ef1a255c6d82b6c1b231c08aa1f7de86ab8c0029720816 \
  kotoba_aiueos_service_lifecycle
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_object" \
  b0e9c90aaef5477fb5ababd6dd3067dd95a7eba93f3bb262cc49bace7e5a44ce \
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
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_digest_equal_object" \
  6d005bf596ff10343377d9c243d473437fa272559b7f9130cba47cc4cd80d3aa \
  kotoba_aiueos_digest_equal
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_catalog_valid_object" \
  bf990c3775bd1351627daa669a124adad8e194710dc41d93f0c1b2ccfdacd927 \
  kotoba_aiueos_app_catalog_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_app_lookup_object" \
  aa8ecea382820707638aa24e49226dbab243c95dc2a28ebfe3fac3a4dffe1a6c \
  kotoba_aiueos_app_lookup_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_elf_valid_object" \
  b363aa7608f95c5fee37ddb95961c7e7524ca307f4d7407c4c25ca05435426ab \
  kotoba_aiueos_user_elf_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_context_object" \
  8e743cba708c79e6800d5c0f26c68dfefe055179f2bef8e24753012a4bc21e5b \
  kotoba_aiueos_user_context_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mapping_plan_object" \
  c492472360f4632a5f4e0457ef3f2dd867306a36ea8ba3415cdb4463c78106b5 \
  kotoba_aiueos_page_mapping_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_process_plan_object" \
  a1931ab0058a322f728203e1441cd93848d2661b639c600d8049f33056260ddf \
  kotoba_aiueos_process_create_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_teardown_plan_object" \
  0a34d448348f366d6bd41560a1a62ea4fb9d317c281beec14656af65976182b9 \
  kotoba_aiueos_process_teardown_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_task_plan_object" \
  da4e6d51f2bc5ed6f0120513bb2d8be60ab1efae8e7020fee3b27ea1df1cc47e \
  kotoba_aiueos_task_slot_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_dispatch_plan_object" \
  19f1dc06e4c6c276e3a7ebb14c9e30a85cbf5c225e7fbae187a7ad4e32a5542a \
  kotoba_aiueos_scheduler_dispatch_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_exit_route_object" \
  dbf1dacb2d4a2fc0adf49134cbd6b973fa3a85e780f3d2b242a9baacb28799d2 \
  kotoba_aiueos_task_exit_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_task_object" \
  a6b70f28d7b63a64b9b0ff0b66eba0e465a65caa39b0413f34eef5245d32d466 \
  kotoba_aiueos_service_task_transition
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_rsa2048_object" \
  97a6c6b1f4c3f3569bf8d40423db924d291aa0b6f10cd7bace79f54e193387a6 \
  kotoba_aiueos_rsa2048_sha256_verify
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
  "$kernel_process_object" "$kernel_loader_object" \
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
  "$kotoba_user_object_journal_value_object" "$kotoba_sha256_object" \
  "$kotoba_digest_equal_object" "$kotoba_catalog_valid_object" \
  "$kotoba_app_lookup_object" \
  "$kotoba_user_elf_valid_object" \
  "$kotoba_user_context_object" \
  "$kotoba_mapping_plan_object" \
  "$kotoba_process_plan_object" \
  "$kotoba_teardown_plan_object" \
  "$kotoba_task_plan_object" \
  "$kotoba_dispatch_plan_object" \
  "$kotoba_exit_route_object" \
  "$kotoba_service_task_object" \
  "$kotoba_rsa2048_object"
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
