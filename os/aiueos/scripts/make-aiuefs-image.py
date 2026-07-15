#!/usr/bin/env python3
import argparse, hashlib, struct
from pathlib import Path

RSA_MODULUS = int("C0FBFBDA54C6B7DD1DC4294335EF742A7217963E3E06089F40F7D9AE25D186A43C07B911D7FC51D8E60FD7D0C3F135187E8DDC182E0F62C5B42DD4AC953ADF3E872AF12E31F274DB4C6402BC06A8E5477702744BC39D48DD520008EEDF54CF400E769AC1C8C3D7D5A9CCF43E8635E5DA8FFD2475EE56E64A0A9263067E6CFEF0DAE01B1D7F9CAB4ECF254FAD9DD32D1A0B8CA470EDD677D27E81D98599D66792E6444C09E3D59A4106AEF023FE05876A948CDDA86C9C1C009E7F3C51B87F0C7FEAC2E954775FC0EE5806A1D5AEA1AABF1939F3084F7C9FE6802F9EE005392225F1655F8AE9E97F86B2A36C745E8A8C59BFAC2869BC25D8046D8D34A0CB97853D",16)

def fnv(data):
    value=2166136261
    for byte in data: value=((value^byte)*16777619)&0xffffffff
    return value

def verify_signature(data,signature):
    if len(signature)!=256: return False
    digest=hashlib.sha256(data).digest()
    info=bytes.fromhex("3031300d060960864801650304020105000420")+digest
    wanted=b"\0\1"+b"\xff"*(256-len(info)-3)+b"\0"+info
    actual=pow(int.from_bytes(signature,"big"),65537,RSA_MODULUS).to_bytes(256,"big")
    return actual==wanted

parser=argparse.ArgumentParser()
parser.add_argument("--entry",action="append",required=True,metavar="ID,ELF,SIGNATURE")
parser.add_argument("--catalog-signature",required=True)
parser.add_argument("--output",required=True)
args=parser.parse_args()
if not 1<=len(args.entry)<=4: raise SystemExit("aiuefs catalog supports one to four apps")
apps=[]; next_sector=4
for specification in args.entry:
    parts=specification.split(",",2)
    if len(parts)!=3: raise SystemExit("entry must be ID,ELF,SIGNATURE")
    identity=parts[0].encode("ascii"); data=Path(parts[1]).read_bytes(); signature=Path(parts[2]).read_bytes()
    if not identity or len(identity)>15 or b"\0" in identity: raise SystemExit("app id must be 1..15 ASCII bytes")
    if not data or len(data)>12288 or not verify_signature(data,signature): raise SystemExit("app extent or RSA signature is invalid")
    data_sector=next_sector; signature_sector=data_sector+(len(data)+511)//512
    next_sector=signature_sector+1
    apps.append((identity,data,signature,data_sector,signature_sector))
catalog_sector=next_sector
entries=b"".join(struct.pack("<16sII32sII",identity,data_sector,len(data),hashlib.sha256(data).digest(),signature_sector,1)
                 for identity,data,_,data_sector,signature_sector in apps)
catalog=struct.pack("<8sII",b"AIUCAT1\0",1,len(apps))+entries
catalog_signature=Path(args.catalog_signature).read_bytes()
if len(catalog)>512 or not verify_signature(catalog,catalog_signature): raise SystemExit("catalog RSA signature is invalid")
catalog_signature_sector=catalog_sector+1
root=b"KOTOBASE-ROOT-V1"; image=bytearray(1024*1024)
header=struct.pack("<8s9I32s2I24s",b"AIUEFS1\0",3,108,1+len(apps),0,128,len(root),fnv(root),
                   catalog_sector,len(catalog),hashlib.sha256(catalog).digest(),catalog_signature_sector,1,bytes(24))
image[:len(header)]=header; image[128:128+len(root)]=root
for _,data,signature,data_sector,signature_sector in apps:
    image[data_sector*512:data_sector*512+len(data)]=data
    image[signature_sector*512:signature_sector*512+256]=signature
image[catalog_sector*512:catalog_sector*512+len(catalog)]=catalog
image[catalog_signature_sector*512:catalog_signature_sector*512+256]=catalog_signature
Path(args.output).write_bytes(image)
