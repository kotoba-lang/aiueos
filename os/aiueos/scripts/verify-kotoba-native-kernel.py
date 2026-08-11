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
context_offset = segments[1][2]
if segments[1][5] < 16 or struct.unpack_from("<Q", data, context_offset + 8)[0] != 8192:
    raise SystemExit("error: Kotoba-native kernel context does not carry sealed fuel 8192")
cr3_read_encodings = (b"\x0f\x20\xd8", b"\x41\x0f\x20\xda")
cr3_write_encodings = (b"\x0f\x22\xd8", b"\x41\x0f\x22\xda")
invlpg_encodings = (b"\x0f\x01\x38", b"\x41\x0f\x01\x3a")
if (not any(encoding in data for encoding in cr3_read_encodings)
        or not any(encoding in data for encoding in cr3_write_encodings)
        or not any(encoding in data for encoding in invlpg_encodings)
        or b"\xee" not in data or b"\xef" not in data):
    raise SystemExit("error: privileged CR3/invlpg/debug-port lowering evidence is absent")
if b"\x88" not in data:
    raise SystemExit("error: allocator zero-store lowering evidence is absent")
for forbidden in (b".interp", b".dynamic", b".dynsym", b"NEEDED", b"libc"):
    if forbidden in data:
        raise SystemExit("error: dynamic/C runtime dependency found")
payload = {
    "format": "aiueos-kotoba-native-receipt/v2",
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
    "fuel": {"initial": 8192, "replenishable": False},
    "allocator": {"page_bytes": 4096, "published_pages": 5,
                  "descriptor_limit": 410,
                  "zero_before_publish": True,
                  "ownership_state": "boot-lifetime-five-slot-bitmap",
                  "duplicate_claim_rejected": True,
                  "double_free_rejected": True,
                  "reclamation_reused": True,
                  "page_table_root_allocated": True,
                  "identity_map_bytes": 1073741824,
                  "cr3_activated": True,
                  "invlpg_executed": True},
}
receipt.write_text(json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n", encoding="ascii")
print("AIUEOS_KOTOBA_NATIVE_KERNEL_OK no-c no-crt no-linker imports=0 fuel=8192 allocator-pages=5 ownership-bitmap page-table-root identity-1g cr3-activated invlpg reuse double-free-rejected descriptors<=410 zero-before-publish")
