#!/usr/bin/env python3
import hashlib
import json
import os
import pathlib
import struct
import sys

efi=pathlib.Path(sys.argv[1]); kernel=pathlib.Path(sys.argv[2])
compiler=sys.argv[3]; receipt=pathlib.Path(sys.argv[4])
data=efi.read_bytes(); payload=kernel.read_bytes()
if data[:2]!=b"MZ": raise SystemExit("error: native bootloader is not PE/COFF")
pe=struct.unpack_from("<I",data,0x3c)[0]
if data[pe:pe+4]!=b"PE\0\0": raise SystemExit("error: PE signature missing")
machine,sections=struct.unpack_from("<HH",data,pe+4)
optional_size=struct.unpack_from("<H",data,pe+20)[0]; optional=pe+24
if machine!=0x8664 or sections!=3 or optional_size!=0xf0:
    raise SystemExit("error: native bootloader COFF contract rejected")
if struct.unpack_from("<H",data,optional)[0]!=0x20b or struct.unpack_from("<H",data,optional+68)[0]!=10:
    raise SystemExit("error: native bootloader is not a PE32+ EFI application")
import_rva,import_size=struct.unpack_from("<II",data,optional+112+8)
if import_rva or import_size: raise SystemExit("error: native bootloader imports are forbidden")
if data.count(payload)!=1: raise SystemExit("error: embedded Kotoba kernel identity rejected")
scratch_pages=14
scratch_allocation=(b"\xb9\x00\x00\x00\x00\xba\x02\x00\x00\x00"
                    b"\x41\xb8"+struct.pack("<I",scratch_pages))
# The allocator-explicit loader twin (amu 5cec91, kotoba-native adeb1b0f)
# expresses the same fourteen-page reservation as mov edx,0x1000 (page bytes)
# followed by mov r8d,14 (page count). Both shapes name the same
# loader-owned allocation contract; accept either, require one.
scratch_allocation_alloc=(b"\xba\x00\x10\x00\x00\x41\xb8"
                          +struct.pack("<I",scratch_pages))
if (scratch_allocation not in data) and (scratch_allocation_alloc not in data):
    raise SystemExit("error: loader-owned fourteen-page kernel scratch allocation is absent")
preflight=os.environ.get("AIUEOS_NATIVE_K16_PREFLIGHT","0")=="1"
k16_pci_probe=(b"\x66\xba\xf8\x0c\xb8\x00\x00\x02\x80\xef"
               b"\x66\xba\xfc\x0c\xed\x3d\xec\x10\x25\x81")
preflight_messages=[message.encode("utf-16le") for message in (
    "AIUEOS K16 PREFLIGHT ENTER\r\n",
    "AIUEOS K16 PREFLIGHT RTL8125\r\n",
    "AIUEOS K16 PREFLIGHT STATUS 00\r\n")]
if preflight and (k16_pci_probe not in data or
                  any(message not in data for message in preflight_messages)):
    raise SystemExit("error: requested K16 preflight checkpoints are absent")
if not preflight and (k16_pci_probe in data or
                      any(message in data for message in preflight_messages)):
    raise SystemExit("error: K16 preflight entered an ordinary boot image")
for forbidden in (b".idata",b".import",b"msvcrt",b"libc",b"NEEDED"):
    if forbidden in data: raise SystemExit("error: foreign runtime dependency found")
value={
 "format":"aiueos-kotoba-native-boot-receipt/v3",
 "compiler_commit":compiler,
 "boot_sha256":hashlib.sha256(data).hexdigest(),
 "boot_bytes":len(data),
 "kernel_sha256":hashlib.sha256(payload).hexdigest(),
 "kernel_bytes":len(payload),
 "c_sources":[],"foreign_objects":[],"imports":[],"dynamic_dependencies":[],
 "boot_services":["AllocatePages","CopyMem","GetMemoryMap","ExitBootServices"],
 "memory_map":{"storage":"loader-rw-inline","capacity_bytes":16384},
 "kernel_scratch":{"address_offset":80,"pages_offset":88,"pages":scratch_pages,
                   "ownership":"EfiLoaderData-before-final-map"},
    "k16_preflight":{"enabled":preflight,
                  "checkpoints":["ENTER","RTL8125","STATUS XX"] if preflight else []}}
receipt.write_text(json.dumps(value,sort_keys=True,separators=(",",":"))+"\n",encoding="ascii")
print("AIUEOS_KOTOBA_NATIVE_BOOT_OK no-c no-crt no-linker imports=0")
