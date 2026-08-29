#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
work=$(mktemp -d "${TMPDIR:-/tmp}/aiueos-qwen35-runtime.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

python3 "$aiueos/tests/make_qwen35_header_fixture.py" "$work/header.gguf"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
  "$aiueos/kernel/qwen35_runtime.c" \
  "$aiueos/tests/qwen35_runtime_model.c" \
  -o "$work/qwen35-runtime-model"
"$work/qwen35-runtime-model" "$work/header.gguf"
