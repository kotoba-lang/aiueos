#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
amu=${1:?usage: AIUEOS_AMU_SELFHOST_BIN=/native/amu build-kotoba-native-desktop.sh /path/to/amu-source; explicit bootstrap: AIUEOS_AMU_BOOTSTRAP=1}
amu_bin=${AIUEOS_AMU_SELFHOST_BIN:-}
if [ -n "$amu_bin" ]; then
  [ -x "$amu_bin" ] || {
    echo "error: AIUEOS_AMU_SELFHOST_BIN is not executable: $amu_bin" >&2; exit 1;
  }
  amu_magic=$(LC_ALL=C od -An -tx1 -N4 "$amu_bin" | tr -d '[:space:]')
  case "$amu_magic" in
    cffaedfe|feedfacf|7f454c46) ;;
    *) echo "error: AIUEOS_AMU_SELFHOST_BIN must be a native Mach-O or ELF executable, not a launcher: $amu_bin" >&2; exit 1 ;;
  esac
  amu_binary_sha256=$(shasum -a 256 "$amu_bin" | awk '{print $1}')
  export AIUEOS_AMU_SELFHOST_BINARY_SHA256="$amu_binary_sha256"
  compiler_route=native-selfhost
elif [ "${AIUEOS_AMU_BOOTSTRAP:-0}" = "1" ]; then
  command -v node >/dev/null 2>&1 || [ "$(od -An -tx1 -N4 "$amu/bin/amu" | tr -d ' \n')" = "7f454c46" ] || {
    echo "error: node is required for the explicit NBB bootstrap route" >&2; exit 1;
  }
  unset AIUEOS_AMU_SELFHOST_BINARY_SHA256
  compiler_route=nbb-bootstrap
else
  echo "error: provide AIUEOS_AMU_SELFHOST_BIN, or explicitly opt into AIUEOS_AMU_BOOTSTRAP=1" >&2
  exit 1
fi
export AIUEOS_NATIVE_COMPILER_ROUTE="$compiler_route"
export AIUEOS_NATIVE_PACKAGE_ROUTE=amu-nbb

# bin/amu is a native ELF since the Amu selfhost transition; the node route
# only applies to a JS launcher. Measured 2026-10-25: node refuses the ELF
# with a SyntaxError at byte 1.
amu_is_elf() {
  [ "$(od -An -tx1 -N4 "$amu/bin/amu" | tr -d ' \n')" = "7f454c46" ]
}
run_amu() {
  if amu_is_elf; then
    KEXE_SECCOMP=if-available KEXE_COMMAND=1 "$amu/bin/amu" "$@"
  else
    node "$amu/bin/amu" "$@"
  fi
}
run_compiler() {
  if [ "$compiler_route" = "native-selfhost" ]; then
    "$amu_bin" "$@"
  else
    run_amu "$@"
  fi
}
package_with_amu_nbb() {
  run_amu "$@"
}
command -v node >/dev/null 2>&1 || [ "$(od -An -tx1 -N4 "$amu/bin/amu" | tr -d ' \n')" = "7f454c46" ] || {
  echo "error: node is required for the Amu/NBB packaging steps" >&2; exit 1;
}
git -C "$amu" rev-parse --show-toplevel >/dev/null || {
  echo "error: Amu source tree not found: $amu" >&2; exit 1;
}
kotoba_native=${AIUEOS_KOTOBA_NATIVE_ROOT:-"$(dirname -- "$amu")/kotoba-native"}
[ -f "$kotoba_native/src/kotoba/native/elf64.cljc.cljk" ] || {
  echo "error: kotoba-native source not found: $kotoba_native/src" >&2; exit 1;
}

amu_commit=$(git -C "$amu" rev-parse HEAD)
amu_untracked=$(git -C "$amu" ls-files --others --exclude-standard)
[ -z "$amu_untracked" ] || {
  echo "error: Amu source tree has untracked files; provenance cannot include them" >&2
  printf '%s\n' "$amu_untracked" >&2
  exit 1
}
amu_worktree_sha256=$(git -C "$amu" diff --binary HEAD | shasum -a 256 | awk '{print $1}')
kotoba_native_commit=$(git -C "$kotoba_native" rev-parse HEAD)
kotoba_native_untracked=$(git -C "$kotoba_native" ls-files --others --exclude-standard)
[ -z "$kotoba_native_untracked" ] || {
  echo "error: kotoba-native source tree has untracked files; provenance cannot include them" >&2
  printf '%s\n' "$kotoba_native_untracked" >&2
  exit 1
}
kotoba_native_worktree_sha256=$(git -C "$kotoba_native" diff --binary HEAD | shasum -a 256 | awk '{print $1}')

