#!/bin/sh
set -eu

aiueos=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler=${1:?usage: reproduce-kotoba-kernel-object.sh /path/to/compiler}
expected=b3bbec460dd344a69aa000c2760c138d7afbb8a6
actual=$(git -C "$compiler" rev-parse HEAD)

[ "$actual" = "$expected" ] || {
  echo "error: compiler HEAD is $actual; expected $expected" >&2
  exit 1
}

tmp=${TMPDIR:-/tmp}/aiueos-kotoba-kernel-probe.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM
"$compiler/bin/kotoba-compiler" compile "$aiueos/kotoba/kernel-probe.kotoba" \
  --target x86_64-aiueos-kernel-v1 --output "$tmp"
cmp "$aiueos/kotoba/kernel-probe.o" "$tmp"
python3 "$aiueos/scripts/verify-kotoba-kernel-object.py" "$tmp" \
  10d91712fccd887e68f9caa25413c8fa2c783968e72b1bead4025c6a294ffa42
echo "AIUEOS_KOTOBA_REPRODUCIBLE_OK compiler=$actual"
