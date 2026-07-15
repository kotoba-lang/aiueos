#!/usr/bin/env python3
import argparse
import hashlib
import struct
from pathlib import Path

RSA_MODULUS = int("AF94E7A5C5D01E285E6C4BEA6B681866935EFA06033705BE5CB0FA761D443AB101C616A7935D4C90E627C673F60A0C0550C4A5544BCB4B410A61DBA9603F6770A51A9883D68B08A61915672BA857C03E484A2379988181CFF63853BAE6ECBF24EBBAAEE9BC87EB892B721CC118E88F1A0C948E7B8130B8207C7AD24DA73ABCDBE7DABB23ADA35D59BDFCFB9A47C7364C2990E1BDAC6C1BD4F126EF7BF80DD8E5BDA1AF0E8CCC61945E27C225D3D8399E7AF0B6145A4A67F8DB346EDCC66D9AE59EA2F23168C0782C78C9DC0E54DE8EC3C009196BD948A3541AE7787C5A36CFE79611BC902C841577E9D1E4B66378E07D0997ACA6AF6354346302E84820EC3FA3", 16)

def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value

parser = argparse.ArgumentParser()
parser.add_argument("--app", required=True)
parser.add_argument("--signature", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
app = Path(args.app).read_bytes()
signature = Path(args.signature).read_bytes()
if not app or len(app) > 12288:
    raise SystemExit("Kotoba application exceeds the bounded aiuefs extent")
root = b"KOTOBASE-ROOT-V1"
image = bytearray(1024 * 1024)
if len(signature) != 256:
    raise SystemExit("Kotoba application signature must be RSA-2048")
digest = hashlib.sha256(app).digest()
digest_info = bytes.fromhex("3031300d060960864801650304020105000420") + digest
expected_message = b"\x00\x01" + b"\xff" * (256 - len(digest_info) - 3) + b"\x00" + digest_info
actual_message = pow(int.from_bytes(signature, "big"), 65537, RSA_MODULUS).to_bytes(256, "big")
if actual_message != expected_message:
    raise SystemExit("Kotoba application RSA signature does not verify")
signature_sector = 4 + (len(app) + 511) // 512
header = struct.pack("<8s9I32s2I24s", b"AIUEFS1\0", 2, 108, 2, 0,
                     128, len(root), fnv(root), 4, len(app),
                     digest,
                     signature_sector, 1, bytes(24))
image[:len(header)] = header
image[128:128 + len(root)] = root
image[4 * 512:4 * 512 + len(app)] = app
image[signature_sector * 512:signature_sector * 512 + len(signature)] = signature
Path(args.output).write_bytes(image)
