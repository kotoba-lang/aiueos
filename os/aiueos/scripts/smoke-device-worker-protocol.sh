#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aiueos-device-worker-protocol.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-zig cc} -std=c11 -O2 \
  -o "$work/device-worker-protocol" \
  "$repo/os/aiueos/kernel/device_worker_protocol.c" \
  "$repo/os/aiueos/scripts/device_worker_protocol_model.c"
"$work/device-worker-protocol"

printf '%s\n' \
  "AIUEOS_DEVICE_WORKER_PROTOCOL_OK heartbeat=ready poll=bounded job-id=u64 chunked=accepted"
