#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${AIUEOS_OUT:-"$repo/build/aiueos-micro-infer-model"}
mkdir -p "$out"

cc -std=c11 -O2 -Wall -Wextra -Werror \
  -o "$out/micro-infer-model" \
  "$repo/os/aiueos/tests/micro_infer_model.c" \
  "$repo/os/aiueos/kernel/micro_infer.c"
"$out/micro-infer-model"
