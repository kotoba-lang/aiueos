#!/usr/bin/env python3
import argparse, hashlib, struct
from pathlib import Path

RSA_MODULUS = int("BB4D67342E4E5B1F376D4B23272DA03A093B75BEC378FB2E6DB4EDEF2CDCD33D141541206CEB83551DDFD5387E2074C7F4AA9895A71CF6AF28E12D2A747CA58844CB1250B6875E85679E545E9B85CADB4214CE2C7BE0A541ABB0FC3D34D386FE2EAC6C1B75C21114EBCC4174BFEF4B23712462BBD7AE03ADF6D0E038953610B13AD4FBAF3B8F5BC478578BF0D0C685D369B55071403F11847290121CB0A934765C27640B2E3FC98B698085531CEAF42FBE8744B791F31645F2B582F51ACC44D348FBC1E989C4EC30F5E3FD4DA858D4897CA6685530327CC25733A3BA2AE20ED7CC10448A53D8C991B500966234AB622647FA05CCF9552DEFE526CAB9B768291B",16)

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
