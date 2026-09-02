#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}
esp="$out/esp"
efi="$esp/EFI/BOOT/BOOTX64.EFI"
object="$out/uefi-main.obj"
model_slots_object="$out/uefi-model-slots.obj"
identity_source="$out/kernel-identity.c"
identity_object="$out/kernel-identity.obj"
model_identity_header="$out/aiueos-model-identity.h"
build_identity_header="$out/aiueos-build-identity.h"
embedded_source="$out/embedded-release.S"
embedded_object="$out/embedded-release.obj"
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
kernel_rtl8125_object="$out/kernel-rtl8125.o"
kernel_relay_protocol_object="$out/kernel-relay-protocol.o"
kernel_micro_infer_object="$out/kernel-micro-infer.o"
kernel_inference_status_object="$out/kernel-inference-status.o"
kernel_device_result_object="$out/kernel-device-result.o"
kernel_device_worker_protocol_object="$out/kernel-device-worker-protocol.o"
kernel_model_handoff_object="$out/kernel-model-handoff.o"
kernel_qwen35_runtime_object="$out/kernel-qwen35-runtime.o"
kernel_qwen35_quant_object="$out/kernel-qwen35-quant.o"
kernel_qwen35_infer_object="$out/kernel-qwen35-infer.o"
kernel_kototama_runtime_object="$out/kernel-kototama-runtime.o"
kernel_job_protocol_object="$out/kernel-job-protocol.o"
kernel_tls_aes_object="$out/kernel-tls-aes-gcm.o"
kernel_tls13_object="$out/kernel-tls13.o"
kernel_scheduler_object="$out/kernel-scheduler.o"
kernel_syscall_object="$out/kernel-syscall.o"
kernel_process_object="$out/kernel-process.o"
kernel_loader_object="$out/kernel-loader.o"
kernel_smp_object="$out/kernel-smp.o"
kernel_trampoline_object="$out/kernel-ap-trampoline.o"
kernel_ioapic_object="$out/kernel-ioapic.o"
kernel_framebuffer_object="$out/kernel-framebuffer.o"
kernel_qualification_object="$out/kernel-qualification.o"
kernel_qualification_entry_object="$out/kernel-qualification-entry.o"
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
# The RTL8125 2.5GbE driver (ADR-0140). Six objects, always linked: the NIC is
# how a diskless PXE node reaches anything at all, and `kernel/rtl8125.c` is in
# every profile. `aiueos_rtl8125_kotoba_selftest` runs all six against a
# software model of the BAR on every boot, so an unlinked one is a link error
# rather than a silent absence.
kotoba_rtl8125_identify_object=${AIUEOS_KOTOBA_RTL8125_IDENTIFY_OBJECT:-"$aiueos/kotoba/rtl8125-identify.o"}
kotoba_rtl8125_link_up_object=${AIUEOS_KOTOBA_RTL8125_LINK_UP_OBJECT:-"$aiueos/kotoba/rtl8125-link-up.o"}
kotoba_rtl8125_ring_build_object=${AIUEOS_KOTOBA_RTL8125_RING_BUILD_OBJECT:-"$aiueos/kotoba/rtl8125-ring-build.o"}
kotoba_rtl8125_program_object=${AIUEOS_KOTOBA_RTL8125_PROGRAM_OBJECT:-"$aiueos/kotoba/rtl8125-program.o"}
kotoba_rtl8125_tx_submit_object=${AIUEOS_KOTOBA_RTL8125_TX_SUBMIT_OBJECT:-"$aiueos/kotoba/rtl8125-tx-submit.o"}
kotoba_rtl8125_rx_poll_object=${AIUEOS_KOTOBA_RTL8125_RX_POLL_OBJECT:-"$aiueos/kotoba/rtl8125-rx-poll.o"}
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
# tokenizer: the three objects that turn a prompt into token ids and back
# (ADR-0135). They are in the image and NOTHING CALLS THEM YET -- kernel/
# qwen35_infer.c takes a single BOS id and has no tokenizer at all, so there is
# no C function here to replace. Linking them now makes wiring the prompt path
# a change to the C that calls them rather than a change to what the image
# contains, and keeps them under the same per-object export and relocation
# check as every other Kotoba object.
kotoba_qwen35_vocab_index_object=${AIUEOS_KOTOBA_QWEN35_VOCAB_INDEX_OBJECT:-"$aiueos/kotoba/qwen35-vocab-index-build.o"}
kotoba_qwen35_tokenize_object=${AIUEOS_KOTOBA_QWEN35_TOKENIZE_OBJECT:-"$aiueos/kotoba/qwen35-tokenize.o"}
kotoba_qwen35_detokenize_object=${AIUEOS_KOTOBA_QWEN35_DETOKENIZE_OBJECT:-"$aiueos/kotoba/qwen35-detokenize.o"}
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
kotoba_ecdsa_object=${AIUEOS_KOTOBA_ECDSA_OBJECT:-"$aiueos/kotoba/ecdsa-p256.o"}
# Qwen3.8-27B GGUF admission (ADR-0137). Three objects, linked only by the
# model-handoff profile because nothing else parses a model. They replace the
# whole of kernel/qwen35_runtime.c's parser: with
# -DAIUEOS_QWEN35_KOTOBA_ADMISSION that file compiles to buffer plumbing and a
# translation into struct aiueos_qwen35_model, and its C parser is not in the
# image at all.
kotoba_qwen35_header_object=${AIUEOS_KOTOBA_QWEN35_HEADER_OBJECT:-"$aiueos/kotoba/qwen35-gguf-header-valid.o"}
kotoba_qwen35_kv_object=${AIUEOS_KOTOBA_QWEN35_KV_OBJECT:-"$aiueos/kotoba/qwen35-gguf-kv-scan.o"}
kotoba_qwen35_tensor_object=${AIUEOS_KOTOBA_QWEN35_TENSOR_OBJECT:-"$aiueos/kotoba/qwen35-tensor-table-bind.o"}
# The TLS 1.3 AEAD and record layer (ADR-0132, ADR-0133). Both return a REASON
# CODE and zero is success -- the C they replaced returned 1 for success, so a
# transcribed `if (call(...))` accepts exactly what they refuse.
# `hkdf-sha256.o` is deliberately NOT here: it does not return on this machine
# (see the stage-5 ADR), and linking an object nothing may call would put a
# 13 KiB artifact in the image to no end.
kotoba_aes128_gcm_object=${AIUEOS_KOTOBA_AES128_GCM_OBJECT:-"$aiueos/kotoba/aes128-gcm.o"}
kotoba_tls13_record_object=${AIUEOS_KOTOBA_TLS13_RECORD_OBJECT:-"$aiueos/kotoba/tls13-record.o"}
# The two project-route objects (ADR-0141). NOT LINKED, and that is a decision
# about the kernel rather than an omission: nothing in the kernel calls
# `kotoba_aiueos_cid_v1_admit` or `kotoba_aiueos_value_runtime_cas_verify` yet.
# They are verified here anyway, exactly as the qwen35 objects below are,
# because the digest is what ties the committed artifact to the source its
# contract graded -- and until amu#742 neither could be built at all.
kotoba_cid_v1_admit_object=${AIUEOS_KOTOBA_CID_V1_ADMIT_OBJECT:-"$aiueos/kotoba/cid-v1-admit.o"}
kotoba_value_runtime_cas_verify_object=${AIUEOS_KOTOBA_VALUE_RUNTIME_CAS_VERIFY_OBJECT:-"$aiueos/kotoba/value-runtime-cas-verify.o"}
# The Qwen3.5 forward pass, first tranche (ADR-0147). Linked ONLY with the
# model handoff, because that is the only build in which the C they are
# compared against (kernel/qwen35_infer.c, kernel/qwen35_quant.c) is compiled
# at all -- linking them into every image would carry 23 KiB nothing calls.
# All three return a REASON CODE and zero is success, except the dot product,
# whose answer IS the value; see the objects' contracts.
kotoba_qwen35_dot_object=${AIUEOS_KOTOBA_QWEN35_DOT_OBJECT:-"$aiueos/kotoba/qwen35-dot-f32.o"}
kotoba_qwen35_dequant_object=${AIUEOS_KOTOBA_QWEN35_DEQUANT_OBJECT:-"$aiueos/kotoba/qwen35-dequant-row.o"}
kotoba_qwen35_matvec_object=${AIUEOS_KOTOBA_QWEN35_MATVEC_OBJECT:-"$aiueos/kotoba/qwen35-matvec.o"}
# Second tranche (ADR-0148): the scalar functions between the matvecs.
kotoba_qwen35_activation_object=${AIUEOS_KOTOBA_QWEN35_ACTIVATION_OBJECT:-"$aiueos/kotoba/qwen35-activation.o"}
kotoba_qwen35_norm_object=${AIUEOS_KOTOBA_QWEN35_NORM_OBJECT:-"$aiueos/kotoba/qwen35-norm.o"}
kotoba_qwen35_attention_object=${AIUEOS_KOTOBA_QWEN35_ATTENTION_OBJECT:-"$aiueos/kotoba/qwen35-attention.o"}
kotoba_qwen35_recurrent_object=${AIUEOS_KOTOBA_QWEN35_RECURRENT_OBJECT:-"$aiueos/kotoba/qwen35-recurrent-step.o"}
qwen35_kotoba_link=
# device-client: the Murakumo device-P256 worker's signed canonical text,
# v2 and v3 (ADR-0137). Linked unconditionally, not behind
# AIUEOS_MURAKUMO_DEVICE_RESULT, because main.c's DEVCLIENT-PARITY boot
# self-test runs it on every UEFI profile -- the K16 profile is the only one
# that can send a request, and it is the one that cannot be booted here.
kotoba_device_worker_canonical_object=${AIUEOS_KOTOBA_DEVICE_WORKER_CANONICAL_OBJECT:-"$aiueos/kotoba/device-worker-canonical.o"}
# sha-stream: streaming SHA-256 (ADR-0155). Three objects over ONE shared module
# (`kotoba/aiueos/lib/sha256_stream.kotoba`), so the compression rounds exist
# once in source; three symbols because a fuel tier is a per-CALL budget and
# these three calls are not the same size.
#
# `sha256.o` above stays: it is the whole-message object that ten call sites
# in this kernel already use, and nothing here replaces it. What these add is
# the sizes it refuses: its ceiling is 12,288 bytes and `input-sha256` reaches 131,080.
#
# Linked unconditionally, like device-worker-canonical and for the same
# reason: main.c's SHA-STREAM-PARITY boot self-test runs all three on every
# UEFI profile.
kotoba_sha256_stream_object=${AIUEOS_KOTOBA_SHA256_STREAM_OBJECT:-"$aiueos/kotoba/sha256-stream.o"}
kotoba_sha256_region_object=${AIUEOS_KOTOBA_SHA256_REGION_OBJECT:-"$aiueos/kotoba/sha256-region.o"}
kotoba_device_worker_digest_object=${AIUEOS_KOTOBA_DEVICE_WORKER_DIGEST_OBJECT:-"$aiueos/kotoba/device-worker-digest.o"}
# ECDSA P-256 deterministic sign (ADR-0105). Linked ONLY when
# AIUEOS_ECDSA_SIGN_KAT=1 -- it is ~50 KiB and the kernel is near its 1 MiB
# ceiling, so it stays out of the default and the SSH-listener builds until the
# sshd needs it. Regenerate the object with
# scripts/reproduce-ecdsa-sign-object.clj (the pinned amu needs the
# kernel-object-entries + 250M fuel-tier patch that recipe applies).
kotoba_ecdsa_sign_object=${AIUEOS_KOTOBA_ECDSA_SIGN_OBJECT:-"$aiueos/kotoba/ecdsa-p256-sign.o"}
kotoba_ecdsa_public_object=${AIUEOS_KOTOBA_ECDSA_PUBLIC_OBJECT:-"$aiueos/kotoba/ecdsa-p256-public.o"}
ecdsa_sign_link=
ecdsa_public_link=
device_result_link=
# Linked for the sign KAT and, now, for the SSH listener build: net_ssh_kex
# signs the exchange hash H with it (ADR-0107). Listener+sign measured at
# 1,043,688 bytes -- under the 1 MiB ceiling, so ADR-0105's separation is
# lifted for the SSH build (the KAT keeps its own flag for the KAT code only).
if [ "${AIUEOS_ECDSA_SIGN_KAT:-0}" = 1 ] || [ "${AIUEOS_SSH_LISTEN:-0}" = 1 ] ||
   [ "${AIUEOS_MURAKUMO_DEVICE_RESULT:-0}" = 1 ]; then
  ecdsa_sign_link="$kotoba_ecdsa_sign_object"
