#!/bin/sh
set -eu

volume=${1:-}
if [ -z "$volume" ]; then
  for candidate in "/Volumes/AIUEOS QUAL" "/Volumes/AIUEOS RSLT"; do
    if [ -f "$candidate/EFI/AIUEOS/RESULT.LOG" ]; then
      volume=$candidate
      break
    fi
  done
fi
[ -n "$volume" ] || {
  echo "error: AIUEOS result volume is not mounted" >&2
  exit 2
}

result="$volume/EFI/AIUEOS/RESULT.LOG"
probe="$volume/EFI/AIUEOS/PROBE.LOG"
[ -f "$result" ] || { echo "error: RESULT.LOG is absent" >&2; exit 2; }
[ -f "$probe" ] || { echo "error: PROBE.LOG is absent" >&2; exit 2; }

result_text=$(LC_ALL=C tr -d '\000\r' < "$result")
probe_text=$(LC_ALL=C tr -d '\000\r' < "$probe")

for marker in \
  "AIUEOS_K16_RESULT_V2" \
  "internal_ssd_writes=none" \
  "usb_log_writes=self-only" \
  "qualification_variable_cleared=yes"; do
  printf '%s\n' "$result_text" | grep -Fx "$marker" >/dev/null || {
    echo "error: RESULT.LOG missing marker: $marker" >&2
    exit 3
  }
done
for marker in \
  "AIUEOS_HW_PROBE_CPU vendor=" \
  "AIUEOS_HW_PROBE_GOP capability=present" \
  "AIUEOS_HW_PROBE_MEMORY capability=present" \
  "AIUEOS_HW_PROBE_ACPI rsdp=present" \
  "AIUEOS_HW_PROBE_PCI capability=present" \
  "AIUEOS_HW_PROBE_BLOCK capability=present" \
  "AIUEOS_HW_PROBE_DONE exit_boot_services=no internal_disk_writes=none"; do
  printf '%s\n' "$probe_text" | grep -F "$marker" >/dev/null || {
    echo "error: PROBE.LOG missing marker: $marker" >&2
    exit 3
  }
done

state=$(printf '%s\n' "$result_text" | sed -n 's/^state=//p' | head -1)
code=$(printf '%s\n' "$result_text" | sed -n 's/^code=//p' | head -1)
case "$state" in
  success)
    [ "$code" = 0 ] || { echo "error: success result has code=$code" >&2; exit 3; }
    printf 'AIUEOS_K16_PHYSICAL_RESULT_OK state=success code=0 internal-ssd-writes=none\n'
    ;;
  failure)
    printf 'AIUEOS_K16_PHYSICAL_RESULT_FAIL state=failure code=%s internal-ssd-writes=none\n' "$code" >&2
    exit 4
    ;;
  incomplete)
    printf 'AIUEOS_K16_PHYSICAL_RESULT_INCOMPLETE code=%s internal-ssd-writes=none\n' "$code" >&2
    exit 5
    ;;
  *)
    echo "error: unknown qualification state" >&2
    exit 3
    ;;
esac
