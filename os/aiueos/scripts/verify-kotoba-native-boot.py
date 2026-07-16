#!/usr/bin/env python3
import hashlib
import json
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
for forbidden in (b".idata",b".import",b"msvcrt",b"libc",b"NEEDED"):
    if forbidden in data: raise SystemExit("error: foreign runtime dependency found")
value={
 "format":"aiueos-kotoba-native-boot-receipt/v1",
 "compiler_commit":compiler,
 "boot_sha256":hashlib.sha256(data).hexdigest(),
 "boot_bytes":len(data),
 "kernel_sha256":hashlib.sha256(payload).hexdigest(),
 "kernel_bytes":len(payload),
 "c_sources":[],"foreign_objects":[],"imports":[],"dynamic_dependencies":[],
 "boot_services":["AllocatePages","CopyMem","AllocatePool","GetMemoryMap","ExitBootServices"]}
receipt.write_text(json.dumps(value,sort_keys=True,separators=(",",":"))+"\n",encoding="ascii")
print("AIUEOS_KOTOBA_NATIVE_BOOT_OK no-c no-crt no-linker imports=0")
