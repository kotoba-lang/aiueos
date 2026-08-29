#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${AIUEOS_OUT:-"$repo/build/aiueos-relay-protocol-model"}
mkdir -p "$out"

cc -std=c11 -O2 -Wall -Wextra -Werror \
  -o "$out/relay-protocol-model" \
  "$repo/os/aiueos/tests/relay_protocol_model.c" \
  "$repo/os/aiueos/kernel/relay_protocol.c"
"$out/relay-protocol-model"
