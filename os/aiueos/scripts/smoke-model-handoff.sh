#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos-model-handoff"}
mkdir -p "$out"
header="$out/aiueos-model-identity.h"
binary="$out/model-handoff-model"

python3 - "$header" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_text(
    "#ifndef AIUEOS_MODEL_IDENTITY_H\n#define AIUEOS_MODEL_IDENTITY_H\n"
    "#include <stdint.h>\n"
    "#define AIUEOS_MODEL_TOTAL_BYTES 10934860704ULL\n"
    "#define AIUEOS_MODEL_PART_COUNT 3U\n"
    "static const uint8_t aiueos_expected_model_sha256[32]={0};\n"
    "#endif\n", encoding="ascii")
PY

cc -std=c11 -Wall -Wextra -Werror -I "$out" \
  "$aiueos/kernel/model_handoff.c" "$aiueos/tests/model_handoff_model.c" \
  -o "$binary"
"$binary"