fi
if [ "${AIUEOS_MURAKUMO_DEVICE_RESULT:-0}" = 1 ] ||
   [ "${AIUEOS_ECDSA_PUBLIC_KAT:-0}" = 1 ]; then
  ecdsa_public_link="$kotoba_ecdsa_public_object"
fi
if [ "${AIUEOS_MURAKUMO_DEVICE_RESULT:-0}" = 1 ]; then
  device_result_link="$kernel_device_result_object $kernel_device_worker_protocol_object"
fi
physical_qualification_cflags=
physical_network_qualification_cflags=
physical_direct_https_qualification_cflags=
physical_relay_qualification_cflags=
physical_job_qualification_cflags=
murakumo_device_result_cflags=
persistent_boot_cflags=
qualification_link=
qualification_gc_link=
gop_discovery_cflags=
loader_failure_test_cflags=
loader_hang_test_cflags=
qualification_watchdog_cflags=
kernel_hang_test_cflags=
embedded_release_cflags=
netboot_qualification_cflags=
embedded_release_link=
if [ "${AIUEOS_PHYSICAL_QUALIFICATION:-0}" = 1 ]; then
  physical_qualification_cflags="-DAIUEOS_PHYSICAL_QUALIFICATION=1"
  qualification_link="$kernel_qualification_entry_object $kernel_qualification_object"
  qualification_gc_link="--gc-sections"
fi
if [ "${AIUEOS_PERSISTENT_BOOT:-0}" = 1 ]; then
  [ "${AIUEOS_PHYSICAL_QUALIFICATION:-0}" = 1 ] || {
    echo "error: persistent boot requires physical qualification" >&2
    exit 1
  }
  persistent_boot_cflags="-DAIUEOS_PERSISTENT_BOOT=1"
fi
if [ "${AIUEOS_PHYSICAL_NETWORK_QUALIFICATION:-0}" = 1 ]; then
  [ "${AIUEOS_PHYSICAL_QUALIFICATION:-0}" = 1 ] || {
    echo "error: physical network qualification requires physical qualification" >&2
    exit 1
  }
  physical_network_qualification_cflags="-DAIUEOS_PHYSICAL_NETWORK_QUALIFICATION=1"
fi
if [ "${AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION:-0}" = 1 ]; then
  [ "${AIUEOS_PHYSICAL_NETWORK_QUALIFICATION:-0}" = 1 ] || {
    echo "error: physical direct HTTPS qualification requires physical network qualification" >&2
    exit 1
  }
  physical_direct_https_qualification_cflags="-DAIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION=1"
fi
if [ -n "$device_result_link" ]; then
  [ "${AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION:-0}" = 1 ] || {
    echo "error: Murakumo device result requires physical direct HTTPS" >&2
    exit 1
  }
  [ "${AIUEOS_QWEN38_MODEL_HANDOFF:-0}" = 1 ] || {
    echo "error: Murakumo device result requires the exact Qwen3.8 model handoff" >&2
    exit 1
  }
  [ "${AIUEOS_MODEL_TEST_FIXTURE:-0}" != 1 ] || {
    echo "error: Murakumo device result refuses a model test fixture" >&2
    exit 1
  }
  murakumo_device_result_cflags="-DAIUEOS_MURAKUMO_DEVICE_RESULT=1"
fi
if [ "${AIUEOS_PHYSICAL_RELAY_QUALIFICATION:-0}" = 1 ]; then
  [ "${AIUEOS_PHYSICAL_NETWORK_QUALIFICATION:-0}" = 1 ] || {
    echo "error: physical relay qualification requires physical network qualification" >&2
    exit 1
  }
  physical_relay_qualification_cflags="-DAIUEOS_PHYSICAL_RELAY_QUALIFICATION=1"
fi
if [ "${AIUEOS_PHYSICAL_JOB_QUALIFICATION:-0}" = 1 ]; then
  [ "${AIUEOS_PHYSICAL_RELAY_QUALIFICATION:-0}" = 1 ] || {
    echo "error: physical job qualification requires physical relay qualification" >&2
    exit 1
  }
  physical_job_qualification_cflags="-DAIUEOS_PHYSICAL_JOB_QUALIFICATION=1"
fi
if [ "${AIUEOS_EMBEDDED_RELEASE:-0}" = 1 ]; then
  embedded_release_cflags="-DAIUEOS_EMBEDDED_RELEASE=1"
  embedded_release_link="$embedded_object"
fi
if [ "${AIUEOS_NETBOOT_QUALIFICATION:-0}" = 1 ]; then
  [ "${AIUEOS_PHYSICAL_QUALIFICATION:-0}" = 1 ] || {
    echo "error: netboot qualification requires AIUEOS_PHYSICAL_QUALIFICATION=1" >&2
    exit 1
  }
  [ "${AIUEOS_EMBEDDED_RELEASE:-0}" = 1 ] || {
    echo "error: netboot qualification requires AIUEOS_EMBEDDED_RELEASE=1" >&2
    exit 1
  }
  netboot_qualification_cflags="-DAIUEOS_NETBOOT_QUALIFICATION=1"
fi
if [ "${AIUEOS_GOP_FORCE_PROTOCOL_SCAN:-0}" = 1 ]; then
  gop_discovery_cflags="-DAIUEOS_GOP_FORCE_PROTOCOL_SCAN=1"
fi
if [ -n "${AIUEOS_QUALIFICATION_FORCE_LOADER_FAILURE_CODE:-}" ]; then
  loader_failure_test_cflags="-DAIUEOS_QUALIFICATION_FORCE_LOADER_FAILURE_CODE=${AIUEOS_QUALIFICATION_FORCE_LOADER_FAILURE_CODE}"
fi
qualification_watchdog_seconds=${AIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS:-}
if [ -n "${AIUEOS_QUALIFICATION_FORCE_LOADER_HANG_CODE:-}" ]; then
  loader_hang_test_cflags="-DAIUEOS_QUALIFICATION_FORCE_LOADER_HANG_CODE=${AIUEOS_QUALIFICATION_FORCE_LOADER_HANG_CODE}"
  qualification_watchdog_seconds=${qualification_watchdog_seconds:-3}
