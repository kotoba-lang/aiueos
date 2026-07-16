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
if b"\x0f\x20\xd8" not in data or b"\xee" not in data or b"\xef" not in data:
    raise SystemExit("error: privileged CR3/debug-port lowering evidence is absent")
for forbidden in (b".interp", b".dynamic", b".dynsym", b"NEEDED", b"libc"):
    if forbidden in data:
        raise SystemExit("error: dynamic/C runtime dependency found")
payload = {
    "format": "aiueos-kotoba-native-receipt/v1",
    "target": "x86_64-aiueos-kernel-v1",
    "entry": "aiueos_kernel_entry",
    "compiler_commit": compiler,
    "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "artifact_sha256": hashlib.sha256(data).hexdigest(),
    "artifact_bytes": len(data),
    "foreign_objects": [],
    "c_sources": [],
    "dynamic_dependencies": [],
}
receipt.write_text(json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n", encoding="ascii")
print("AIUEOS_KOTOBA_NATIVE_KERNEL_OK no-c no-crt no-linker imports=0")
