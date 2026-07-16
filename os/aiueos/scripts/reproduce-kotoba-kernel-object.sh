#!/bin/sh
set -eu

aiueos=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:?usage: reproduce-kotoba-kernel-object.sh /path/to/compiler}
expected=95b7b1ca4a88c436d8f65154c509b61e8d3256d6
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
transaction_route_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-transaction-route.$$
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
service_registry_state_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-service-registry-state.$$
user_object_journal_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-user-object-journal.$$
user_object_journal_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-user-object-journal-valid.$$
user_object_journal_value_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-user-object-journal-value.$$
sha256_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-sha256.$$
digest_equal_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-digest-equal.$$
catalog_valid_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-app-catalog-valid.$$
app_lookup_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-app-lookup-plan.$$
rsa2048_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-rsa2048.$$
user_elf_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-user-smoke.$$
trap 'rm -f "$tmp" "$journal_tmp" "$fnv_tmp" "$journal_valid_tmp" "$transaction_valid_tmp" "$transaction_route_tmp" "$mutable_valid_tmp" "$superblock_valid_tmp" "$journal_build_tmp" "$mutable_build_tmp" "$cap_valid_tmp" "$extent_valid_tmp" "$region_valid_tmp" "$syscall_range_tmp" "$copy_in_tmp" "$capability_tmp" "$service_lifecycle_tmp" "$service_registry_tmp" "$service_registry_state_tmp" "$user_object_journal_tmp" "$user_object_journal_valid_tmp" "$user_object_journal_value_tmp" "$sha256_tmp" "$digest_equal_tmp" "$catalog_valid_tmp" "$app_lookup_tmp" "$rsa2048_tmp" "$user_elf_tmp"' EXIT HUP INT TERM
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
  c924ac51de16c3120a6fd227eb49a14ab1874e4e365e1bdf3a1bfe7fca7672f3 \
  kotoba_aiueos_fnv1a
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/journal-record-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$journal_valid_tmp"
cmp "$aiueos/kotoba/journal-record-valid.o" "$journal_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$journal_valid_tmp" \
  7c3a5b581c99daa5282d963efb9162dc0a2af25185523ce031270204e213e3f0 \
  kotoba_aiueos_journal_record_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/object-transaction-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$transaction_valid_tmp"
cmp "$aiueos/kotoba/object-transaction-valid.o" "$transaction_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$transaction_valid_tmp" \
  fb8d3cd1b1b9c13cfd3e6f80cac15f568d13327cac76b47b54ca60ad3fd09d86 \
  kotoba_aiueos_object_transaction_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/object-transaction-route.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$transaction_route_tmp"
cmp "$aiueos/kotoba/object-transaction-route.o" "$transaction_route_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$transaction_route_tmp" \
  ab98299f535a2d0752135032b960d7830cca8aee4cdfff8a2f4952d897cfe3dd \
  kotoba_aiueos_object_transaction_route
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/mutable-object-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$mutable_valid_tmp"
cmp "$aiueos/kotoba/mutable-object-valid.o" "$mutable_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$mutable_valid_tmp" \
  53513e67ae900ce2de971aea92ccecc976d361beeaedc8a633b14ef1f873fc73 \
  kotoba_aiueos_mutable_object_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/superblock-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$superblock_valid_tmp"
cmp "$aiueos/kotoba/superblock-valid.o" "$superblock_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$superblock_valid_tmp" \
  c7181af2d2ff2713b1e7e5979d2fb0b4bc989ace280858d7afd478c3739a980e \
  kotoba_aiueos_superblock_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/journal-record-build.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$journal_build_tmp"