fi
if [ -n "$qualification_watchdog_seconds" ]; then
  case "$qualification_watchdog_seconds" in
    *[!0-9]*|'')
      echo "error: qualification loader watchdog seconds must be an integer" >&2
      exit 1
      ;;
  esac
  [ "$qualification_watchdog_seconds" -ge 1 ] &&
    [ "$qualification_watchdog_seconds" -le 3600 ] || {
      echo "error: qualification loader watchdog seconds must be 1..3600" >&2
      exit 1
    }
  [ "${AIUEOS_PHYSICAL_QUALIFICATION:-0}" = 1 ] || {
    echo "error: qualification loader watchdog requires physical qualification" >&2
    exit 1
  }
  [ "${AIUEOS_PERSISTENT_BOOT:-0}" != 1 ] || {
    echo "error: persistent boot disables the loader watchdog" >&2
    exit 1
  }
  qualification_watchdog_cflags="-DAIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS=${qualification_watchdog_seconds}"
fi
if [ -n "${AIUEOS_QUALIFICATION_FORCE_KERNEL_HANG_CODE:-}" ]; then
  kernel_hang_test_cflags="-DAIUEOS_QUALIFICATION_FORCE_KERNEL_HANG_CODE=${AIUEOS_QUALIFICATION_FORCE_KERNEL_HANG_CODE}"
fi
kotoba_ime_object=${AIUEOS_KOTOBA_IME_OBJECT:-"$aiueos/kotoba/ime-romaji.o"}
kotoba_wm_object=${AIUEOS_KOTOBA_WM_OBJECT:-"$aiueos/kotoba/wm-hit.o"}
kotoba_scanout_object=${AIUEOS_KOTOBA_SCANOUT_OBJECT:-"$aiueos/kotoba/scanout-bind.o"}
kotoba_broker_object=${AIUEOS_KOTOBA_BROKER_OBJECT:-"$aiueos/kotoba/broker-admit.o"}
kotoba_session_object=${AIUEOS_KOTOBA_SESSION_OBJECT:-"$aiueos/kotoba/session-restore.o"}
kotoba_net_arp_object=${AIUEOS_KOTOBA_NET_ARP_OBJECT:-"$aiueos/kotoba/net-arp-reply-valid.o"}
kotoba_ipv4_checksum_object=${AIUEOS_KOTOBA_IPV4_CHECKSUM_OBJECT:-"$aiueos/kotoba/ipv4-checksum.o"}
kotoba_ipv4_icmp_object=${AIUEOS_KOTOBA_IPV4_ICMP_OBJECT:-"$aiueos/kotoba/ipv4-icmp-reply-valid.o"}
kotoba_tcp_checksum_object=${AIUEOS_KOTOBA_TCP_CHECKSUM_OBJECT:-"$aiueos/kotoba/tcp-checksum-ok.o"}
kotoba_tcp_segment_object=${AIUEOS_KOTOBA_TCP_SEGMENT_OBJECT:-"$aiueos/kotoba/tcp-segment-valid.o"}
kotoba_dhcp_reply_object=${AIUEOS_KOTOBA_DHCP_REPLY_OBJECT:-"$aiueos/kotoba/dhcp-reply-valid.o"}
kotoba_dhcp_option_object=${AIUEOS_KOTOBA_DHCP_OPTION_OBJECT:-"$aiueos/kotoba/dhcp-option-u32.o"}
kotoba_user_elf=${AIUEOS_KOTOBA_USER_ELF:-"$aiueos/kotoba/user-smoke.elf"}
kotoba_fnv_sha=
if [ -z "${AIUEOS_KOTOBA_FNV_OBJECT:-}" ]; then
  kotoba_fnv_sha=bd1ea7d065ab9c306de56074606ab66d8a409fc5d99e36cae2b77b99f5f16ce0
fi
kotoba_journal_sha=
if [ -z "${AIUEOS_KOTOBA_JOURNAL_OBJECT:-}" ]; then
  kotoba_journal_sha=6983847499117a5bfeb2194490290794eed792e405a8b3b52b5f808c1a2461d3
fi
kotoba_kernel_sha=
if [ -z "${AIUEOS_KOTOBA_KERNEL_OBJECT:-}" ]; then
  kotoba_kernel_sha=e230c792ebe8e983bb68a86b913f121afeb50e834032ff499f5f35e4a7a01002
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
# ADR-0199 red/green.  1 raises a #UD at the earliest point the early fatal
# IDT covers; 2 raises the same instruction with that table compiled out,
# which is what every boot did before ADR-0199.  Never set in production.
if [ "${AIUEOS_EARLY_FAULT_SMOKE:-0}" != 0 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_EARLY_FAULT_SMOKE=${AIUEOS_EARLY_FAULT_SMOKE}"
  if [ "${AIUEOS_EARLY_FAULT_SMOKE}" = 2 ]; then
    input_smoke_cflags="$input_smoke_cflags -DAIUEOS_SKIP_EARLY_FATAL_IDT=1"
  fi
fi
# Test-only. Breaks a received DHCP reply in exactly ONE way so the gate can
# show the admission refusing it, and refusing it for the reason that was
# broken. Only kernel/pci.c is compiled with it; a value of 0 or an unset
# variable compiles none of the tampering in at all.
if [ -n "${AIUEOS_DHCP_TAMPER:-}" ] && [ "${AIUEOS_DHCP_TAMPER}" != 0 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_DHCP_TAMPER=${AIUEOS_DHCP_TAMPER}"
fi
# Test-only. Compiles the SSH passive-open listener (pci.c) and its evidence
# marker (main.c) into the kernel. Off by default so every existing gate
# builds the exact kernel it built before; the SSH gate sets it. Both main.c
# and pci.c receive input_smoke_cflags, so one flag reaches both.
if [ "${AIUEOS_SSH_LISTEN:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_SSH_LISTEN=1"
  if [ "${AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION:-0}" = 1 ]; then
    [ "${AIUEOS_MURAKUMO_DEVICE_RESULT:-0}" = 1 ] || {
      echo "error: physical SSH requires the Device-P256 identity" >&2
      exit 1
    }
    [ -n "${AIUEOS_SSH_AUTHORIZED_KEY_HEX:-}" ] || {
      echo "error: physical SSH requires AIUEOS_SSH_AUTHORIZED_KEY_HEX (public x||y only)" >&2
      exit 1
    }
  fi
  if [ -n "${AIUEOS_SSH_AUTHORIZED_KEY_HEX:-}" ]; then
    python3 "$aiueos/scripts/write-ssh-authorized-key-header.py" \
      "$AIUEOS_SSH_AUTHORIZED_KEY_HEX" "$out/aiueos-ssh-authorized-key.h"
    input_smoke_cflags="$input_smoke_cflags -DAIUEOS_SSH_AUTHORIZED_KEY_HEADER=1"
  fi
fi
# The ECDSA sign known-answer test (main.c). Its own flag so the ~50 KiB sign
# object is only linked here, not in the SSH-listener build near the 1 MiB
# ceiling (ADR-0105).
if [ "${AIUEOS_ECDSA_SIGN_KAT:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_ECDSA_SIGN_KAT=1"
fi
if [ "${AIUEOS_ECDSA_PUBLIC_KAT:-0}" = 1 ]; then
  input_smoke_cflags="$input_smoke_cflags -DAIUEOS_ECDSA_PUBLIC_KAT=1"
fi

# Pure-AIUEOS Qwen handoff. Production values are pinned and cannot be
# overridden accidentally; the tiny overrides exist only for the QEMU gate and
# require the explicit test-fixture switch.
model_handoff_cflags=
model_handoff_link=
qwen35_smp_cflags=
qwen35_vector_cflags=
model_slots_cflags=
model_slots_link=
model_total=${AIUEOS_MODEL_TOTAL_BYTES:-10934860704}
model_part0=${AIUEOS_MODEL_PART0_BYTES:-4000000000}
model_part1=${AIUEOS_MODEL_PART1_BYTES:-4000000000}
model_part2=${AIUEOS_MODEL_PART2_BYTES:-2934860704}
model_sha256=${AIUEOS_MODEL_SHA256:-c0b7c3038681ed2e3040456c1dd45f9858b6c2290bed172c70388a94874f3eee}
model_min_address=${AIUEOS_MODEL_MIN_ADDRESS:-4294967296}
model_max_address=${AIUEOS_MODEL_MAX_ADDRESS:-68719476735}
# The Kotoba/C parity self-test for the Qwen3.5 forward-pass objects
# (ADR-0147).  A SEPARATE flag from AIUEOS_QWEN38_MODEL_HANDOFF on purpose: the
# handoff makes the UEFI loader demand a 10,934,860,704-byte model mapping and
# refuse the boot without one (AIUEOS_LOADER_FAIL qwen38-model-admission
# code=121, measured 2026-09-02), while this comparison needs no model -- it is
# bit-equality of the arithmetic over synthetic inputs.  It compiles the same
# two C files the handoff does, so the reference half of the comparison is the
# code the model path actually runs.
qwen35_parity_cflags=
qwen35_parity_link=
case "${AIUEOS_QWEN35_KOTOBA_PARITY:-0}" in
  0) ;;
  1|2|3|4) qwen35_parity_cflags="-DAIUEOS_QWEN35_KOTOBA_PARITY=${AIUEOS_QWEN35_KOTOBA_PARITY}" ;;
  *) echo "error: AIUEOS_QWEN35_KOTOBA_PARITY must be 0, 1, 2, 3 or 4" >&2; exit 1 ;;
esac
if [ "${AIUEOS_QWEN38_MODEL_HANDOFF:-0}" = 1 ] ||
   [ "${AIUEOS_MODEL_NVME_SLOTS:-0}" = 1 ]; then
  if [ "${AIUEOS_MODEL_TEST_FIXTURE:-0}" != 1 ]; then
    [ "$model_total" = 10934860704 ] &&
    [ "$model_part0" = 4000000000 ] &&
    [ "$model_part1" = 4000000000 ] &&
    [ "$model_part2" = 2934860704 ] &&
    [ "$model_sha256" = c0b7c3038681ed2e3040456c1dd45f9858b6c2290bed172c70388a94874f3eee ] &&
    [ "$model_min_address" = 4294967296 ] &&
    [ "$model_max_address" = 68719476735 ] || {
      echo "error: production Qwen handoff identity is pinned; overrides require AIUEOS_MODEL_TEST_FIXTURE=1" >&2
      exit 1
    }
  fi
  for value in "$model_total" "$model_part0" "$model_part1" "$model_part2" \
               "$model_min_address" "$model_max_address"; do
    case "$value" in ''|*[!0-9]*) echo "error: model identity values must be decimal integers" >&2; exit 1 ;; esac
  done
  case "$model_sha256" in
    *[!0-9a-f]*|'') echo "error: model SHA-256 must be lowercase hexadecimal" >&2; exit 1 ;;
  esac
  [ "${#model_sha256}" -eq 64 ] || { echo "error: model SHA-256 must contain 64 hex digits" >&2; exit 1; }
  [ $((model_part0 + model_part1 + model_part2)) -eq "$model_total" ] || {
    echo "error: model part lengths do not sum to total bytes" >&2; exit 1;
  }
  if [ "${AIUEOS_QWEN38_MODEL_HANDOFF:-0}" = 1 ]; then
    model_handoff_cflags="-DAIUEOS_QWEN38_MODEL_HANDOFF=1"
    case "${AIUEOS_QWEN35_SMP:-1}" in
      1) qwen35_smp_cflags="-DAIUEOS_QWEN35_SMP=1" ;;
      0) qwen35_smp_cflags= ;;
      *) echo "error: AIUEOS_QWEN35_SMP must be 0 or 1" >&2; exit 1 ;;
    esac
    case "${AIUEOS_QWEN35_AVX2:-1}" in
      1) qwen35_vector_cflags= ;;
      0) qwen35_vector_cflags="-DAIUEOS_QWEN35_SCALAR=1" ;;
      *) echo "error: AIUEOS_QWEN35_AVX2 must be 0 or 1" >&2; exit 1 ;;
    esac
    if [ "${AIUEOS_MODEL_TEST_FIXTURE:-0}" = 1 ]; then
      model_handoff_cflags="$model_handoff_cflags -DAIUEOS_MODEL_TEST_FIXTURE=1"
    fi
    model_handoff_link="$kernel_model_handoff_object $kernel_qwen35_runtime_object $kernel_qwen35_quant_object $kernel_qwen35_infer_object $kotoba_qwen35_header_object $kotoba_qwen35_kv_object $kotoba_qwen35_tensor_object"
  fi
  if [ "${AIUEOS_MODEL_NVME_SLOTS:-0}" = 1 ]; then
    model_slots_cflags="-DAIUEOS_MODEL_NVME_SLOTS=1"
    if [ "${AIUEOS_MODEL_NVME_TARGET_OPTIONAL:-0}" = 1 ]; then
      model_slots_cflags="$model_slots_cflags -DAIUEOS_MODEL_NVME_TARGET_OPTIONAL=1"
    fi
    if [ "${AIUEOS_MODEL_SLOT_IMPORT_EXIT:-0}" = 1 ]; then
      [ "${AIUEOS_MODEL_TEST_FIXTURE:-0}" = 1 ] || {
        echo "error: model-slot import exit is test-fixture only" >&2
        exit 1
      }
      model_slots_cflags="$model_slots_cflags -DAIUEOS_MODEL_SLOT_IMPORT_EXIT=1"
    fi
    model_slots_link="$model_slots_object"
  fi
