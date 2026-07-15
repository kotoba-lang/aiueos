#!/bin/sh
set -eu

aiueos=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:?usage: reproduce-kotoba-kernel-object.sh /path/to/compiler}
expected=2aae14084f41a819d6893c2447317435ddd248da
actual=$(git -C "$compiler" rev-parse HEAD)

[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2
  exit 1
}

tmp=${TMPDIR:-/tmp}/aiueos-kotoba-kernel-probe.$$
journal_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-journal-plan.$$
fnv_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-fnv1a.$$
journal_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-journal-valid.$$
transaction_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-transaction-valid.$$
mutable_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-mutable-valid.$$
superblock_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-superblock-valid.$$
journal_build_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-journal-build.$$
mutable_build_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-mutable-build.$$
cap_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-cap-valid.$$
extent_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-extent-valid.$$
region_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-region-valid.$$
syscall_range_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-syscall-range.$$
copy_in_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-copy-in.$$
capability_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-capability.$$
service_lifecycle_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-service-lifecycle.$$
service_registry_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-service-registry.$$
user_elf_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-user-smoke.$$
trap 'rm -f "$tmp" "$journal_tmp" "$fnv_tmp" "$journal_valid_tmp" "$transaction_valid_tmp" "$mutable_valid_tmp" "$superblock_valid_tmp" "$journal_build_tmp" "$mutable_build_tmp" "$cap_valid_tmp" "$extent_valid_tmp" "$region_valid_tmp" "$syscall_range_tmp" "$copy_in_tmp" "$capability_tmp" "$service_lifecycle_tmp" "$service_registry_tmp" "$user_elf_tmp"' EXIT HUP INT TERM
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/kernel-probe.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$tmp"
cmp "$aiueos/kotoba/kernel-probe.o" "$tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$tmp" \
  10d91712fccd887e68f9caa25413c8fa2c783968e72b1bead4025c6a294ffa42
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/journal-plan.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$journal_tmp"
cmp "$aiueos/kotoba/journal-plan.o" "$journal_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$journal_tmp" \
  c24c7bdab170d65624c1ee2cb939b949c94750b651f59b5aa7d4bc192ec62df6 \
  kotoba_aiueos_journal_plan
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/fnv1a.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$fnv_tmp"
cmp "$aiueos/kotoba/fnv1a.o" "$fnv_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$fnv_tmp" \
  9d447888daf2c5065b3caf98ee348b426296c95781d0651989bd2025ac7ba52d \
  kotoba_aiueos_fnv1a
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/journal-record-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$journal_valid_tmp"
cmp "$aiueos/kotoba/journal-record-valid.o" "$journal_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$journal_valid_tmp" \
  f06982540d9516409888a759659b7dc75a30972960f567535b47a57d97399c95 \
  kotoba_aiueos_journal_record_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/object-transaction-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$transaction_valid_tmp"
cmp "$aiueos/kotoba/object-transaction-valid.o" "$transaction_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$transaction_valid_tmp" \
  1d2bc2c52b48c6743877901fe9bd208cc39a0a20efc7dd7b1997eb3981079a1f \
  kotoba_aiueos_object_transaction_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/mutable-object-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$mutable_valid_tmp"
cmp "$aiueos/kotoba/mutable-object-valid.o" "$mutable_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$mutable_valid_tmp" \
  cbbd06c9d4805d36d79d3fe2d17e0769f077d3a6699693825a45a1d17620ae5d \
  kotoba_aiueos_mutable_object_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/superblock-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$superblock_valid_tmp"
cmp "$aiueos/kotoba/superblock-valid.o" "$superblock_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$superblock_valid_tmp" \
  3e53077d751eadb01195a6a0b375fb8e8680c98a0a28dadae29ebb4426d6aee7 \
  kotoba_aiueos_superblock_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/journal-record-build.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$journal_build_tmp"
cmp "$aiueos/kotoba/journal-record-build.o" "$journal_build_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$journal_build_tmp" \
  1f1dedd438d523c7f92bde90f8bf07c92768fd9dd7cfc73a27f9dc895eb3bca7 \
  kotoba_aiueos_journal_record_build
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/mutable-object-build.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$mutable_build_tmp"
cmp "$aiueos/kotoba/mutable-object-build.o" "$mutable_build_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$mutable_build_tmp" \
  22b54f50d63e5ff0a1563acef324a53adacd824ebc98768ac614fb41ec415f1c \
  kotoba_aiueos_mutable_object_build
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/virtio-cap-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$cap_valid_tmp"
cmp "$aiueos/kotoba/virtio-cap-valid.o" "$cap_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$cap_valid_tmp" \
  f03487d441ca9af4da636bcf6a9c983e23de86eb60ab70fe7533fa558f4262d4 \
  kotoba_aiueos_virtio_cap_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/pci-extent-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$extent_valid_tmp"
cmp "$aiueos/kotoba/pci-extent-valid.o" "$extent_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$extent_valid_tmp" \
  345d52917447ddedd21fcee1e7c1143395132828deade02e29896a3829bafdbb \
  kotoba_aiueos_pci_extent_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/pci-region-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$region_valid_tmp"
cmp "$aiueos/kotoba/pci-region-valid.o" "$region_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$region_valid_tmp" \
  824abbe8509d43eb5276a612bd38e9b472ebba1b4bd71f416671062e4b523123 \
  kotoba_aiueos_pci_region_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/syscall-range-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$syscall_range_tmp"
cmp "$aiueos/kotoba/syscall-range-valid.o" "$syscall_range_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$syscall_range_tmp" \
  c65aa4b0b2b47891f2b1340a289157625262156733d85195d0449a2050aa18b8 \
  kotoba_aiueos_syscall_range_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/copy-in.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$copy_in_tmp"
cmp "$aiueos/kotoba/copy-in.o" "$copy_in_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$copy_in_tmp" \
  f3b8ae90a2d77ca821c82dfd03f0b6ffc080ffe2b78195a334a4265fbec518e4 \
  kotoba_aiueos_copy_in
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/capability-plan.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$capability_tmp"
cmp "$aiueos/kotoba/capability-plan.o" "$capability_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$capability_tmp" \
  006f509119d39298a1a64093f9b49f48f808445d251e96505c4c03e3abc068bb \
  kotoba_aiueos_capability_plan
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/service-lifecycle.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$service_lifecycle_tmp"
cmp "$aiueos/kotoba/service-lifecycle.o" "$service_lifecycle_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$service_lifecycle_tmp" \
  cd6d9c57cd4dd94839ef1a255c6d82b6c1b231c08aa1f7de86ab8c0029720816 \
  kotoba_aiueos_service_lifecycle
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/service-registry-build.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$service_registry_tmp"
cmp "$aiueos/kotoba/service-registry-build.o" "$service_registry_tmp"
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/user-smoke.kotoba" \
  --target x86_64-aiueos-user-v1 --output "$user_elf_tmp"
cmp "$aiueos/kotoba/user-smoke.elf" "$user_elf_tmp"
python3 "$aiueos/scripts/verify-kotoba-user-elf.py" "$user_elf_tmp" \
  a4050d7c7e3feca1e66eeff188240b9bff3c91dbea02ff0aafb5d1b09c63089a
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$service_registry_tmp" \
  70eee5d4dd599ea2049261e92a656931768b355eefc0fb6d83deee192a3a05f0 \
  kotoba_aiueos_service_registry_build
echo "AIUEOS_KOTOBA_REPRODUCIBLE_OK compiler=$actual"
