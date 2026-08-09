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
kotoba_pci_config_read_object=${AIUEOS_KOTOBA_PCI_CONFIG_READ_OBJECT:-"$aiueos/kotoba/pci-config-read.o"}
kotoba_pci_config_write_object=${AIUEOS_KOTOBA_PCI_CONFIG_WRITE_OBJECT:-"$aiueos/kotoba/pci-config-write.o"}
kotoba_mmio_map_admit_object=${AIUEOS_KOTOBA_MMIO_MAP_ADMIT_OBJECT:-"$aiueos/kotoba/mmio-map-admit.o"}
kotoba_acpi_checksum_object=${AIUEOS_KOTOBA_ACPI_CHECKSUM_OBJECT:-"$aiueos/kotoba/acpi-checksum-ok.o"}
kotoba_acpi_table_valid_object=${AIUEOS_KOTOBA_ACPI_TABLE_VALID_OBJECT:-"$aiueos/kotoba/acpi-table-valid.o"}
kotoba_vtd_admit_object=${AIUEOS_KOTOBA_VTD_ADMIT_OBJECT:-"$aiueos/kotoba/vtd-admit.o"}
kotoba_msr_read_object=${AIUEOS_KOTOBA_MSR_READ_OBJECT:-"$aiueos/kotoba/msr-read.o"}
kotoba_msr_write_object=${AIUEOS_KOTOBA_MSR_WRITE_OBJECT:-"$aiueos/kotoba/msr-write.o"}
kotoba_idt_gate_object=${AIUEOS_KOTOBA_IDT_GATE_OBJECT:-"$aiueos/kotoba/idt-gate-build.o"}
# kernel/main.c now quiets the legacy 8259 through this object before the first
# `sti`. Until now this path never masked the PIC at all and was green only
# because OVMF does it before handoff (ADR-0028 fixed the Multiboot path only).
kotoba_pic_disable_object=${AIUEOS_KOTOBA_PIC_DISABLE_OBJECT:-"$aiueos/kotoba/pic-disable.o"}
# The six hand-written `cpuid` sites in this kernel asked THREE questions, and
# only two were feature tests, so they become three objects rather than one
# generic accessor -- the knowledge of which leaf and which bit answers a
# question is the decision, and a `(leaf, bit)` parameter pair would have left
# it as magic numbers at the C call site. This path compiles all three of the C
# files involved (paging.c, process.c, pci.c below), so it links all three:
#   cpu-feature-nx       paging.c   leaf 0x80000001 EDX bit 20
#   cpu-feature-syscall  process.c  leaf 0x80000001 EDX bit 11
#   cpu-apic-id          pci.c      leaf 1 EBX 31:24 -- an ID, not a feature
# The Multiboot path compiles NONE of those three files and links none of these.
kotoba_cpu_feature_nx_object=${AIUEOS_KOTOBA_CPU_FEATURE_NX_OBJECT:-"$aiueos/kotoba/cpu-feature-nx.o"}
kotoba_cpu_feature_syscall_object=${AIUEOS_KOTOBA_CPU_FEATURE_SYSCALL_OBJECT:-"$aiueos/kotoba/cpu-feature-syscall.o"}
kotoba_cpu_apic_id_object=${AIUEOS_KOTOBA_CPU_APIC_ID_OBJECT:-"$aiueos/kotoba/cpu-apic-id.o"}
kotoba_syscall_range_object=${AIUEOS_KOTOBA_SYSCALL_RANGE_OBJECT:-"$aiueos/kotoba/syscall-range-valid.o"}
kotoba_copy_in_object=${AIUEOS_KOTOBA_COPY_IN_OBJECT:-"$aiueos/kotoba/copy-in.o"}
kotoba_capability_object=${AIUEOS_KOTOBA_CAPABILITY_OBJECT:-"$aiueos/kotoba/capability-plan.o"}
kotoba_capability_mutation_object=${AIUEOS_KOTOBA_CAPABILITY_MUTATION_OBJECT:-"$aiueos/kotoba/capability-mutation-plan.o"}
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
# The kernel-selector twin of the object above, for tasks `iret` enters at ring
# 0. Same 160-byte frame in the same bounded 4 KiB stack; CS 0x08 / SS 0x10 and
# an RSP that is the top of that very stack rather than a separate user stack,
# eight bytes lower than the ring-3 frame because `iret` lands in an ordinary C
# function and must reproduce the RSP % 16 == 8 that `call` would have left.
# Both twins are linked here because this path is the one that compiles
# kernel/scheduler.c, which owns both entry points.
kotoba_kernel_context_object=${AIUEOS_KOTOBA_KERNEL_CONTEXT_OBJECT:-"$aiueos/kotoba/kernel-context-build.o"}
kotoba_mapping_plan_object=${AIUEOS_KOTOBA_MAPPING_PLAN_OBJECT:-"$aiueos/kotoba/page-mapping-plan.o"}
kotoba_process_plan_object=${AIUEOS_KOTOBA_PROCESS_PLAN_OBJECT:-"$aiueos/kotoba/process-create-plan.o"}
kotoba_teardown_plan_object=${AIUEOS_KOTOBA_TEARDOWN_PLAN_OBJECT:-"$aiueos/kotoba/process-teardown-plan.o"}
kotoba_task_plan_object=${AIUEOS_KOTOBA_TASK_PLAN_OBJECT:-"$aiueos/kotoba/task-slot-plan.o"}
kotoba_dispatch_plan_object=${AIUEOS_KOTOBA_DISPATCH_PLAN_OBJECT:-"$aiueos/kotoba/scheduler-dispatch-plan.o"}
kotoba_exit_route_object=${AIUEOS_KOTOBA_EXIT_ROUTE_OBJECT:-"$aiueos/kotoba/task-exit-route.o"}
kotoba_service_task_object=${AIUEOS_KOTOBA_SERVICE_TASK_OBJECT:-"$aiueos/kotoba/service-task-transition.o"}
kotoba_rsa2048_object=${AIUEOS_KOTOBA_RSA2048_OBJECT:-"$aiueos/kotoba/rsa2048.o"}
kotoba_x25519_object=${AIUEOS_KOTOBA_X25519_OBJECT:-"$aiueos/kotoba/x25519.o"}
kotoba_net_arp_object=${AIUEOS_KOTOBA_NET_ARP_OBJECT:-"$aiueos/kotoba/net-arp-reply-valid.o"}
kotoba_ipv4_checksum_object=${AIUEOS_KOTOBA_IPV4_CHECKSUM_OBJECT:-"$aiueos/kotoba/ipv4-checksum.o"}
kotoba_ipv4_icmp_object=${AIUEOS_KOTOBA_IPV4_ICMP_OBJECT:-"$aiueos/kotoba/ipv4-icmp-reply-valid.o"}
kotoba_tcp_checksum_object=${AIUEOS_KOTOBA_TCP_CHECKSUM_OBJECT:-"$aiueos/kotoba/tcp-checksum-ok.o"}
kotoba_tcp_segment_object=${AIUEOS_KOTOBA_TCP_SEGMENT_OBJECT:-"$aiueos/kotoba/tcp-segment-valid.o"}
kotoba_user_elf=${AIUEOS_KOTOBA_USER_ELF:-"$aiueos/kotoba/user-smoke.elf"}
kotoba_fnv_sha=
if [ -z "${AIUEOS_KOTOBA_FNV_OBJECT:-}" ]; then
  kotoba_fnv_sha=1970e6a3830e7922606ff126f416bbd5de6dfa3bd7258e2340d06fad6eaacca1