elif [ "${AIUEOS_MODEL_NVME_TARGET_OPTIONAL:-0}" = 1 ]; then
  echo "error: optional NVMe model target requires AIUEOS_MODEL_NVME_SLOTS=1" >&2
  exit 1
elif [ "${AIUEOS_MODEL_TEST_FIXTURE:-0}" = 1 ]; then
  echo "error: model test fixture requires a model handoff or NVMe-slot build" >&2
  exit 1
fi

command -v zig >/dev/null 2>&1 || {
  echo "error: Zig is required to build the freestanding UEFI application" >&2
  exit 1
}

mkdir -p "$(dirname -- "$efi")" "$kernel_dir"

# ---------------------------------------------------------------------------
# AIUEOS_K16_PURE_NATIVE=1 -- the pure Kotoba/Amu profile (ADR-0131).
#
# This profile does NOT produce a bootable image and is not trying to. What it
# does is make the Kotoba/foreign boundary MACHINE-CHECKED instead of stated:
#
#   (i)   the kernel link input is restricted to Kotoba objects and Amu
#         toolchain stubs -- the 33 C/ASM objects the ordinary link carries are
#         not compiled and not linked;
#   (ii)  the exact link list is handed to k16-pure-native-gate.cljs BEFORE
#         `zig ld.lld` runs, and a non-zero exit aborts the build;
#   (iii) the artifacts are named aiueos-k16-pure-native-* and the gate receipt
#         is written next to them;
#   (iv)  BOOTX64.EFI would have to come from a Kotoba/Amu artifact. None
#         exists: `uefi/main.c` is the loader. So this profile REFUSES to emit
#         a loader rather than shipping the C one under a "pure native" name.
#
# The refusal is conditional on a measurement, not hard-coded: set
# AIUEOS_KOTOBA_LOADER_EFI to a Kotoba/Amu-produced PE32+ image and this stops
# refusing on that ground. Until such an artifact exists, the honest output of
# this profile is a refusal and a count.
#
# The link list is DERIVED from the production `zig ld.lld` invocation below
# rather than restated here. A second copy of that list is a copy that can fall
# out of step with it silently, and a gate over the wrong list is worse than no
# gate at all.
# ---------------------------------------------------------------------------
if [ "${AIUEOS_K16_PURE_NATIVE:-0}" = 1 ]; then
  command -v nbb >/dev/null 2>&1 || {
    echo "error: the K16 pure-native profile needs nbb to run its gate" >&2
    exit 2
  }
  pure_prefix="$out/aiueos-k16-pure-native"
  pure_list="$pure_prefix-link-list.txt"
  pure_receipt="$pure_prefix-receipt.edn"
  pure_kernel="$pure_prefix-kernel.elf"
  pure_vars=$(sed -n '/^zig ld\.lld /,/^fi$/p' "$0" \
    | grep -o '\$kotoba_[A-Za-z0-9_]*_object' | sed 's/^\$//')
  [ -n "$pure_vars" ] || {
    echo "error: could not derive the Kotoba link list from $0" >&2
    exit 2
  }
  : > "$pure_list"
  for pure_var in $pure_vars; do
    eval "printf '%s\n' \"\$$pure_var\"" >> "$pure_list"
  done
  # The two conditionally linked Kotoba objects reach the production link
  # through these variables rather than by name, so they are appended the same
  # way the link line reads them.
  for pure_extra in $ecdsa_sign_link $ecdsa_public_link; do
    printf '%s\n' "$pure_extra" >> "$pure_list"
  done
  echo "AIUEOS_K16_PURE_NATIVE profile: kernel link restricted to Kotoba objects + Amu stubs"
  echo "AIUEOS_K16_PURE_NATIVE link-list=$pure_list entries=$(wc -l < "$pure_list" | tr -d ' ')"
  pure_gate_status=0
  # The provenance manifest is overridable so the profile's ADMITTED branch can
  # be exercised. It is not a way to wave objects through: the override is still
  # a manifest the gate checks digest-for-digest against the bytes on disk.
  pure_provenance=${AIUEOS_K16_PURE_NATIVE_PROVENANCE:-"$aiueos/kotoba/provenance.edn"}
  nbb "$aiueos/scripts/k16-pure-native-gate.cljs" \
    --link-list "$pure_list" \
    --provenance "$pure_provenance" \
    --receipt-out "$pure_receipt" \
    --root "$repo" || pure_gate_status=$?
  if [ "$pure_gate_status" = 0 ]; then
    zig ld.lld -nostdlib -static --strip-all -z max-page-size=0x1000 \
      -T "$aiueos/kernel/linker.ld" -o "$pure_kernel" @"$pure_list" \
      || {
        echo "AIUEOS_K16_PURE_NATIVE_LINK_FAILED kernel=$pure_kernel" >&2
        pure_gate_status=3
      }
  else
    echo "AIUEOS_K16_PURE_NATIVE gate refused; kernel not linked" >&2
  fi
  pure_kernel_state=not-linked
  if [ -f "$pure_kernel" ]; then
    pure_kernel_state=linked
  fi
  if [ -n "${AIUEOS_KOTOBA_LOADER_EFI:-}" ] && [ -f "${AIUEOS_KOTOBA_LOADER_EFI:-}" ]; then
    cp "$AIUEOS_KOTOBA_LOADER_EFI" "$pure_prefix-BOOTX64.EFI"
    pure_loader_state=copied
  else
    echo "REFUSED foreign-code: uefi/main.c"
    pure_loader_state=refused
    if [ "$pure_gate_status" = 0 ]; then
      pure_gate_status=3
    fi
  fi
  echo "AIUEOS_K16_PURE_NATIVE_INCOMPLETE loader=$pure_loader_state kernel=$pure_kernel_state bootable=no receipt=$pure_receipt"
  echo "This profile cannot produce a bootable K16 image today. It exists so that" >&2
  echo "every later stream can measure progress against a machine-checked boundary." >&2
  exit "$pure_gate_status"
