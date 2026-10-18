#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
amu=${1:?usage: AIUEOS_AMU_SELFHOST_BIN=/path/to/native/amu build-kotoba-native-kernel.sh /path/to/amu-source}
amu_bin=${AIUEOS_AMU_SELFHOST_BIN:-}
[ -n "$amu_bin" ] && [ -x "$amu_bin" ] || {
  echo "error: set AIUEOS_AMU_SELFHOST_BIN to an executable Amu selfhost compiler" >&2; exit 1;
}
amu_magic=$(LC_ALL=C od -An -tx1 -N4 "$amu_bin" | tr -d '[:space:]')
case "$amu_magic" in
  cffaedfe|feedfacf|7f454c46) ;;
  *) echo "error: AIUEOS_AMU_SELFHOST_BIN must be a native Mach-O or ELF executable, not a launcher: $amu_bin" >&2; exit 1 ;;
esac
amu_binary_sha256=$(shasum -a 256 "$amu_bin" | awk '{print $1}')
export AIUEOS_AMU_SELFHOST_BINARY_SHA256="$amu_binary_sha256"
git -C "$amu" rev-parse --show-toplevel >/dev/null || {
  echo "error: Amu source tree not found: $amu" >&2; exit 1;
}
kotoba_native=${AIUEOS_KOTOBA_NATIVE_ROOT:-"$(dirname -- "$amu")/kotoba-native"}
[ -f "$kotoba_native/src/kotoba/native/elf64.cljc.cljk" ] || {
  echo "error: kotoba-native source not found: $kotoba_native/src" >&2; exit 1;
}
amu_commit=$(git -C "$amu" rev-parse HEAD)
untracked=$(git -C "$amu" ls-files --others --exclude-standard)
[ -z "$untracked" ] || {
  echo "error: Amu source tree has untracked files; provenance cannot include them" >&2
  printf '%s\n' "$untracked" >&2
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
export AMU_KOTOBA_NATIVE_SRC="$kotoba_native/src"
fuel=${AIUEOS_NATIVE_FUEL:-1073741824}
out=${AIUEOS_NATIVE_OUT:-"$repo/build/aiueos-native"}
kernel="$out/KERNEL.ELF"
source=${AIUEOS_NATIVE_KERNEL_SOURCE:-"$aiueos/native/kernel.kotoba"}
second="$out/KERNEL.reproduced.ELF"
receipt="$out/receipt.json"
link_frame_source=${AIUEOS_LINK_FRAME_SOURCE_PATH:-"$repo/../capability-link-frame/kotoba"}
dma_map_source=${AIUEOS_DMA_MAP_SOURCE_PATH:-"$repo/../capability-dma-map/kotoba"}
mmio_map_source=${AIUEOS_MMIO_MAP_SOURCE_PATH:-"$repo/../capability-mmio-map/kotoba"}
net_transport_source=${AIUEOS_NET_TRANSPORT_SOURCE_PATH:-"$repo/../capability-net-transport/kotoba"}
org_ietf_tcp_source=${AIUEOS_ORG_IETF_TCP_SOURCE_PATH:-"$repo/../org-ietf-tcp/kotoba"}
link_frame_commit=ae212c6311d51024ffddaf0c8f86886993fc95f4
dma_map_commit=f0b45b04a1679d49c7cd2ecdec0815cff045b330
mmio_map_commit=761147d8e09c7eab414c27fc6cadee3810847637
net_transport_commit=33ea97c229741c98681e4563c5d3ec8a7697f91d
org_ietf_tcp_commit=6532f6b13ddfa0404a050bab174cbbc5f036d335
require_source_commit() {
  label=$1
  source_root=$2
  expected_commit=$3
  [ -d "$source_root" ] || {
    echo "error: $label source root not found: $source_root" >&2
    exit 1
  }
  source_repo=$(git -C "$source_root" rev-parse --show-toplevel)
  source_commit=$(git -C "$source_repo" rev-parse HEAD)
  [ "$source_commit" = "$expected_commit" ] || {
    echo "error: $label HEAD is $source_commit; expected $expected_commit" >&2
    exit 1
  }
  git -C "$source_repo" diff --quiet HEAD -- "$source_root" || {
    echo "error: $label native source differs from committed $expected_commit" >&2
    exit 1
  }
}
require_source_commit link/frame "$link_frame_source" "$link_frame_commit"
require_source_commit dma/map "$dma_map_source" "$dma_map_commit"
require_source_commit mmio/map "$mmio_map_source" "$mmio_map_commit"
require_source_commit net/transport "$net_transport_source" "$net_transport_commit"
require_source_commit org-ietf-tcp "$org_ietf_tcp_source" "$org_ietf_tcp_commit"
mkdir -p "$out"
"$amu_bin" compile "$source" \
  --source-path "$aiueos" \
  --source-path "$link_frame_source" \
  --source-path "$dma_map_source" \
  --source-path "$mmio_map_source" \
  --source-path "$net_transport_source" \
  --source-path "$org_ietf_tcp_source" --unpinned \
  --jvm-free --target x86_64-aiueos-kernel-v1 --artifact image --fuel "$fuel" --output "$kernel"
"$amu_bin" compile "$source" \
  --source-path "$aiueos" \
  --source-path "$link_frame_source" \
  --source-path "$dma_map_source" \
  --source-path "$mmio_map_source" \
  --source-path "$net_transport_source" \
  --source-path "$org_ietf_tcp_source" --unpinned \
  --jvm-free --target x86_64-aiueos-kernel-v1 --artifact image --fuel "$fuel" --output "$second"
cmp "$kernel" "$second"
rm -f "$second"
python3 "$aiueos/scripts/verify-kotoba-native-kernel.py" \
  "$kernel" "$source" "$amu_commit" "$amu_worktree_sha256" \
  "$kotoba_native_commit" "$kotoba_native_worktree_sha256" "$receipt" \
  "$link_frame_source/capability/link/frame.kotoba" "$link_frame_commit" \
  "$dma_map_source/capability/dma/map.kotoba" "$dma_map_commit" \
  "$mmio_map_source/capability/mmio/map.kotoba" "$mmio_map_commit" \
  "$net_transport_source/capability/net/transport.kotoba" "$net_transport_commit" \
  "$org_ietf_tcp_source/tcp/seq_core.kotoba" "$org_ietf_tcp_commit" \
  "$org_ietf_tcp_source/tcp/reassemble_core.kotoba" "$org_ietf_tcp_commit"
foreign=$(find "$out" -type f \( -name '*.c' -o -name '*.o' -o -name '*.obj' -o -name '*.a' -o -name '*.so' \) \
  -print -quit)
[ -z "$foreign" ] || {
  echo "error: foreign/C artifact entered native output: $foreign" >&2; exit 1;
}
