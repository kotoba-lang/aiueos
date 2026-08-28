#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos-dbc-probe"}
esp="$out/esp/EFI/BOOT"
object="$out/dbc-probe.obj"
efi="$esp/BOOTX64.EFI"

command -v zig >/dev/null 2>&1 || { echo "error: Zig is required" >&2; exit 1; }
mkdir -p "$esp"
zig cc -target x86_64-windows-gnu -std=c11 -O2 -ffreestanding -fshort-wchar \
  -fno-stack-protector -mno-red-zone -Wall -Wextra -Werror \
  -c -o "$object" "$aiueos/dbc-probe/main.c"
zig lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /timestamp:0 \
  /fixed:no "/out:$efi" "$object"
printf '%s\n' "$efi"