fi

build_version=$(tr -d '\r\n' < "$aiueos/VERSION")
build_source_commit=$(git -C "$repo" rev-parse HEAD)
build_source_dirty=false
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
  build_source_dirty=true
fi
python3 - "$build_identity_header" "$build_version" \
  "$build_source_commit" "$build_source_dirty" <<'PYBUILD'
from pathlib import Path
import re
import sys

out, version, commit, dirty = sys.argv[1:]
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?", version):
    raise SystemExit("error: os/aiueos/VERSION must be a bounded SemVer string")
if not re.fullmatch(r"[0-9a-f]{40}", commit):
    raise SystemExit("error: AIUEOS source commit must be a full lowercase git hash")
suffix = "-DIRTY" if dirty == "true" else ""
Path(out).write_text(
    "#ifndef AIUEOS_BUILD_IDENTITY_H\n#define AIUEOS_BUILD_IDENTITY_H\n"
    f'#define AIUEOS_BUILD_VERSION "{version}"\n'
    f'#define AIUEOS_BUILD_SOURCE_HASH "{commit[:12]}"\n'
    f'#define AIUEOS_BUILD_DIRTY_SUFFIX "{suffix}"\n'
    "#endif\n", encoding="ascii")
PYBUILD
python3 - "$model_identity_header" "$model_total" "$model_part0" \
  "$model_part1" "$model_part2" "$model_sha256" "$model_min_address" \
  "$model_max_address" <<'PYMODEL'
from pathlib import Path
import sys

out, total, part0, part1, part2, digest, minimum, maximum = sys.argv[1:]
values = ",".join(f"0x{int(digest[i:i+2], 16):02x}" for i in range(0, 64, 2))
Path(out).write_text(
    "#ifndef AIUEOS_MODEL_IDENTITY_H\n#define AIUEOS_MODEL_IDENTITY_H\n"
    "#include <stdint.h>\n"
    f"#define AIUEOS_MODEL_TOTAL_BYTES {total}ULL\n"
    f"#define AIUEOS_MODEL_PART0_BYTES {part0}ULL\n"
    f"#define AIUEOS_MODEL_PART1_BYTES {part1}ULL\n"
    f"#define AIUEOS_MODEL_PART2_BYTES {part2}ULL\n"
    "#define AIUEOS_MODEL_PART_COUNT 3U\n"
    f"#define AIUEOS_MODEL_MIN_ADDRESS {minimum}ULL\n"
    f"#define AIUEOS_MODEL_MAX_ADDRESS {maximum}ULL\n"
    "static const uint8_t aiueos_expected_model_sha256[32]={" + values + "};\n"
    "#endif\n", encoding="ascii")
PYMODEL
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_kernel_object" "$kotoba_kernel_sha"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_object" \
  "$kotoba_journal_sha" kotoba_aiueos_journal_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_fnv_object" \
  "$kotoba_fnv_sha" kotoba_aiueos_fnv1a
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_valid_object" \
  c6dfb5f85029fd1f32a9fb84e6060f0d6287e3b018c24aa1cdf666870e417d62 \
  kotoba_aiueos_journal_record_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_valid_object" \
  503260e5548a48ede216ac6413a99dd4a26d7db2a32a2109cdfae7b93a05cc4c \
  kotoba_aiueos_object_transaction_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_transaction_route_object" \
  913ac071a4e19423c89fa7d0da20fbba0a0861d41e49ebe23e8397f2a08856ed \
  kotoba_aiueos_object_transaction_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_valid_object" \
  22fc8573f63decf753f4bc042d5e1253442a690842ab9923ca6b0a9020c0122e \
  kotoba_aiueos_mutable_object_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_superblock_valid_object" \
  41c5efc78b799b3121978f184959f8c02ffd1a3d6a65ac1f661684d1f050feb6 \
  kotoba_aiueos_superblock_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_journal_build_object" \
  d34637fe845cede481ee5bdd23fc6297d9a4933191fefba4bb6e8c26df7d1b7a \
  kotoba_aiueos_journal_record_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mutable_build_object" \
  a9cbcc5ebefbe8615de9d291d6bffd95119b591540acdcb9041e43e4ebbd595f \
  kotoba_aiueos_mutable_object_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cap_valid_object" \
  c13ec58990659e864cc2a347bc79b3644e40807c7545dbb49852c397bf5d52e0 \
  kotoba_aiueos_virtio_cap_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_extent_valid_object" \
  a2666dfadf05361a7e0ad06cd4d24d55dc75b0e06f9323ae86f1ac8a29a695c5 \
  kotoba_aiueos_pci_extent_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_region_valid_object" \
  a96c6589752121b11e915728a33ca0d78805d0bdf3c80852a35d288d12b22bb6 \
  kotoba_aiueos_pci_region_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_pci_config_read_object" \
  dc724f88d81dda6958cc41faaa7fe53245426945cb7b2d66c9c6b420e4c83ed7 \
  kotoba_aiueos_pci_config_read
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_pci_config_write_object" \
  1259a136aa27bda936cb777d5bd7eaaea478137185d66d8f1aecfbfd53c7a458 \
  kotoba_aiueos_pci_config_write
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mmio_map_admit_object" \
  0df513684b67fe6bb2bbec701b11e153423c392f40eb539cf108c191822a5c47 \
  kotoba_aiueos_mmio_map_admit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_acpi_checksum_object" \
  a7652a850836fc2a6d1eb618cf2c7bc61125fcff1c0e0311a9d5aec33fe56a72 \
  kotoba_aiueos_acpi_checksum_ok
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_acpi_table_valid_object" \
  572f13213143a316884f6d8d85f392ec636cb951319f31714b5c6333ed11e166 \
  kotoba_aiueos_acpi_table_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_vtd_admit_object" \
  b2cf5a9a469f8fe50a3336c96fbf1b55fea424bb4c03fddd6e56bb550510ca02 \
  kotoba_aiueos_vtd_admit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_msr_read_object" \
  0bb9f53dcc719f5ba142ef890c6d85d023a8d82062e08b2eb4b8494aff58919b \
  kotoba_aiueos_msr_read
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_msr_write_object" \
  dfc03c6e48813242ba072a2288851d13b4daa1e2cd1af2b618c0bfa763462249 \
  kotoba_aiueos_msr_write
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_idt_gate_object" \
  e96cfccddf68bd877f11177994f3bc4a7f77e4d0db86fc3fa46b01f402c23fd0 \
  kotoba_aiueos_idt_gate_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_pic_disable_object" \
  ed310cc84e041d0b39dd419ef9ceadd1bd37d60b4e1e1cc449621597044dfde2 \
  kotoba_aiueos_pic_disable
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cpu_feature_nx_object" \
  ee2be68aff236cf140327833baa9ab15c6ef8561816296441eb55094372c83f4 \
  kotoba_aiueos_cpu_feature_nx
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cpu_feature_syscall_object" \
  211c2384278813e7d6f6ca3b0e86ce5780ab72df976d42d07231c40c89cce21d \
  kotoba_aiueos_cpu_feature_syscall
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cpu_apic_id_object" \
  d6f28fdfc7a8df6767c655342bd81abad806ee715cbb579b279f3c2947419b2b \
  kotoba_aiueos_cpu_apic_id
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_syscall_range_object" \
  1a25f96061a6990bbdf3564b2afe8c27644bdb86b2517fddd231baf0af9c5d81 \
  kotoba_aiueos_syscall_range_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_copy_in_object" \
  2e3f0cc18348d8748edcd879979a6ebcf7a706b0044e8264d18abe2f424715b3 \
  kotoba_aiueos_copy_in
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_capability_object" \
  0f2b8f694bc9c79b2c6ea155c81d5d6855a1f346beb89c903dc90528b1c6d222 \
  kotoba_aiueos_capability_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_capability_mutation_object" \
  ef16ab4b0720cbbb1a28071499e28aebf6c30accec7ff457b697d430514c6459 \
  kotoba_aiueos_capability_mutation_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_lifecycle_object" \
  ca63a6fe25c3373741057d9f17bf9ff184d19a92da61e9f5c1a3677915a5eaae \
  kotoba_aiueos_service_lifecycle
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_object" \
  c0bbb2bd36658a9099eee50502c6110c882cdcd7fdec066618fb3012ef582e6e \
  kotoba_aiueos_service_registry_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_registry_state_object" \
  1bcfc41ed3549c7a0ccf4a96e861a363abf6e524c1ff35c0c8164279d7a6abad \
  kotoba_aiueos_service_registry_state
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_object" \
  9cec3d6655c9b2876585fea896ae5f5f07fd6a1b111e533ed18534fbb8698712 \
  kotoba_aiueos_user_object_journal_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_valid_object" \
  d1faea1e1446651bfa7cbf1927b6b2add539dd6fde47b781f8bab7a933f0e30d \
  kotoba_aiueos_user_object_journal_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_object_journal_value_object" \
  d96960555b01642a1b2204b2e2b73a0d33de522ce01293ba5fca64c4338994a6 \
  kotoba_aiueos_user_object_journal_value
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_sha256_object" \
  af378b061725473bf4aa66d02d276973ffc5c7cef4b0ed1f4a0e01fc754a7753 \
  kotoba_aiueos_sha256