cmp "$aiueos/kotoba/journal-record-build.o" "$journal_build_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$journal_build_tmp" \
  9691f9dda0899b70fa2853f07da2a974cefd957bdb7c6fee3235301f6a3143dc \
  kotoba_aiueos_journal_record_build
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/mutable-object-build.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$mutable_build_tmp"
cmp "$aiueos/kotoba/mutable-object-build.o" "$mutable_build_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$mutable_build_tmp" \
  d593a4db905cac264c67732983bf0b62de783011b46e505257a51d94d820eafd \
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
  ab367b1ac46461f6228080ee909415b8825a429b47abb1edc8dbafc7083bba7c \
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
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/service-registry-state.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$service_registry_state_tmp"
cmp "$aiueos/kotoba/service-registry-state.o" "$service_registry_state_tmp"
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/user-object-journal-build.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$user_object_journal_tmp"
cmp "$aiueos/kotoba/user-object-journal-build.o" "$user_object_journal_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$user_object_journal_tmp" \
  994d8a296d17afa67a8c9267cafa6079edca5068aeed46e78d8f455a40df1cfd \
  kotoba_aiueos_user_object_journal_build
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/user-object-journal-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$user_object_journal_valid_tmp"
cmp "$aiueos/kotoba/user-object-journal-valid.o" "$user_object_journal_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$user_object_journal_valid_tmp" \
  0f2015e53ed083741687abfbaff72edf8a525947b9fc753cacc7a1bf10faf46f \
  kotoba_aiueos_user_object_journal_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/user-object-journal-value.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$user_object_journal_value_tmp"
cmp "$aiueos/kotoba/user-object-journal-value.o" "$user_object_journal_value_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$user_object_journal_value_tmp" \
  bd1de2777d75e02968939d2b7bc74e84dc16a8a9431fe36bd2c2170d6866fad3 \
  kotoba_aiueos_user_object_journal_value
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/sha256.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$sha256_tmp"
cmp "$aiueos/kotoba/sha256.o" "$sha256_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$sha256_tmp" \
  ad28e7d83d6e582df2dacf802e915fc9532fc99e141e174e7bf8642191db2c29 \
  kotoba_aiueos_sha256
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/digest-equal.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$digest_equal_tmp"
cmp "$aiueos/kotoba/digest-equal.o" "$digest_equal_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$digest_equal_tmp" \
  6d005bf596ff10343377d9c243d473437fa272559b7f9130cba47cc4cd80d3aa \
  kotoba_aiueos_digest_equal
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/app-catalog-valid.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$catalog_valid_tmp"
cmp "$aiueos/kotoba/app-catalog-valid.o" "$catalog_valid_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$catalog_valid_tmp" \
  bf990c3775bd1351627daa669a124adad8e194710dc41d93f0c1b2ccfdacd927 \
  kotoba_aiueos_app_catalog_valid
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/app-lookup-plan.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$app_lookup_tmp"
cmp "$aiueos/kotoba/app-lookup-plan.o" "$app_lookup_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$app_lookup_tmp" \
  aa8ecea382820707638aa24e49226dbab243c95dc2a28ebfe3fac3a4dffe1a6c \
  kotoba_aiueos_app_lookup_plan
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/rsa2048.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$rsa2048_tmp"
cmp "$aiueos/kotoba/rsa2048.o" "$rsa2048_tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$rsa2048_tmp" \
  97a6c6b1f4c3f3569bf8d40423db924d291aa0b6f10cd7bace79f54e193387a6 \
  kotoba_aiueos_rsa2048_sha256_verify
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/user-smoke.kotoba" \
  --target x86_64-aiueos-user-v1 --policy "$aiueos/kotoba/user-runtime-policy.edn" \
  --output "$user_elf_tmp"
cmp "$aiueos/kotoba/user-smoke.elf" "$user_elf_tmp"
python3 "$aiueos/scripts/verify-kotoba-user-elf.py" "$user_elf_tmp" \
  1f0e5897831d0de6bbcb15eec82a6e0c4b402b436689cec051bc6de3b5c4e905
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$service_registry_tmp" \
  b0e9c90aaef5477fb5ababd6dd3067dd95a7eba93f3bb262cc49bace7e5a44ce \
  kotoba_aiueos_service_registry_build
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$service_registry_state_tmp" \
  d73f13de0d86a4af46e33516b8b0f6358b5d477307c61d40624b971f34c15f3e \
  kotoba_aiueos_service_registry_state
echo "AIUEOS_KOTOBA_REPRODUCIBLE_OK compiler=$actual"
