#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}
log="$out/uefi-debug.log"
serial_log="$out/kernel-serial.log"
blk_image="$out/virtio-blk-smoke.img"
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}

AIUEOS_INPUT_SMOKE_SYNTHETIC=1 AIUEOS_CATALOG_POLICY_SELFTEST=1 \
  "$aiueos/scripts/build-uefi.sh" >/dev/null
if [ "${AIUEOS_CORRUPT_KERNEL:-0}" = 1 ]; then
  python3 - "$out/esp/EFI/AIUEOS/KERNEL.ELF" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
data[-1] ^= 0x01
path.write_bytes(data)
PY
fi
command -v "$qemu" >/dev/null 2>&1 || {
  echo "error: qemu-system-x86_64 is required" >&2
  exit 1
}

if [ -z "${OVMF_CODE:-}" ]; then
  for candidate in \
    /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
    /opt/homebrew/Cellar/qemu/*/share/qemu/edk2-x86_64-code.fd \
    /usr/share/OVMF/OVMF_CODE_4M.fd \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/x64/OVMF_CODE.fd; do
    if [ -f "$candidate" ]; then OVMF_CODE=$candidate; break; fi
  done
fi
[ -f "${OVMF_CODE:-}" ] || {
  echo "error: OVMF firmware not found; set OVMF_CODE" >&2
  exit 1
}

rm -f "$log" "$serial_log"
if [ "${AIUEOS_PRESERVE_BLK_IMAGE:-0}" != 1 ] || [ ! -f "$blk_image" ]; then
python3 "$aiueos/scripts/make-aiuefs-image.py" \
  --entry "app/hello,$aiueos/kotoba/user-smoke.elf,$aiueos/kotoba/user-smoke.sig" \
  --entry "app/worker,$aiueos/kotoba/user-smoke.elf,$aiueos/kotoba/user-smoke.sig" \
  --catalog-signature "$aiueos/kotoba/app-catalog.sig" --output "$blk_image"
fi
if [ "${AIUEOS_CORRUPT_KOTOBA_APP:-0}" = 1 ]; then
python3 - "$blk_image" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes()); b[4*512+64]^=1; p.write_bytes(b)
PY
fi
if [ "${AIUEOS_CORRUPT_KOTOBA_SIGNATURE:-0}" = 1 ]; then
python3 - "$blk_image" <<'PY'
from pathlib import Path
import struct, sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes()); catalog_sector=struct.unpack_from('<I',b,36)[0]
signature_sector=struct.unpack_from('<I',b,catalog_sector*512+16+56)[0]
b[signature_sector*512+17]^=1; p.write_bytes(b)
PY
fi
if [ "${AIUEOS_CORRUPT_KOTOBA_CATALOG:-0}" = 1 ]; then
python3 - "$blk_image" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]);b=bytearray(p.read_bytes());sector=struct.unpack_from('<I',b,36)[0]
b[sector*512+20]^=1;p.write_bytes(b)
PY
fi
if [ -n "${AIUEOS_CDROM_IMAGE:-}" ]; then
  [ -f "$AIUEOS_CDROM_IMAGE" ] || {
    echo "error: AIUEOS_CDROM_IMAGE does not exist: $AIUEOS_CDROM_IMAGE" >&2
    exit 1
  }
  # El Torito boot from the release ISO; cdrom media is opened read-only.
  boot_drive="format=raw,media=cdrom,file=$AIUEOS_CDROM_IMAGE"
elif [ -n "${AIUEOS_DISK_IMAGE:-}" ]; then
  [ -f "$AIUEOS_DISK_IMAGE" ] || {
    echo "error: AIUEOS_DISK_IMAGE does not exist: $AIUEOS_DISK_IMAGE" >&2
    exit 1
  }
  # OVMF may open the boot medium writable; snapshot keeps the release artifact immutable.
  boot_drive="format=raw,snapshot=on,file=$AIUEOS_DISK_IMAGE"
else
  boot_drive="format=raw,file=fat:rw:$out/esp"
fi
set +e
iommu_args=
if [ "${AIUEOS_TEST_DMAR:-0}" = 1 ]; then iommu_args="-device intel-iommu,intremap=on"; fi
# shellcheck disable=SC2086 # intentional optional pair of QEMU arguments
"$qemu" \
  -machine q35,accel=tcg -cpu max -m 128M -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive "$boot_drive" \
  -device isa-debugcon,iobase=0xe9,chardev=debug \
  -chardev file,id=debug,path="$log" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  $iommu_args \
  -device virtio-rng-pci \
  -drive if=none,id=aiueosblk,format=raw,file="$blk_image" \
  -device virtio-blk-pci,drive=aiueosblk,disable-legacy=on \
  -device virtio-keyboard-pci,disable-legacy=on \
  -device virtio-vga,disable-legacy=on \
  -display none -serial "file:$serial_log" -monitor none -no-reboot
status=$?
set -e

if [ "${AIUEOS_CORRUPT_KERNEL:-0}" = 1 ]; then
  [ "$status" -eq 255 ] || {
    echo "error: corrupted kernel produced unexpected QEMU status $status" >&2
    exit 1
  }
  grep -F "AIUEOS_LOADER_FAIL kernel-sha256" "$log" >/dev/null || {
    echo "error: corrupted kernel was not rejected by loader" >&2
    exit 1
  }
  echo "AIUEOS_KERNEL_INTEGRITY_REJECTION_OK"
  exit 0
fi

if [ "${AIUEOS_EXPECT_FAULT:-0}" = 1 ]; then
  # The unexpected-exception path writes 0x7d; isa-debug-exit maps it to 251.
  [ "$status" -eq 251 ] || {
    echo "error: synthetic fault produced unexpected QEMU status $status" >&2
    test -f "$serial_log" && tail -20 "$serial_log" >&2
    exit 1
  }
  grep -F "AIUEOS_FAULT_SMOKE synthetic unexpected-ud2" "$serial_log" >/dev/null || {
    echo "error: synthetic fault trigger marker was not observed" >&2
    exit 1
  }
  grep -F "AIUEOS_FAULT_RECEIPT_OK polled try-lock written readback pending" "$serial_log" >/dev/null || {
    echo "error: fault-context crash receipt write evidence was not observed" >&2
    exit 1
  }
  echo "AIUEOS_FAULT_BOOT_OK synthetic-fault polled-receipt-written"
  exit 0
fi

if [ "${AIUEOS_EXPECT_CRASH:-0}" = 1 ]; then
  # The synthetic panic writes 0x5c; isa-debug-exit maps it to (0x5c << 1) | 1.
  [ "$status" -eq 185 ] || {
    echo "error: synthetic panic produced unexpected QEMU status $status" >&2
    test -f "$serial_log" && tail -20 "$serial_log" >&2
    exit 1
  }
  grep -F "AIUEOS_PANIC synthetic reason=42" "$serial_log" >/dev/null || {
    echo "error: synthetic panic marker was not observed" >&2
    exit 1
  }
  grep -F "AIUEOS_PANIC_RECEIPT_OK synthetic reason=42 written readback pending" "$serial_log" >/dev/null || {
    echo "error: durable crash receipt write evidence was not observed" >&2
    exit 1
  }
  echo "AIUEOS_CRASH_PANIC_BOOT_OK synthetic-panic receipt-written"
  exit 0
fi

# The #UD handler writes 0x30; isa-debug-exit maps it to (0x30 << 1) | 1 = 97.
[ "$status" -eq 97 ] || {
  if { [ "${AIUEOS_CORRUPT_KOTOBA_APP:-0}" = 1 ] ||
       [ "${AIUEOS_CORRUPT_KOTOBA_SIGNATURE:-0}" = 1 ] ||
       [ "${AIUEOS_CORRUPT_KOTOBA_CATALOG:-0}" = 1 ]; } && [ "$status" -eq 227 ]; then
    ! grep -F "AIUEOS_KOTOBA_APP_ADMISSION_OK" "$serial_log" >/dev/null || {
      echo "error: corrupted Kotoba app reached admission" >&2; exit 1;
    }
    echo "AIUEOS_KOTOBA_APP_AUTH_REJECTION_OK digest-or-signature"
    exit 0
  fi
  echo "error: unexpected QEMU exit status $status" >&2
  test -f "$log" && sed -n '1,80p' "$log" >&2
  exit 1
}
grep -F "AIUEOS_LOADER_OK" "$log" >/dev/null || {
  echo "error: loader identity was not observed" >&2
  exit 1
}
grep -F "AIUEOS_GOP_HANDOFF_OK framebuffer-v1" "$log" >/dev/null || {
  echo "error: loader did not hand off a validated GOP mode" >&2
  exit 1
}
grep -F "AIUEOS_LOADER_INTEGRITY_OK sha256-v1" "$log" >/dev/null || {
  echo "error: kernel integrity evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KERNEL_OK memory-map-v1" "$log" >/dev/null || {
  echo "error: kernel handoff was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERIAL_OK stack-v1 memory-map-v1" "$serial_log" >/dev/null || {
  echo "error: kernel COM1 evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,80p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_NATIVE_OK elf64-relocatable sysv-v1 result=42" "$serial_log" >/dev/null || {
  echo "error: Kotoba compiler-emitted native probe did not execute" >&2
  exit 1
}
grep -F "AIUEOS_DESCRIPTOR_TABLES_OK gdt-v1 idt-v1" "$serial_log" >/dev/null || {
  echo "error: kernel descriptor-table evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PAGING_OK cr3-owned wx-v1 nx-wp" "$serial_log" >/dev/null || {
  echo "error: kernel-owned paging evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_FRAMEBUFFER_OK gop-owned retained-rectangles hash-verified" "$serial_log" >/dev/null || {
  echo "error: kernel did not validate and render the GOP framebuffer" >&2
  exit 1
}
grep -F "AIUEOS_DESKTOP_SURFACE_OK envelope-v1 opaque-handle full-damage hash-verified" "$serial_log" >/dev/null || {
  echo "error: bounded desktop surface envelope was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PHYSICAL_ALLOCATOR_OK pages=2 zeroed" "$serial_log" >/dev/null || {
  echo "error: physical page allocator evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_ACPI_OK rsdp-xsdt-madt cpu>=2" "$serial_log" >/dev/null || {
  echo "error: validated ACPI CPU discovery evidence was not observed" >&2
  exit 1
}
if [ "${AIUEOS_TEST_DMAR:-0}" = 1 ]; then
  grep -F "AIUEOS_VTD_OK tes=1 root-context-slpt domain=1 aperture=128MiB" "$serial_log" >/dev/null || {
    echo "error: VT-d translation-enable register evidence was not observed" >&2; exit 1;
  }
  grep -F "AIUEOS_DMA_POLICY_OK dmar=validated dma=vtd-isolated" "$serial_log" >/dev/null || {
    echo "error: isolated VT-d DMA policy evidence was not observed" >&2; exit 1;
  }
else
  grep -F "AIUEOS_DMA_POLICY_OK dmar=absent test-only-unisolated" "$serial_log" >/dev/null || {
    echo "error: explicit no-IOMMU test DMA policy evidence was not observed" >&2; exit 1;
  }
fi
grep -F "AIUEOS_APIC_TIMER_OK vector=32 eoi-v1" "$serial_log" >/dev/null || {
  echo "error: Local APIC timer interrupt evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SMP_OK cpus=2 init-sipi-v1 per-cpu-stack" "$serial_log" >/dev/null || {
  echo "error: BSP-to-AP INIT/SIPI evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PCI_OK bounded-scan virtio-vendor=1af4" "$serial_log" >/dev/null || {
  echo "error: bounded PCI/virtio discovery evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,160p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_RNG_OK modern-pci caps-bounded dma=4pages completion=32" "$serial_log" >/dev/null || {
  echo "error: modern virtio-rng DMA completion evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,120p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_RNG_MSIX_OK vector=34 irq=1 table-pba-bounded" "$serial_log" >/dev/null || {
  echo "error: interrupt-driven virtio-rng MSI-X evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,140p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_BLK_OK capacity-bounded sector=0 bytes=512 readonly" "$serial_log" >/dev/null || {
  echo "error: modern virtio-blk bounded read evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,140p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_BLK_MSIX_OK vector=35 irq-completions-bounded table-pba-bounded" "$serial_log" >/dev/null || {
  echo "error: interrupt-driven virtio-blk MSI-X completion evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,160p' "$serial_log" >&2
  exit 1
}
if [ "${AIUEOS_TEST_DMAR:-0}" = 1 ]; then
  grep -F "AIUEOS_VTD_IR_OK irta=256 source-validated vector=35 remappable-msix" "$serial_log" >/dev/null || {
    echo "error: VT-d interrupt-remapped MSI-X evidence was not observed" >&2; exit 1;
  }
fi
grep -F "AIUEOS_OBJECT_STORE_OK aiuefs-v3 objects=3 catalog=2apps" "$serial_log" >/dev/null || {
  echo "error: bounded read-only object-store evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_APP_ADMISSION_OK catalog=rsa2048 apps=2 digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key" "$serial_log" >/dev/null || {
  echo "error: authenticated object-store Kotoba app admission was not observed" >&2
  exit 1
}
# The catalog-policy self-test is a test-only compile gate. The update-flow
# smoke boots a previous-version image built without it and asserts the
# marker's absence to prove which version booted.
if [ "${AIUEOS_EXPECT_CATALOG_POLICY_SELFTEST:-1}" = 1 ]; then
  grep -F "AIUEOS_KOTOBA_CATALOG_POLICY_SELFTEST_OK malformed=6" "$serial_log" >/dev/null || {
    echo "error: Kotoba catalog policy malformed-input evidence missing" >&2
    cat "$serial_log" >&2
    exit 1
  }
else
  ! grep -F "AIUEOS_KOTOBA_CATALOG_POLICY_SELFTEST_OK" "$serial_log" >/dev/null || {
    echo "error: unexpected catalog-policy self-test evidence in previous-version boot" >&2
    exit 1
  }
fi
grep -F "AIUEOS_JOURNAL_OK dual-slot committed append-readback" "$serial_log" >/dev/null || {
  echo "error: journal write/readback evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_OBJECT_TXN_OK journal-first sector=3 apply-readback route=kotoba fixed-stack" "$serial_log" >/dev/null || {
  echo "error: journal-backed object transaction evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_JOURNAL_PLAN_OK" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native journal planning evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_FNV_OK bounded-load journal-object-validation" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native bounded FNV evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_RECORD_VALIDATION_OK journal transaction bounded-u32" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native record validation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_STORAGE_READ_VALIDATION_OK superblock mutable-object" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native storage read validation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_STORAGE_WRITE_OK journal mutable-object bounded-store" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native storage write evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_PCI_PLANNER_OK cap extent msix-region" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native PCI planner evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured synthetic-smoke" "$serial_log" >/dev/null || {
  echo "error: modern virtio-input configuration/synthetic transport evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_DESKTOP_INPUT_OK envelope-v1 sequence=1 kind=key ime-neutral" "$serial_log" >/dev/null || {
  echo "error: validated browser desktop input envelope was not observed" >&2; exit 1;
}
grep -F "AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded" "$serial_log" >/dev/null || {
  echo "error: bounded virtio-gpu display-info completion was not observed" >&2
  exit 1
}
grep -F "AIUEOS_BROWSER_DESKTOP_TRANSPORT_OK surface-v1 gpu-scanout-bound input-v1" "$serial_log" >/dev/null || {
  echo "error: framebuffer/browser desktop transport binding was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SCHEDULER_OK tasks=2 policy=round-robin preemption=apic-timer" "$serial_log" >/dev/null || {
  echo "error: preemptive round-robin scheduler evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SCHEDULER_CR3_OK roots=3 private-pages=2 kernel-return" "$serial_log" >/dev/null || {
  echo "error: scheduler-driven address-space switching evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERVICE_RUNTIME_OK services=2 descriptors=8 kotoba-policy spawn-restart-terminate task=generic generation=2 budget=bounded" "$serial_log" >/dev/null || {
  echo "error: persistent service runtime evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERVICE_IPC_OK mailbox=bounded capability=owner-domain cross-cr3 sequence=1" "$serial_log" >/dev/null || {
  echo "error: capability-checked cross-address-space service IPC evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERVICE_REGISTRY_OK journal-object ids=2 generation=2,1 restart=1,0 decoder=kotoba fixed-stack" "$serial_log" >/dev/null || {
  echo "error: durable service registry transaction evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_IOAPIC_OK pit-gsi vector=33 eoi-v1" "$serial_log" >/dev/null || {
  echo "error: IOAPIC external timer IRQ evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SYSCALL_OK int80-cpl0 abi-v1" "$serial_log" >/dev/null || {
  echo "error: CPL0 syscall evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_SYSCALL_PLANNER_OK bootstrap user overflow" "$serial_log" >/dev/null || {
  echo "error: Kotoba syscall range planner evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_COPY_IN_OK cpl0 hash bounded-256" "$serial_log" >/dev/null || {
  echo "error: Kotoba bounded copy-in evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_CAPABILITY_OK table owner generation type rights revoke reissue derivation=multi-hop recursive-revoke" "$serial_log" >/dev/null || {
  echo "error: capability negative evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_DYNAMIC_CAPABILITY_OK page-backed slots>=256 owner=3 reuse generation retirement" "$serial_log" >/dev/null || {
  echo "error: dynamic page-backed capability table evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PROCESS_FOUNDATION_OK tss-descriptor user-wx guard-page" "$serial_log" >/dev/null || {
  echo "error: process isolation foundation evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_ADDRESS_SPACE_OK processes=2 distinct-cr3 private-pages cross-access-fault" "$serial_log" >/dev/null || {
  echo "error: per-process address-space isolation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_RING3_OK processes=2 preemptive roots=2 domains=2,3 kernel-stacks=2 syscall-sysret" "$serial_log" >/dev/null || {
  echo "error: CPL3 syscall and kernel-return evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_PROCESS_CREATE_OK descriptors=8 entry-argument-stack domain-address-space-task" "$serial_log" >/dev/null || {
  echo "process-create ABI evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_SYSRET_OK star-lstar-fmask canonical-rip-rsp rflags-sanitized per-task-stack" "$serial_log" >/dev/null || {
  echo "error: native syscall/sysret evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_CAPABILITY_TRANSFER_OK source=2 target=3 attenuated atomic-claim transferred-use owner-exit=descendants-revoked" "$serial_log" >/dev/null || {
  echo "error: atomic process capability transfer evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PROCESS_REAP_OK tasks=4 services=2-persistent process-slots=8 task-slots=8 generations=reused owner-caps-revoked allocator-pages=24 stack-pages=reused zero-reused" "$serial_log" >/dev/null || {
  echo "error: process exit/reap/reuse evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_ELF_PROCESS_OK source=catalog apps=2 et-exec segments=rx,rw result=42 domains=4,5" "$serial_log" >/dev/null || {
  echo "Kotoba ELF process evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_USER_RUNTIME_OK abi=v2 transport=syscall capabilities=2,3,4,5 object=service-registry,user-store service-ipc=mailbox domains=4,5 result=42" "$serial_log" >/dev/null || {
  echo "error: Kotoba user runtime syscall evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_SERVICE_IPC_OK senders=4,5 recipients=service0,service1 payload=42 sequence=1 bounded=2 persistent-services=2" "$serial_log" >/dev/null || {
  echo "error: Kotoba to persistent service IPC evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_OBJECT_WRITE_OK domains=4,5 journals=44-47 objects=42,43 value=42 receipt=readback transaction=journal-first serializer=kotoba validator=kotoba decoder=kotoba materializer=kotoba fixed-stack" "$serial_log" >/dev/null || {
  echo "error: Kotoba user object transaction evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_APP_CATALOG_LOOKUP_OK ids=app/hello,app/worker unknown=denied extents=nonoverlap" "$serial_log" >/dev/null || {
  echo "catalog lookup evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_USER_SYSCALL_OK valid-log copied-payload too-big stale-generation foreign-owner wrong-type no-rights invalid-pointer" "$serial_log" >/dev/null || {
  echo "error: CPL3 syscall positive/negative evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_COPYIN_OK noncanonical-and-unmapped-denied" "$serial_log" >/dev/null || {
  echo "error: invalid-pointer evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PAGE_FAULT_OK write-protect vector=14" "$serial_log" >/dev/null || {
  echo "error: write-protect page-fault evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PAGE_FAULT_OK no-execute vector=14" "$serial_log" >/dev/null || {
  echo "error: no-execute page-fault evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_EXCEPTION_OK vector=6 invalid-opcode" "$serial_log" >/dev/null || {
  echo "error: kernel exception dispatch evidence was not observed" >&2
  exit 1
}
echo "AIUEOS_UEFI_SMOKE_OK"