# tokenizer: same check as every object above -- one export, zero imports, one
# relocation into its own .data, and the sha256 of the artifact this tree
# carries.
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_vocab_index_object" \
  dc58fefacf66431ddcd493038bdc42c5cf4ae1dce29d821cb1d0d9bf4c29cefd \
  kotoba_aiueos_qwen35_vocab_index_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_tokenize_object" \
  bef86908cb7ba9c8dc620fbb8cd36685d996f802baf96436247157337662133a \
  kotoba_aiueos_qwen35_tokenize
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_detokenize_object" \
  3d11e2900be3799ab3b3101c816c5e3a3c896083ac7e0c3d92d298cdc43fe392 \
  kotoba_aiueos_qwen35_detokenize
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_digest_equal_object" \
  6156db8b78f883610521ac4eb458cb98df655b26087e7d6808279c8b9d927b78 \
  kotoba_aiueos_digest_equal
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_catalog_valid_object" \
  cc965586258815beeac08c6cf2ca4debcd5fa7ea99f9d230452fe6eab962e7d6 \
  kotoba_aiueos_app_catalog_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_app_lookup_object" \
  ca8a261b5a967237c602912c1f945404baa5b6e9aba678fe37450633dcd587d9 \
  kotoba_aiueos_app_lookup_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_elf_valid_object" \
  d79cc375b46a6bc7c482e05c9f2e859f62c7a6ce186a8762b5161c2f2a426534 \
  kotoba_aiueos_user_elf_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_user_context_object" \
  66955e000a4b5f8a80ab97c031522e32e427cc141c6f9702f956aabce66d657f \
  kotoba_aiueos_user_context_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_kernel_context_object" \
  0f1364422887e9de0f40a084d4efc24ef670172926ffc63a3b0a7746145dc2a4 \
  kotoba_aiueos_kernel_context_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_mapping_plan_object" \
  34de7a17f3bd3d1314815bc2ebab86d668001707b74164cbaaacef966b8d5ef1 \
  kotoba_aiueos_page_mapping_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_process_plan_object" \
  d9fcc49f43351e666636f0145bd15e67e8054f1bb2a453c687e6d5dc9a0c42ce \
  kotoba_aiueos_process_create_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_teardown_plan_object" \
  bd3e12cd46665caedb36ee6e836644309874fdbf67a8d7702e206833cb46f6e7 \
  kotoba_aiueos_process_teardown_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_task_plan_object" \
  df56b4eb01d25d512da5d9db8352299f66d2dd8c6e794129d3e40dfb88a23cb5 \
  kotoba_aiueos_task_slot_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_dispatch_plan_object" \
  fb74d5e17d67d6e494601bc523a616e498e80c09d5f76bf802a268b418f3fd14 \
  kotoba_aiueos_scheduler_dispatch_plan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_exit_route_object" \
  db3c7e138fec3f34593d717ecd250ca9297f5eb5d653622565bb74d3ccd595a9 \
  kotoba_aiueos_task_exit_route
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_service_task_object" \
  8b2794d08387dfb55c739ccf8e7082425282fb0975b31b14090cc57e4dcac854 \
  kotoba_aiueos_service_task_transition
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_rsa2048_object" \
  b48dc4edec96fc109d89570bbd872d0dc525a1b536ee84dc90c1c1671c6d15e9 \
  kotoba_aiueos_rsa2048_sha256_verify
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_net_arp_object" \
  150dc02d7c875bdf228f6023830aeb73918cbe1089c0c5e8491c6173e8f0ed38 \
  kotoba_aiueos_net_arp_reply_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ipv4_checksum_object" \
  fc5a25ee36559d7d03831721cca41a71f3f3c41b6c3f459893b84169bd2565c1 \
  kotoba_aiueos_ipv4_checksum
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ipv4_icmp_object" \
  da83994ce5e6d52594badc13a5abc2d7448640d45c3dac9465b71d41a78c47fd \
  kotoba_aiueos_ipv4_icmp_reply_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_tcp_checksum_object" \
  2ec2c58715ae7c60846d9fc4a9788ec90b20f0ae7c0ac3229ee913a63ea07ed5 \
  kotoba_aiueos_tcp_checksum_ok
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_tcp_segment_object" \
  887ac75e01b396acd5dd694c3aa25c82295ca196726d0487cfcfc88d4c750b55 \
  kotoba_aiueos_tcp_segment_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_dhcp_reply_object" \
  ceecb05c3400535ccff22509d2fc1426eb9166c2d79a93ae952377a0f4bc5951 \
  kotoba_aiueos_dhcp_reply_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_dhcp_option_object" \
  0b5341000376104c6a23d2e6ef89c05c08fd03e91d2c2aa905643c65604741a0 \
  kotoba_aiueos_dhcp_option_u32
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_x25519_object" \
  5956dc8b7de8b71fdf9b8735bc50f2b3e50eb0f975184d8eb37c9e8e58c60c4c \
  kotoba_aiueos_x25519
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ecdsa_object" \
  5026b4346bdb02ba689fad3afe67f21e556ca028b03338764140079f0308dc29 \
  kotoba_aiueos_ecdsa_p256_sha256_verify
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_aes128_gcm_object" \
  65492e522fdea32d7c9e83d463c42d26dece7ee3300f1a58ba74dd352687b265 \
  kotoba_aiueos_aes128_gcm
# Qwen3.8-27B GGUF admission (ADR-0137). Verified unconditionally like every
# object above, even in profiles that do not link them: the digests are what
# ties the committed artifacts to the sources the oracles graded.
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_header_object" \
  ede85ad44b12e624d2fe9d9c37102270ff7a68e5dfbb1b4e8a5b77a06e232ec6 \
  kotoba_aiueos_qwen35_gguf_header_valid
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_kv_object" \
  c0b7e0a6fca9672371b26249dc197375a734b30f1d0e18b36fd8c9b2d2280e2e \
  kotoba_aiueos_qwen35_gguf_kv_scan
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_tensor_object" \
  8509d721348c060e73576bbdb4380301cc10f1eb9c1f9e1595e7e342e7ea1454 \
  kotoba_aiueos_qwen35_tensor_table_bind
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_tls13_record_object" \
  5c3dee8193c63f3ac1c32b791c4ccdf1fd9d6cda2e3ead08f45c1068e9d36334 \
  kotoba_aiueos_tls13_record
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_cid_v1_admit_object" \
  3aeb23085b6fe6071b5afb1772d712b18db65c0f066ac6a801c6feaeae889808 \
  kotoba_aiueos_cid_v1_admit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_value_runtime_cas_verify_object" \
  dfef74bb28a510d6ca1afc671171ffb8370560f1b98e46d85f063110a46ebc70 \
  kotoba_aiueos_value_runtime_cas_verify
if [ -n "$model_handoff_link" ] || [ -n "$qwen35_parity_cflags" ]; then
  python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_dot_object" \
    4effd1be80404bd910f0df31dd6510391671b7af025a61d9974ffdecf05f1340 kotoba_aiueos_qwen35_dot_f32
  python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_dequant_object" \
    9eb567d148f09b5c26885846e44092ecc686da0c99c9e21e3d9cf8535132a4bc kotoba_aiueos_qwen35_dequant_row
  python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_matvec_object" \
    ec0e3352538e670db1b8165c51e14eff0250908a247a2b0f901484ea5571fa44 kotoba_aiueos_qwen35_matvec
  # FOUR parity profiles, each linking only the objects its stages call. The
  # low region (`aiueos_low_end <= 0x1f4000`) cannot hold every object at once
  # since the tokenizer objects landed -- measured, not assumed, and measured
  # again for the third tranche: attention (17,872 B) and recurrent-step
  # (6,808 B) together overflow it, and so does either one alone until the
  # parity build gained `--gc-sections` (ADR-0175).
  if [ "${AIUEOS_QWEN35_KOTOBA_PARITY:-0}" = 2 ]; then
    python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_activation_object" \
      49556872a8110c3cf95eea7648df693e8352f0d65bf1755404c4f3d92e114b68 kotoba_aiueos_qwen35_activation
    python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_norm_object" \
      faff9aefe51909376dc6a2bfe30fa720c88cc47db06a70df32fa7852b110eaee kotoba_aiueos_qwen35_norm
    qwen35_kotoba_link="$kotoba_qwen35_activation_object $kotoba_qwen35_norm_object"
  elif [ "${AIUEOS_QWEN35_KOTOBA_PARITY:-0}" = 3 ]; then
    python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_attention_object" \
      5cd0baa60ed33c2158842b2ef7c24e6db954d04897bade1d4f663c96765b3049 \
      kotoba_aiueos_qwen35_attention
    qwen35_kotoba_link="$kotoba_qwen35_attention_object"
  elif [ "${AIUEOS_QWEN35_KOTOBA_PARITY:-0}" = 4 ]; then
    python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_qwen35_recurrent_object" \
      23308afec306c50aeaa12796be73e782262cad829f2907ccc2a2b6fc5692f707 \
      kotoba_aiueos_qwen35_recurrent_step
    qwen35_kotoba_link="$kotoba_qwen35_recurrent_object"
  else
    qwen35_kotoba_link="$kotoba_qwen35_dot_object $kotoba_qwen35_dequant_object $kotoba_qwen35_matvec_object"
  fi
