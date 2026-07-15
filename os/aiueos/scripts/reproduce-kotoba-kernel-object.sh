#!/bin/sh
set -eu

aiueos=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:?usage: reproduce-kotoba-kernel-object.sh /path/to/compiler}
expected=01f3ce2fe21ea002b38f346b4139604eb0f747e5
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
trap 'rm -f "$tmp" "$journal_tmp" "$fnv_tmp" "$journal_valid_tmp" "$transaction_valid_tmp" "$mutable_valid_tmp" "$superblock_valid_tmp"' EXIT HUP INT TERM
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
  3daa4b0b43f58bd9b42005cf3bc41c35a24b35c82a466313cb954f854e75429e \
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
echo "AIUEOS_KOTOBA_REPRODUCIBLE_OK compiler=$actual"