source="$aiueos/native/desktop.kotoba"
browser="$aiueos/kotoba/browser-frame2.kotoba"
catalog="$repo/apps/session/src/aiueos/session/app_catalog.cljk"
font="$aiueos/fonts/aiueos-16.fnt"
[ -f "$source" ] && [ -f "$browser" ] && [ -f "$catalog" ] && [ -f "$font" ] || {
  echo "error: desktop source, Browser frame, shared app catalog, or font payload is missing" >&2; exit 1;
}

stamp=$(date -u +%Y%m%dT%H%M%S)
out=${AIUEOS_NATIVE_DESKTOP_OUT:-"$repo/build/aiueos-native-desktop-$stamp"}
[ ! -e "$out" ] || {
  echo "error: output already exists; choose AIUEOS_NATIVE_DESKTOP_OUT: $out" >&2; exit 1;
}
mkdir -p "$out/esp/EFI/BOOT"
kernel="$out/KERNEL.ELF"
second="$out/KERNEL.reproduced.ELF"
boot="$out/esp/EFI/BOOT/BOOTX64.EFI"
boot_second="$out/BOOTX64.reproduced.EFI"
kernel_receipt="$out/kernel-receipt.json"
boot_receipt="$out/receipt.json"

export AMU_KOTOBA_NATIVE_SRC="$kotoba_native/src"
export AIUEOS_NATIVE_FUEL=${AIUEOS_NATIVE_FUEL:-9007199254740991}
export AIUEOS_NATIVE_K16_PREFLIGHT=0
export AIUEOS_NATIVE_PROFILE=browser-gop-desktop-v1

compile_desktop() {
  run_compiler compile "$source" \
    --source-path "$aiueos" --source-path "$repo/apps/session/src" --unpinned --jvm-free \
    --target x86_64-aiueos-kernel-v1 --artifact image --selfhost-desktop \
    --fuel "$AIUEOS_NATIVE_FUEL" --output "$1"
}
compile_desktop "$kernel"
compile_desktop "$second"
cmp "$kernel" "$second"
rm -f "$second" "$second.provenance.edn" "$second.publication.edn"

python3 "$aiueos/scripts/verify-kotoba-native-desktop.py" \
  "$kernel" "$source" "$browser" "$catalog" "$amu_commit" "$amu_worktree_sha256" \
  "$kotoba_native_commit" "$kotoba_native_worktree_sha256" "$kernel_receipt"

package_with_amu_nbb package-aiueos-boot "$kernel" --output "$boot" \
  --payload "$font" --jvm-free
package_with_amu_nbb package-aiueos-boot "$kernel" --output "$boot_second" \
  --payload "$font" --jvm-free
cmp "$boot" "$boot_second"
rm -f "$boot_second"
python3 "$aiueos/scripts/verify-kotoba-native-boot.py" \
  "$boot" "$kernel" "$amu_commit" "$amu_worktree_sha256" \
  "$kotoba_native_commit" "$kotoba_native_worktree_sha256" "$boot_receipt" "$font"

disk="$out/AIUEOS.img"
disk_second="$out/AIUEOS.reproduced.img"
disk_receipt="$out/disk-receipt.json"
disk_receipt_second="$out/disk-receipt-reproduced.json"
package_with_amu_nbb package-aiueos-disk-image "$boot" --output "$disk" --receipt "$disk_receipt" --jvm-free
package_with_amu_nbb package-aiueos-disk-image "$boot" --output "$disk_second" --receipt "$disk_receipt_second" --jvm-free
cmp "$disk" "$disk_second"
rm -f "$disk_second" "$disk_receipt_second"

foreign=$(find "$out" -type f \( -name '*.c' -o -name '*.o' -o -name '*.obj' \
  -o -name '*.a' -o -name '*.so' \) -print -quit)
[ -z "$foreign" ] || {
  echo "error: foreign/C artifact entered native desktop output: $foreign" >&2; exit 1;
}
echo "AIUEOS_NATIVE_DESKTOP_IMAGE=$disk"
echo "AIUEOS_NATIVE_COMPILER_ROUTE=$compiler_route AIUEOS_NATIVE_PACKAGE_ROUTE=amu-nbb"