fi
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_device_worker_canonical_object" \
  c64371c50104c8acd2b73224eed1377e160cd36ab70e27cae11d39b76e440b0a \
  kotoba_aiueos_device_worker_canonical
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_sha256_stream_object" \
  fdf5b3ac388982cdcc851817a7414f638a821420f70650df4dc8985491281abc \
  kotoba_aiueos_sha256_stream
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_sha256_region_object" \
  5306967ed079f62c37e906095e75a5d0d3cf1e7b0df741a4219d40515e851f0e \
  kotoba_aiueos_sha256_region
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_device_worker_digest_object" \
  496d285c78f906e2de996fc392212fc6ed01c770fa7d05c813db07833f830ca2 \
  kotoba_aiueos_device_worker_digest
if [ -n "$ecdsa_sign_link" ]; then
  python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ecdsa_sign_object" \
    decd83b1cd331af1305ad24c080443118f925067353c320078e66085695fd433 \
    kotoba_aiueos_ecdsa_p256_sign
fi
if [ -n "$ecdsa_public_link" ]; then
  python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ecdsa_public_object" \
    e524dbbfb56f58e5bfac41c0a5b62d355b67c8e9790678984c3ca01a0a66c993 \
    kotoba_aiueos_ecdsa_p256_public
fi
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_ime_object" \
  ee11f50c9dfb30d03c820bead466b2f1bf18e4e64f3a2bfda98f5a5dd5d4ca34 \
  kotoba_aiueos_ime_commit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_wm_object" \
  70fac07783c5b2841b76d9d599c03a97a44d290a8ad977f48aa5af39b21efc7f \
  kotoba_aiueos_wm_hit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_scanout_object" \
  5ca924dff9fe42620f2313d16f8f62018d9bfbf588c7b142383066cce65d8305 \
  kotoba_aiueos_scanout_bind
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_broker_object" \
  713bdc8e4c3d41ea9602fe9184741c15bd7a7b71d6f1b688a766a2d83714a48f \
  kotoba_aiueos_broker_admit
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$kotoba_session_object" \
  1983e5fa9026b2a356a4c43f6fa84630f6261ebd50c9939c4d657a326659048b \
  kotoba_aiueos_session_restore
python3 "$aiueos/scripts/verify-kotoba-user-elf.py" "$kotoba_user_elf" \
  1f0e5897831d0de6bbcb15eec82a6e0c4b402b436689cec051bc6de3b5c4e905
if [ -n "${AIUEOS_EXTERNAL_KERNEL_ELF:-}" ]; then
  python3 "$aiueos/scripts/verify-kotoba-native-kernel.py" \
    "$AIUEOS_EXTERNAL_KERNEL_ELF" "$aiueos/native/kernel.kotoba" \
    6d14f1ab0dc8c296a25a3b41a774423a829239eb "$out/native-kernel-receipt.json"
  cp "$AIUEOS_EXTERNAL_KERNEL_ELF" "$kernel"
else
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone -I "$out" \
  $input_smoke_cflags $model_handoff_cflags $physical_qualification_cflags \
  $qwen35_smp_cflags $qwen35_parity_cflags \
  $physical_network_qualification_cflags $physical_direct_https_qualification_cflags \
  $murakumo_device_result_cflags $persistent_boot_cflags \
  $physical_relay_qualification_cflags \
  $physical_job_qualification_cflags \
  $kernel_hang_test_cflags \
  -c -o "$kernel_object" "$aiueos/kernel/main.c"
zig cc -target x86_64-freestanding-none \
  -c -o "$kernel_entry_object" "$aiueos/kernel/entry.S"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone -I "$out" \
  $physical_qualification_cflags $model_handoff_cflags \
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
  -ffreestanding -fno-stack-protector -mno-red-zone -I "$out" \
  $input_smoke_cflags $physical_network_qualification_cflags \
  $physical_direct_https_qualification_cflags \
  $murakumo_device_result_cflags \
  $physical_relay_qualification_cflags \
  $physical_job_qualification_cflags \
  -c -o "$kernel_pci_object" "$aiueos/kernel/pci.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_rtl8125_object" "$aiueos/kernel/rtl8125.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_relay_protocol_object" "$aiueos/kernel/relay_protocol.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_micro_infer_object" "$aiueos/kernel/micro_infer.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_inference_status_object" "$aiueos/kernel/inference_status.c"
if [ "${AIUEOS_MURAKUMO_DEVICE_RESULT:-0}" = 1 ]; then
  zig cc -target x86_64-freestanding-none -std=c11 -O2 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    -c -o "$kernel_device_result_object" "$aiueos/kernel/device_result.c"
  zig cc -target x86_64-freestanding-none -std=c11 -O2 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    -c -o "$kernel_device_worker_protocol_object" \
    "$aiueos/kernel/device_worker_protocol.c"
fi
if [ -z "$model_handoff_link" ] && [ -n "$qwen35_parity_cflags" ]; then
  # The parity build compiles the reference C without the model handoff, so
  # the loader never asks for a model and the comparison still runs against
  # the same qwen35_quant.c / qwen35_infer.c the handoff compiles.
  #
  # `-ffunction-sections -fdata-sections` + `--gc-sections` is what makes a
  # parity image FIT. MEASURED 2026-09-03 at aiueos main f11967e: without it
  # every profile is over `aiueos_low_end <= 0x1f4000` -- profile 1 by 45,056
  # bytes, profile 2 by 40,960, profile 3 by 40,960, profile 4 by 28,672 --
  # and ld.lld produces nothing. That is not a cost this tranche introduced:
  # profiles 1 and 2 landed working and the tree grew past them. The
  # comparison is unaffected because what the collector removes is the model
  # path (`aiueos_qwen35_generate`, the GGUF parser, the IQ codebook grids),
  # which no self-test stage calls, and it removes it from the REFERENCE half
  # -- a stage whose reference had been collected would not agree with the
  # object, it would fail to link.
  qwen35_parity_section_cflags="-ffunction-sections -fdata-sections"
  qualification_gc_link="--gc-sections"
  zig cc -target x86_64-freestanding-none -std=c11 -O2 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $qwen35_parity_section_cflags \
    -c -o "$kernel_qwen35_runtime_object" "$aiueos/kernel/qwen35_runtime.c"
  zig cc -target x86_64-freestanding-none -std=c11 -O3 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $qwen35_parity_section_cflags \
    -c -o "$kernel_qwen35_quant_object" "$aiueos/kernel/qwen35_quant.c"
  zig cc -target x86_64-freestanding-none -std=c11 -O3 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $qwen35_parity_section_cflags \
    $qwen35_parity_cflags \
    -c -o "$kernel_qwen35_infer_object" "$aiueos/kernel/qwen35_infer.c"
  qwen35_parity_link="$kernel_qwen35_runtime_object $kernel_qwen35_quant_object $kernel_qwen35_infer_object"
fi
if [ -n "$model_handoff_link" ]; then
  zig cc -target x86_64-freestanding-none -std=c11 -O2 \
    -ffreestanding -fno-stack-protector -mno-red-zone -I "$out" \
    $model_handoff_cflags \
    -c -o "$kernel_model_handoff_object" "$aiueos/kernel/model_handoff.c"
  zig cc -target x86_64-freestanding-none -std=c11 -O2 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $model_handoff_cflags -DAIUEOS_QWEN35_KOTOBA_ADMISSION \
    -c -o "$kernel_qwen35_runtime_object" "$aiueos/kernel/qwen35_runtime.c"
  zig cc -target x86_64-freestanding-none -std=c11 -O3 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $model_handoff_cflags \
    -c -o "$kernel_qwen35_quant_object" "$aiueos/kernel/qwen35_quant.c"
  zig cc -target x86_64-freestanding-none -std=c11 -O3 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $qwen35_smp_cflags \
    $qwen35_vector_cflags \
    $model_handoff_cflags \
    -c -o "$kernel_qwen35_infer_object" "$aiueos/kernel/qwen35_infer.c"
fi
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  $model_handoff_cflags \
  -c -o "$kernel_kototama_runtime_object" "$aiueos/kernel/kototama_runtime.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_job_protocol_object" "$aiueos/kernel/job_protocol.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_tls_aes_object" "$aiueos/kernel/tls_aes_gcm.c"
zig cc -target x86_64-freestanding-none -std=c11 -O2 \
  -ffreestanding -fno-stack-protector -mno-red-zone \
  -c -o "$kernel_tls13_object" "$aiueos/kernel/tls13.c"
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
  -ffreestanding -fno-stack-protector -mno-red-zone -I "$out" \
  $physical_qualification_cflags \
  -c -o "$kernel_framebuffer_object" "$aiueos/kernel/framebuffer.c"
if [ -n "$qualification_link" ]; then
  zig cc -target x86_64-freestanding-none \
    -c -o "$kernel_qualification_entry_object" \
    "$aiueos/kernel/qualification_entry.S"
  zig cc -target x86_64-freestanding-none -std=c11 -O2 \
    -ffreestanding -fno-stack-protector -mno-red-zone \
    $persistent_boot_cflags \
    -c -o "$kernel_qualification_object" "$aiueos/kernel/qualification.c"
