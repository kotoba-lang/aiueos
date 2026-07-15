#!/usr/bin/env python3
import argparse, hashlib, struct
from pathlib import Path

RSA_MODULUS = int("A02BD6466AD0AA5B296CC8390E2990BE9D867E75D7C0157127C234BE62BC2D1DDB8702534227F0E5369E1AA496DAF26EB86F69A98078D31EC1B37269FA2BD6318F4F07E7C413A9D9E394F465C3916C643F5444FCF99208AF304E99370CED30DCA70F8C6468B15A15DAC4141A99ED5A1AB99618048B969BFDB512432B48D4EBE8B55FB517DDAB5954C665CF66846050555FA3BADA1E4B72D8B96090CFD27EC39761E38E153F45761EAAB999422DFD475FA4E7846F15B39D4B361FAC5CB073BA38C0082E36BD0254CE49476CD187B1AB881CB90736F43D733D96DCA78F39379D40C801E27601CFDC284E6C7DF95F757F220CF0D408B510A4F558C0DCA757CEEBF5",16)

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
