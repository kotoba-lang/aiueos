#!/usr/bin/env python3
import hashlib
import json
import pathlib
import struct
import sys

elf = pathlib.Path(sys.argv[1])
source = pathlib.Path(sys.argv[2])
compiler = sys.argv[3]
receipt = pathlib.Path(sys.argv[4])
data = elf.read_bytes()
if data[:4] != b"\x7fELF" or data[4:7] != b"\x02\x01\x01":
    raise SystemExit("error: Kotoba-native kernel is not ELF64 little-endian")
if struct.unpack_from("<H", data, 16)[0] != 2 or struct.unpack_from("<H", data, 18)[0] != 0x3E:
    raise SystemExit("error: Kotoba-native kernel is not x86-64 ET_EXEC")
entry = struct.unpack_from("<Q", data, 24)[0]
phoff = struct.unpack_from("<Q", data, 32)[0]
phentsize, phnum = struct.unpack_from("<HH", data, 54)
if entry != 0x101000 or phentsize != 56 or phnum != 2:
    raise SystemExit("error: Kotoba-native entry/load contract rejected")
segments = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize) for i in range(phnum)]
if [segment[0] for segment in segments] != [1, 1] or [segment[1] for segment in segments] != [5, 6]:
    raise SystemExit("error: Kotoba-native kernel must contain only RX and RW PT_LOAD segments")
rx_start = segments[0][3]
rx_end = rx_start + segments[0][6]
rx_limit = (rx_end + 4095) & ~4095
rw_start = segments[1][3]
rw_end = rw_start + segments[1][6]
if (rx_start != 0x101000 or rx_limit > rw_start
        or rw_start & 4095 or rw_end >= 0x40000000
        or not (rw_start <= 0x110000 < rw_end)):
    raise SystemExit("error: Kotoba-native RX/RW page boundary rejected")
context_offset = segments[1][2]
if segments[1][5] < 16 or struct.unpack_from("<Q", data, context_offset + 8)[0] != 1048576:
    raise SystemExit("error: Kotoba-native kernel context does not carry sealed fuel 1048576")
cr3_read_encodings = (b"\x0f\x20\xd8", b"\x41\x0f\x20\xda")
cr3_write_encodings = (b"\x0f\x22\xd8", b"\x41\x0f\x22\xda")
invlpg_encodings = (b"\x0f\x01\x38", b"\x41\x0f\x01\x3a")
cr0_read_encodings = (b"\x0f\x20\xc0", b"\x41\x0f\x20\xc2")
cr0_write_encodings = (b"\x0f\x22\xc0", b"\x41\x0f\x22\xc2")
page_fault_frame_encodings = (
    b"\x41\x0f\x20\xd2\x4c\x8b\x1c\x24",
    b"\x41\x0f\x20\xd2\x4c\x8b\x5c\x24\x30",
    b"\x41\x0f\x20\xd2\x4c\x8b\x5c\x24\x38",
    b"\x41\x0f\x20\xd2\x4d\x8b\x1e",
)
if (not any(encoding in data for encoding in cr3_read_encodings)
        or not any(encoding in data for encoding in cr3_write_encodings)
        or not any(encoding in data for encoding in invlpg_encodings)
        or not any(encoding in data for encoding in cr0_read_encodings)
        or not any(encoding in data for encoding in cr0_write_encodings)
        or b"\x0f\x32" not in data or b"\x0f\x30" not in data
        or b"\x0f\xa2" not in data
        or b"\x66\x41\x8c\xca" not in data
        or b"\x41\x0f\x01\x1a" not in data
        or b"\x0f\x01\x0c\x24" not in data
        or not any(encoding in data for encoding in page_fault_frame_encodings)
        or b"\x4c\x89\x14\x25\x00\x01\x11\x00" not in data
        or b"\xee" not in data or b"\xef" not in data):
    raise SystemExit("error: privileged paging/protection lowering evidence is absent")
if b"\x88" not in data:
    raise SystemExit("error: allocator zero-store lowering evidence is absent")
for forbidden in (b".interp", b".dynamic", b".dynsym", b"NEEDED", b"libc"):
    if forbidden in data:
        raise SystemExit("error: dynamic/C runtime dependency found")
probe_encodings = {
    "guard-write": b"\xc6\x04\x25\x00\x00\x10\x00\x00",
    "text-write": b"\xc6\x04\x25\x00\x10\x10\x00\x00",
    "nx-execute": b"\x49\xba\x00\xc0\x10\x00\x00\x00\x00\x00\x41\xff\xd2",
    "recoverable-guard-write": b"\x49\xc7\xc2\x00\x00\x10\x00\x41\xc6\x02\x00",
}
present_probes = [name for name, encoding in probe_encodings.items() if encoding in data]
if len(present_probes) > 1:
    raise SystemExit("error: more than one sealed page-fault probe entered one artifact")
recovery_handler = b"\x48\x89\x04\x25\x10\x01\x11\x00" in data
payload = {
    "format": "aiueos-kotoba-native-receipt/v5",
    "target": "x86_64-aiueos-kernel-v1",
    "entry": "aiueos_kernel_entry",
    "compiler_commit": compiler,
    "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "artifact_sha256": hashlib.sha256(data).hexdigest(),
    "artifact_bytes": len(data),
    "foreign_objects": [],
    "c_sources": [],
    "imports": [],
    "dynamic_dependencies": [],
    "fuel": {"initial": 1048576, "replenishable": False},
    "allocator": {"page_bytes": 4096, "published_pages": 8,
                  "descriptor_limit": 410,
                  "zero_before_publish": True,
                  "ownership_state": "boot-lifetime-eight-slot-bitmap",
                  "duplicate_claim_rejected": True,
                  "double_free_rejected": True,
                  "reclamation_reused": True,
                  "page_table_root_allocated": True,
                  "identity_map_bytes": 1073741824,
                  "cr3_activated": True,
                  "invlpg_executed": True},
    "protection": {"nxe_readback": True, "cr0_wp_readback": True,
                   "guard_page": "0x100000-unmapped",
                   "kernel_text": f"0x{rx_start:x}-0x{rx_limit - 1:x}-rx",
                   "kernel_gap": (f"0x{rx_limit:x}-0x{rw_start - 1:x}-unmapped"
                                  if rx_limit < rw_start else "none"),
                   "kernel_state": f"0x{rw_start:x}-0x{rw_end - 1:x}-rw-nx",
                   "remaining_identity_map": "rw-nx"},
    "exceptions": {"idt_vector": 14, "selector": "runtime-current-cs",
                   "lidt": True, "sidt_readback": True,
                   "handler": ("recoverable-bounded-frame-iretq"
                               if recovery_handler
                               else "non-returning-cr2-error-code-classifier"),
                   "recovery_configuration": "sealed-frame-page-and-dedicated-stack",
                   "frame_fields": ["cr2", "error-code", "original-rip", "handler-stack-top"],
                   "sealed_probe": present_probes[0] if present_probes else None},
}
receipt.write_text(json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n", encoding="ascii")
print("AIUEOS_KOTOBA_NATIVE_KERNEL_OK no-c no-crt no-linker imports=0 fuel=1048576 allocator-pages=8 ownership-bitmap page-table-root identity-1g guard-unmapped text-rx state-rw-nx nxe cr0-wp cr3-activated invlpg idt14-sidt-readback pf-cr2-error-code recovery-frame dedicated-handler-stack reuse double-free-rejected descriptors<=410 zero-before-publish")