fi
# --strip-all: the symbol/string tables are ~550 KiB and are never loaded (only
# PT_LOAD segments are), but the loader shas and reads the WHOLE KERNEL.ELF file
# into a 1 MiB buffer, so they count against the ceiling. Stripping them keeps
# the file at its loadable size (~516 KiB). Relocations are resolved at link
# time; a freestanding static kernel needs no symbol table at runtime. The sha
# is recomputed below, so nothing external pins the pre-strip file. (ADR-0107:
# net_ssh_kex pushed the unstripped file over 1 MiB while the loadable size was
# well under it.)
# The physical qualification branches halt before the generic virtio/SMP
# continuation.  Dropping those unreachable whole sections there keeps
# optional model runtimes inside the existing low-2MiB W^X bootstrap boundary.
# The ordinary OS link deliberately keeps its historical section set.
# AIUEOS_K16_LINK_LIST_OUT=<file> writes the EXACT object list this link is
# about to receive, one path per line, and changes nothing else. It exists so
# that the pure-native gate (ADR-0131) can be run against today's production
# link rather than against a list somebody retyped -- the variable names are
# read back out of this very invocation, so a list added here cannot fall out
# of step with the one measured.
if [ -n "${AIUEOS_K16_LINK_LIST_OUT:-}" ]; then
  : > "$AIUEOS_K16_LINK_LIST_OUT"
  for link_var in $(sed -n '/^zig ld\.lld /,/^fi$/p' "$0" \
                      | grep -o '\$[A-Za-z_][A-Za-z0-9_]*' | sed 's/^\$//' \
                      | grep -vxE 'aiueos|kernel|qualification_gc_link'); do
    eval "set -- \$$link_var"
    for link_path in "$@"; do
      printf '%s\n' "$link_path" >> "$AIUEOS_K16_LINK_LIST_OUT"
    done
  done
  echo "AIUEOS_K16_LINK_LIST_WRITTEN entries=$(wc -l < "$AIUEOS_K16_LINK_LIST_OUT" | tr -d ' ') path=$AIUEOS_K16_LINK_LIST_OUT"
fi
zig ld.lld -nostdlib -static --strip-all $qualification_gc_link -z max-page-size=0x1000 \
  -T "$aiueos/kernel/linker.ld" -o "$kernel" \
  "$kernel_entry_object" "$kernel_object" "$kernel_paging_object" \
  "$kernel_acpi_object" "$kernel_vtd_object" "$kernel_apic_object" "$kernel_memory_object" \
  "$kernel_pci_object" "$kernel_rtl8125_object" "$kernel_relay_protocol_object" \
  "$kernel_micro_infer_object" "$kernel_inference_status_object" \
  $device_result_link $model_handoff_link "$kernel_kototama_runtime_object" \
  "$kernel_job_protocol_object" \
  "$kernel_tls_aes_object" "$kernel_tls13_object" \
  "$kernel_scheduler_object" "$kernel_syscall_object" \
  "$kernel_process_object" "$kernel_loader_object" \
  "$kernel_smp_object" "$kernel_trampoline_object" \
  "$kernel_ioapic_object" "$kernel_framebuffer_object" $qualification_link \
  "$kotoba_kernel_object" \
  "$kotoba_journal_object" "$kotoba_fnv_object" "$kotoba_journal_valid_object" \
  "$kotoba_transaction_valid_object" "$kotoba_transaction_route_object" \
  "$kotoba_mutable_valid_object" \
  "$kotoba_superblock_valid_object" "$kotoba_journal_build_object" \
  "$kotoba_mutable_build_object" "$kotoba_cap_valid_object" \
  "$kotoba_extent_valid_object" "$kotoba_region_valid_object" \
  "$kotoba_pci_config_read_object" "$kotoba_pci_config_write_object" \
  "$kotoba_rtl8125_identify_object" "$kotoba_rtl8125_link_up_object" \
  "$kotoba_rtl8125_ring_build_object" "$kotoba_rtl8125_program_object" \
  "$kotoba_rtl8125_tx_submit_object" "$kotoba_rtl8125_rx_poll_object" \
  "$kotoba_x25519_object"   "$kotoba_ecdsa_object" $ecdsa_sign_link $ecdsa_public_link "$kotoba_ime_object" \
  "$kotoba_aes128_gcm_object" "$kotoba_tls13_record_object" \
  $qwen35_kotoba_link $qwen35_parity_link \
  "$kotoba_device_worker_canonical_object" \
  "$kotoba_sha256_stream_object" "$kotoba_sha256_region_object" \
  "$kotoba_device_worker_digest_object" \
  "$kotoba_wm_object" "$kotoba_scanout_object" "$kotoba_broker_object" "$kotoba_session_object" \
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
  "$kotoba_tcp_segment_object" \
  "$kotoba_dhcp_reply_object" \
  "$kotoba_dhcp_option_object" \
  "$kotoba_qwen35_vocab_index_object" \
  "$kotoba_qwen35_tokenize_object" \
  "$kotoba_qwen35_detokenize_object"
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
if [ "${AIUEOS_EMBEDDED_RELEASE:-0}" = 1 ]; then
  python3 - "$kernel" "$initramfs" "$embedded_source" <<'PYEMBED'
from pathlib import Path
import hashlib
import sys

kernel_path = Path(sys.argv[1]).resolve()
initramfs_path = Path(sys.argv[2]).resolve()
kernel = kernel_path.as_posix()
initramfs = initramfs_path.as_posix()
kernel_sha256 = hashlib.sha256(kernel_path.read_bytes()).hexdigest()
initramfs_sha256 = hashlib.sha256(initramfs_path.read_bytes()).hexdigest()
Path(sys.argv[3]).write_text(
    f'# aiueos-embedded-kernel-sha256={kernel_sha256}\n'
    f'# aiueos-embedded-initramfs-sha256={initramfs_sha256}\n'
    '.section .rdata,"dr"\n'
    '.balign 16\n'
    '.globl aiueos_embedded_kernel_start\n'
    'aiueos_embedded_kernel_start:\n'
    f'.incbin "{kernel}"\n'
    '.globl aiueos_embedded_kernel_end\n'
    'aiueos_embedded_kernel_end:\n'
    '.balign 16\n'
    '.globl aiueos_embedded_initramfs_start\n'
    'aiueos_embedded_initramfs_start:\n'
    f'.incbin "{initramfs}"\n'
    '.globl aiueos_embedded_initramfs_end\n'
    'aiueos_embedded_initramfs_end:\n',
    encoding='ascii')
PYEMBED
  zig cc -target x86_64-windows-gnu -c \
    -o "$embedded_object" "$embedded_source"
fi
if [ -n "$model_slots_link" ]; then
  zig cc -target x86_64-windows-gnu -std=c11 -O2 \
    -ffreestanding -fno-builtin -fshort-wchar -fno-stack-protector -mno-red-zone \
    -I "$aiueos/uefi" -c -o "$model_slots_object" \
    "$aiueos/uefi/model_slots.c"
fi
zig cc -target x86_64-windows-gnu -std=c11 -O2 \
  -ffreestanding -fshort-wchar -fno-stack-protector -mno-red-zone \
  -I "$out" -I "$aiueos/uefi" \
  $gop_discovery_cflags $physical_qualification_cflags $loader_failure_test_cflags \
  $loader_hang_test_cflags $qualification_watchdog_cflags \
  $embedded_release_cflags $netboot_qualification_cflags \
  $persistent_boot_cflags $model_handoff_cflags $model_slots_cflags \
  -c -o "$object" "$aiueos/uefi/main.c"
zig lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /timestamp:0 \
  /fixed:no "/out:$efi" "$object" "$identity_object" \
  $embedded_release_link $model_slots_link

if [ "${AIUEOS_EMBEDDED_RELEASE:-0}" = 1 ]; then
  # `.incbin` inputs are not compiler source files.  Keep both a content salt
  # in embedded-release.S (above) and this post-link admission so a stale Zig
  # object can never be published with fresh expected digests.
  python3 - "$efi" "$kernel" "$initramfs" <<'PYVERIFYEMBED'
from pathlib import Path
import sys

image = Path(sys.argv[1]).read_bytes()
for label, source in (("kernel", sys.argv[2]), ("initramfs", sys.argv[3])):
    payload = Path(source).read_bytes()
    first = image.find(payload)
    if first < 0 or image.find(payload, first + 1) >= 0:
        raise SystemExit(
            f"error: linked EFI must contain exactly one current {label} payload")
print("AIUEOS_EMBEDDED_POSTLINK_OK kernel+initramfs exact-current-bytes")
PYVERIFYEMBED
fi

magic=$(dd if="$efi" bs=1 count=2 2>/dev/null)
[ "$magic" = MZ ] || {
  echo "error: $efi is not a PE/COFF image" >&2
  exit 1
}
[ "$(dd if="$kernel" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] || {
  echo "error: $kernel is not an ELF image" >&2
  exit 1
}

# The freshness receipt (ADR-0155). Written LAST, from the artifacts that were
# just produced and the tree they were produced from, so that a boot harness
# can refuse an image it did not just build. Nothing above this line is allowed
# to have failed: `set -e` is on, and the receipt is therefore evidence that the
# whole build ran, not merely that a file called BOOTX64.EFI exists.
#
# `nbb` absent is a REFUSAL, not a skip. A build that quietly shipped no
# receipt would put every downstream `assert` into could-not-run, which is
# exactly the state this file exists to make impossible.
command -v nbb >/dev/null 2>&1 || {
  echo "error: nbb is required to write the image freshness receipt" >&2
  exit 3
}
nbb "$aiueos/scripts/image-freshness.cljs" record \
  --out "$out/image-receipt.edn" --root "$repo" \
  "$efi" "$kernel" "$initramfs" >/dev/null

echo "$efi"
