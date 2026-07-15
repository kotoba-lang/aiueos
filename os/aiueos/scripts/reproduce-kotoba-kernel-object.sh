#!/bin/sh
set -eu

aiueos=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:?usage: reproduce-kotoba-kernel-object.sh /path/to/compiler}
expected=eea427f730e3ef390c7e37259d8953686ce0144a
actual=$(git -C "$compiler" rev-parse HEAD)

[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2
  exit 1
}

tmp=${TMPDIR:-/tmp}/aiueos-kotoba-kernel-probe.$$
journal_tmp=${TMPDIR:-/tmp}/aiueos-kotoba-journal-plan.$$
trap 'rm -f "$tmp" "$journal_tmp"' EXIT HUP INT TERM
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
echo "AIUEOS_KOTOBA_REPRODUCIBLE_OK compiler=$actual"