fi
kotoba_journal_sha=
if [ -z "${AIUEOS_KOTOBA_JOURNAL_OBJECT:-}" ]; then
  kotoba_journal_sha=77436427e280f319c598f1b3fcb5cd3a5a677749269986161b292c13609c7b64
fi
kotoba_kernel_sha=
if [ -z "${AIUEOS_KOTOBA_KERNEL_OBJECT:-}" ]; then
  kotoba_kernel_sha=aad9e7ebe54f8a1509b8dabdd2ed2c90a4397871cf993bdb4893ff8c9338f7cc
fi
input_smoke_cflags=
if [ "${AIUEOS_INPUT_SMOKE_SYNTHETIC:-0}" = 1 ]; then
  input_smoke_cflags=-DAIUEOS_INPUT_SMOKE_SYNTHETIC=1
fi
if [ "${AIUEOS_CATALOG_POLICY_SELFTEST:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_CATALOG_POLICY_SELFTEST=1"
fi
if [ "${AIUEOS_CRASH_RECEIPT_SMOKE:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_CRASH_RECEIPT_SMOKE=1"
fi
if [ "${AIUEOS_FAULT_RECEIPT_SMOKE:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_FAULT_RECEIPT_SMOKE=1"
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
  cda0832061bc987a848e56761dfb244d55d4da25a2ee68d6338665f3900c408a \
  kotoba_aiueos_journal_record_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_valid_object" \
  a988d758004cbbbe7ab47fbe3d17d9829d46d4ae53ed064b3dfb85a7d7c717e8 \
  kotoba_aiueos_object_transaction_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_route_object" \
  13a728b4cfce5193c459580773bffb6a919459dc44f43366263ccdf7306e7cab \
  kotoba_aiueos_object_transaction_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_valid_object" \
  ab27088387bd3906522a0248ab7d89361258346501590f7033b1d02faee6e25d \
  kotoba_aiueos_mutable_object_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_superblock_valid_object" \
  707d9ce235f9a3cf6d00b56494faa38ff6a456a640a8a47085efbbfe6bf0a114 \
  kotoba_aiueos_superblock_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_build_object" \
  9de2b561d8508e8f9e92ce6031a79a7c24d1c7c975520c2e7031f0d1964ea0c9 \
  kotoba_aiueos_journal_record_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_build_object" \
  a6576493b3a0f38cb3a58b1f814832cdc5cfdff114b0b7e61e7f0a0f0008879b \
  kotoba_aiueos_mutable_object_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cap_valid_object" \
  d9a393ad8e3b7c31439ef7150e2ccd81c79bc79819cf63d0cc6931db144a9a09 \
  kotoba_aiueos_virtio_cap_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_extent_valid_object" \
  44754d39d8ef14b89283c491d9d6b41aaefa605569d7fc8a04870e846948aad8 \
  kotoba_aiueos_pci_extent_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_region_valid_object" \
  3dfa4c096610a41da0a0402a7b98904984483f9bb82d90660d8e566e8514ec02 \
  kotoba_aiueos_pci_region_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_pci_config_read_object" \
  6217aa2dc617c533836f55e73ab33f453ddce2af99326641b8fcae6b5e10b509 \
  kotoba_aiueos_pci_config_read
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_pci_config_write_object" \
  3b56e9c90d5849f7fc0b89fd02abd4dceca3a680defc3048ce8f4c37aed30fc9 \
  kotoba_aiueos_pci_config_write
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mmio_map_admit_object" \
  d50521046b1d2f17a99b0522b8c8d4c91c3932bb5596eed12344d51f145e2355 \
  kotoba_aiueos_mmio_map_admit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_acpi_checksum_object" \
  ca592d688a60f29e60edd8eeeb905429ca75687921bc19bdb8042e7823f3a08c \
  kotoba_aiueos_acpi_checksum_ok
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_acpi_table_valid_object" \
  441d326c311144b6a6b512e5a84c597c1052d903a0b7964d34ef8195baf2d241 \
  kotoba_aiueos_acpi_table_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_vtd_admit_object" \
  6a671faf55325537f6c027d45553b0bdd6863e2eb8b2de350049d821a9f8b6e3 \
  kotoba_aiueos_vtd_admit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_msr_read_object" \
  f9985d1504326f61040c4cb8418708764c42fd7d8d3c7ce9ddfca1ad9fa12c4c \
  kotoba_aiueos_msr_read
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_msr_write_object" \
  25b9ec1d0d789a2b21ba6624a40072d1733973c1d5d8bc74baef17f6ba8300db \
  kotoba_aiueos_msr_write
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_idt_gate_object" \
  6511e47409f4f921e0b459c2506b5055302a183cc250024fe816649c86dd548e \
  kotoba_aiueos_idt_gate_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_pic_disable_object" \
  9c36989f2807db1143b408bbc2a9f43e9d92863e8cc618280cb378aba8257eb7 \
  kotoba_aiueos_pic_disable
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cpu_feature_nx_object" \
  668e000026f184140fbc179a03f9226c903db514d1ffaf073519d2d14b62b66e \
  kotoba_aiueos_cpu_feature_nx
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cpu_feature_syscall_object" \
  f9ce6d93a4740879c47bce35b8129b22b1b88abfc199a2330e1e3d0eae2ddb70 \
  kotoba_aiueos_cpu_feature_syscall
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cpu_apic_id_object" \
  afd81647882b08ca5a257197c6837b2fc901c371c53730f27ddcf43b36042658 \
  kotoba_aiueos_cpu_apic_id
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_syscall_range_object" \
  ea762c91410c0d87d8c7a97aa5030ccb222df509ebf7c7779d44d0a347225da1 \
  kotoba_aiueos_syscall_range_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_copy_in_object" \
  8e451b1e2c050d7bfc78bcb5a47494a57ba8bf4479fa9e56d8b8637166508904 \
  kotoba_aiueos_copy_in
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_capability_object" \
  8796fbdf6bdab8b9e6ea88f5aa87513ea4367a75942f618980340b1c3fd3f5b3 \
  kotoba_aiueos_capability_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_capability_mutation_object" \
  26d27dbbc64ce65fe38cdf965ad9ebcadd8353fdcea6af34a0fe74786b3b297f \
  kotoba_aiueos_capability_mutation_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_lifecycle_object" \
  59774bea830a52a7d41503b6dc2f31f5129a364564530da74a415033b4001a7e \
  kotoba_aiueos_service_lifecycle
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_object" \
  b215888741e45e5444fac1dfb74c0720f4d3d5d497ec359804ee6177b1f7bc92 \
  kotoba_aiueos_service_registry_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_state_object" \
  6c8a21c27c2d69e8d0a05238cc4f31426229558160b2dba0772e589dcd79abbe \
  kotoba_aiueos_service_registry_state
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_object" \
  c3e1b1090493ecc07c4e6c8839900abef2e8e7cdcdf900dfe605f478086853bf \
  kotoba_aiueos_user_object_journal_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_valid_object" \
  b3f5bb205047e113ebae23c290d71d3b4acf32046d948e1dc221c98ba3b5daa2 \
  kotoba_aiueos_user_object_journal_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_value_object" \
  6156c75bbe0568575b82760989c1c0f58a8cde85b42f3c22f2f2e57e5be179ba \
  kotoba_aiueos_user_object_journal_value
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_sha256_object" \
  8724ba47fcfbf8bd0c3a368281a14a84338c35b7aab3cf65718c931c620a4c94 \
  kotoba_aiueos_sha256
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_digest_equal_object" \
  b4c9f8cf9a1420e204485edb3f1e24e5a24988cd2495bda8019a8a51a7c50cfa \
  kotoba_aiueos_digest_equal
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_catalog_valid_object" \
  65291b034e051aa42e0bbf06b556eebf2d1895d0b4edb07412febc5149f14613 \
  kotoba_aiueos_app_catalog_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_app_lookup_object" \
  e6fd9ab9fdf915ffab7c58665677dd81729fee5d454e7b95b90638000e1db893 \
  kotoba_aiueos_app_lookup_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_elf_valid_object" \
  81a7418f9a1580b04ab39ddecf1953415df9c32c6692033ccb0c1ea3cc1f3005 \
  kotoba_aiueos_user_elf_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_context_object" \
  74d11f41cea74f8895743feff2a395a3a5fb3bb50c542b9edd9a0f7ecceebb9e \
  kotoba_aiueos_user_context_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_kernel_context_object" \
  0f1364422887e9de0f40a084d4efc24ef670172926ffc63a3b0a7746145dc2a4 \
  kotoba_aiueos_kernel_context_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mapping_plan_object" \
  60a822c04a00544b16087dccdca586fc0a8f09412a2c6b8db25a27bef80ef7b9 \
  kotoba_aiueos_page_mapping_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_process_plan_object" \
  5bf6dc3174c2de5eeab6b105dd2c123f46af3df0fb7561876327f911c58d8a4a \
  kotoba_aiueos_process_create_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_teardown_plan_object" \
  982b566a9f861bce3fd658a76b0e98589342d3681d84d8cf7ccd16e4184d7767 \
  kotoba_aiueos_process_teardown_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_task_plan_object" \
  ccfed7e37560e154e7cfa6f8ba4370dc02d5d350c2dd43d540349c9fbc5a40c7 \
  kotoba_aiueos_task_slot_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_dispatch_plan_object" \
  fe7e65e1592b37f9cead88749d7fb97dbb1e3e3ed7bf4819d1f9c9574b819087 \
  kotoba_aiueos_scheduler_dispatch_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_exit_route_object" \
  d725fe911de06163f865880572a6d4319be7c8188dd8d30a94b6cdf72646a5b5 \
  kotoba_aiueos_task_exit_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_task_object" \
  2fbc98c8078d204b0fbb0958df47db07a9faecad19db1ab4bee135521a0d2126 \
  kotoba_aiueos_service_task_transition
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_rsa2048_object" \
  191ca321516859892ce3fd8f11492f02a4b17acad173c3e63c7fd3f30ab03cc3 \
  kotoba_aiueos_rsa2048_sha256_verify
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_net_arp_object" \
  c9c339f0418c0c90fa20bfe4e7d0b378b392ddd7214174045158a13ac2faa295 \
  kotoba_aiueos_net_arp_reply_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ipv4_checksum_object" \
  986409896183c5978f7321c1c8da7a87a8e36760d1aac717a7c7a97d54323574 \
  kotoba_aiueos_ipv4_checksum
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ipv4_icmp_object" \
  11de7cd1adca81fc128a7001474e0927740b5273be34b8def699a95891cd612b \
  kotoba_aiueos_ipv4_icmp_reply_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_tcp_checksum_object" \
  880bce504d317cded81feb352b2a6e591e31c1d659448970b0dce7cf2a327d60 \
  kotoba_aiueos_tcp_checksum_ok
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_tcp_segment_object" \
  841cb37ee960cd75f59a5e3256c197cffcbad1cc9c65ac87782edfa5b5fc1144 \
  kotoba_aiueos_tcp_segment_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_x25519_object" \
  62cd62b0bf66a9aeee7f338eb4b9c18473ebd65a4c430c3449b3ca56d81b5e73 \
  kotoba_aiueos_x25519
python3 "$aiueos/scripts/verify-kotoba-user-elf.py" "$kotoba_user_elf" \
  1f0e5897831d0de6bbcb15eec82a6e0c4b402b436689cec051bc6de3b5c4e905
if [ -n "${AIUEOS_EXTERNAL_KERNEL_ELF:-}" ]; then
  python3 "$aiueos/scripts/verify-kotoba-native-kernel.py" \
    "$AIUEOS_EXTERNAL_KERNEL_ELF" "$aiueos/native/kernel.kotoba" \
    6d14f1ab0dc8c296a25a3b41a774423a829239eb "$out/native-kernel-receipt.json"
  cp "$AIUEOS_EXTERNAL_KERNEL_ELF" "$kernel"
else
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  $input_smoke_cflags \
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
  "$kotoba_pci_config_read_object" "$kotoba_pci_config_write_object" \
  "$kotoba_x25519_object" \
  "$kotoba_mmio_map_admit_object" \
  "$kotoba_acpi_checksum_object" "$kotoba_acpi_table_valid_object" \
  "$kotoba_vtd_admit_object" \
  "$kotoba_msr_read_object" "$kotoba_msr_write_object" \
  "$kotoba_idt_gate_object" "$kotoba_pic_disable_object" \
  "$kotoba_cpu_feature_nx_object" "$kotoba_cpu_feature_syscall_object" \
  "$kotoba_cpu_apic_id_object" \
  "$kotoba_syscall_range_object" "$kotoba_copy_in_object" \
  "$kotoba_capability_object" "$kotoba_capability_mutation_object" \
  "$kotoba_service_lifecycle_object" \
  "$kotoba_service_registry_object" "$kotoba_service_registry_state_object" \
  "$kotoba_user_object_journal_object" \
  "$kotoba_user_object_journal_valid_object" \
  "$kotoba_user_object_journal_value_object" "$kotoba_sha256_object" \
  "$kotoba_digest_equal_object" "$kotoba_catalog_valid_object" \
  "$kotoba_app_lookup_object" \
  "$kotoba_user_elf_valid_object" \
  "$kotoba_user_context_object" \
  "$kotoba_kernel_context_object" \
  "$kotoba_mapping_plan_object" \
  "$kotoba_process_plan_object" \
  "$kotoba_teardown_plan_object" \
  "$kotoba_task_plan_object" \
  "$kotoba_dispatch_plan_object" \
  "$kotoba_exit_route_object" \
  "$kotoba_service_task_object" \
  "$kotoba_rsa2048_object" \
  "$kotoba_net_arp_object" \
  "$kotoba_ipv4_checksum_object" \
  "$kotoba_ipv4_icmp_object" \
  "$kotoba_tcp_checksum_object" \
  "$kotoba_tcp_segment_object"
fi
initramfs="$kernel_dir/INITRD.IMG"
recovery_signature="$aiueos/kotoba/user-smoke.sig"
if [ "${AIUEOS_CORRUPT_RECOVERY_SIG:-0}" = 1 ]; then
  # Test-only: a structurally valid archive whose recovery signature fails
  # the RSA policy. The archive digest is recomputed below, so the loader
  # admits it and the kernel admission must be the layer that rejects.
  recovery_signature="$out/corrupt-recovery.sig"
  python3 - "$aiueos/kotoba/user-smoke.sig" "$recovery_signature" <<'PYSIG'
from pathlib import Path
import sys
data = bytearray(Path(sys.argv[1]).read_bytes())
data[17] ^= 1
Path(sys.argv[2]).write_bytes(data)
PYSIG
fi
python3 "$aiueos/scripts/make-initramfs.py" \
  --entry "recovery/user-smoke.elf,$aiueos/kotoba/user-smoke.elf" \
  --entry "recovery/user-smoke.sig,$recovery_signature" \
  --entry "recovery/app-catalog.sig,$aiueos/kotoba/app-catalog.sig" \
  --output "$initramfs" >/dev/null
python3 - "$kernel" "$initramfs" "$identity_source" <<'PY'
import hashlib, pathlib, sys
kernel_digest = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).digest()
initramfs_digest = hashlib.sha256(pathlib.Path(sys.argv[2]).read_bytes()).digest()
def values(digest):
    return ",".join(f"0x{byte:02x}" for byte in digest)
pathlib.Path(sys.argv[3]).write_text(
    "#include <stdint.h>\n"
    "const uint8_t aiueos_expected_kernel_sha256[32]={" + values(kernel_digest) + "};\n"
    "const uint8_t aiueos_expected_initramfs_sha256[32]={" + values(initramfs_digest) + "};\n",
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
