#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${AIUEOS_OUT:-"$repo/build/aiueos-job-protocol-model"}
mkdir -p "$out"

cc -std=c11 -O2 -Wall -Wextra -Werror \
  -o "$out/job-protocol-model" \
  "$repo/os/aiueos/tests/job_protocol_model.c" \
  "$repo/os/aiueos/kernel/job_protocol.c" \
  "$repo/os/aiueos/kernel/micro_infer.c"
"$out/job-protocol-model"
