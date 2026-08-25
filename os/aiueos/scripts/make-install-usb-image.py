#!/usr/bin/env python3
"""Build and verify the aiueos install-USB GPT image (install-v1.edn, root ADR
adr-2608251418).

The install USB is the release image plus intent plus installer, as ONE disk:

  LBA 0            protective MBR, byte-identical BIOS refusal stub
  LBA 1..33        new primary GPT (three partitions)
  LBA 2048..       ESP + recovery, byte-identical to the release image slices
  LBA 131072..     payload partition (FAT32, Microsoft basic data):
                     RELEASE.IMG  the whole release GPT image
                     RECEIPT.JSN  its build receipt
                     INTENT.JSN   the install intent
                     INSTALL.TGZ  installer bundle (mjs + scripts + runbook)
                     SHA256S.TXT  digests of everything above
                     README.TXT   runbook
  tail             new backup GPT

Because the ESP and recovery slices are byte-identical and keep their LBAs,
GUIDs and names, the stick boots exactly what usb-boot-v1.edn proved -- the
firmware reads partition entries, not the disk size. The payload partition is
Microsoft basic data so any rescue Linux mounts it read-only without tooling.

The receipt closes the I1 digest chain: release build receipt -> release image
-> intent -> payload files -> final USB image. `verify` re-derives all of it
from the image alone, so a stick flashed from a drifted image cannot match.
"""

import argparse
import binascii
import gzip
import hashlib
import io
import json
import os
import struct
import tarfile
import uuid
from datetime import datetime, timezone
from pathlib import Path

SECTOR = 512
ALIGN = 2048  # 1 MiB
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE // SECTOR
RELEASE_SECTORS = 131072  # geometry of the release image this consumes
PAYLOAD_FIRST = RELEASE_SECTORS  # 64 MiB boundary, already 1 MiB aligned
NAMESPACE = uuid.UUID("18b3fb94-8713-54c4-9e3a-f0c78a88d192")
DISK_GUID = uuid.uuid5(NAMESPACE, "aiueos-install-usb-disk-v1")
PAYLOAD_GUID = uuid.uuid5(NAMESPACE, "aiueos-install-payload-v1")
BASIC_DATA_TYPE = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
EPOCH = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))

PAYLOAD_NAMES = ["RELEASE.IMG", "RECEIPT.JSN", "INTENT.JSN",
                 "INSTALL.TGZ", "SHA256S.TXT", "README.TXT"]

README_TEXT = """aiueos install USB payload (install-v1.edn)

This stick boots aiueos directly (the ESP is the release image's, unchanged).
This partition additionally carries everything an INSTALL to an internal disk
needs. Nothing here runs by itself; the target machine is erased only through
the guarded installer, and only for the one disk INTENT.JSN names.

From any Linux environment on the target machine:

  MEDIA=/path-to-this-partition                    # wherever this FAT is mounted
  mkdir -p /tmp/aiueos && cd /tmp/aiueos           # do NOT run from the FAT mount
  tar xzf "$MEDIA"/INSTALL.TGZ && cd aiueos-installer
  sh offline-linux-installer.sh \
    --device /dev/nvmeXn1 \
    --image "$MEDIA"/RELEASE.IMG --receipt ./release-receipt.json
  # That is a DRY RUN: it inspects, refuses, and prints the exact three
  # destructive arguments a real install additionally needs. Read its report
  # before adding them. With nbb available, prefer the intent-checking
  # orchestrator instead:
  #   nbb install-to-disk.cljs --intent ./install-intent.json \
  #     --device /dev/nvmeXn1 --image "$MEDIA"/RELEASE.IMG \
  #     --receipt ./release-receipt.json
All digests are in SHA256S.TXT; verify before trusting anything here.
"""


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def fat_name(name):
    stem, dot, suffix = name.partition(".")
    return (stem.upper().ljust(8) + (suffix.upper() if dot else "").ljust(3)).encode("ascii")


def dirent(name, attr, cluster, size=0):
    entry = bytearray(32)
    entry[:11] = fat_name(name)
    entry[11] = attr
    struct.pack_into("<H", entry, 16, 0x0021)  # fixed 1980-01-01, as the release ESP does
    struct.pack_into("<H", entry, 18, 0x0021)
    struct.pack_into("<H", entry, 24, 0x0021)
    struct.pack_into("<H", entry, 20, cluster >> 16)
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def make_payload_fat32(total_sectors, files):
    """A FAT32 volume with the given {8.3-name: bytes} in the root directory.
    Same construction as the release ESP (make-release-image.py make_fat32),
    generalized to arbitrary root files and an adaptive cluster size so the
    FAT32 floor of 65525 clusters holds for any payload size."""
    reserved, fats = 32, 2
    spc = 8
    while spc > 1 and (total_sectors - reserved) // spc < 65525 + 8:
        spc //= 2
    denominator = spc * SECTOR + fats * 4
    fat_sectors = ((total_sectors - reserved + 2 * spc) * 4 + denominator - 1) // denominator
    data_start = reserved + fats * fat_sectors
    clusters = (total_sectors - data_start) // spc
    if clusters < 65525:
        raise ValueError("payload partition is too small for FAT32")

    image = bytearray(total_sectors * SECTOR)
    boot = bytearray(SECTOR)
    boot[:3] = b"\xeb\x58\x90"
    boot[3:11] = b"AIUEOS  "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, spc, reserved, fats, 0, 0,
                     0xF8, 0, 63, 255, PAYLOAD_FIRST, total_sectors)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x41495553)
    boot[71:82] = b"AIUEOS INST"
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xaa"
    image[:SECTOR] = boot
    image[6 * SECTOR:7 * SECTOR] = boot

    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<II", fsinfo, 488, 0xFFFFFFFF, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    image[SECTOR:2 * SECTOR] = fsinfo
    image[7 * SECTOR:8 * SECTOR] = fsinfo

    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
    fat[2] = 0x0FFFFFFF  # root directory, one cluster
    next_cluster = 3
    cluster_bytes = spc * SECTOR

    def allocate(payload):
        nonlocal next_cluster
        count = max(1, (len(payload) + cluster_bytes - 1) // cluster_bytes)
        first = next_cluster
        for index in range(count):
            cluster = next_cluster + index
            fat[cluster] = 0x0FFFFFFF if index == count - 1 else cluster + 1
            offset = (data_start + (cluster - 2) * spc) * SECTOR
            chunk = payload[index * cluster_bytes:(index + 1) * cluster_bytes]
            image[offset:offset + len(chunk)] = chunk
        next_cluster += count
        return first

    root = bytearray()
    for name in PAYLOAD_NAMES:
        data = files[name]
        root += dirent(name, 0x20, allocate(data), len(data))
    if len(root) > cluster_bytes:
        raise ValueError("root directory exceeds one cluster")
    root_offset = data_start * SECTOR
    image[root_offset:root_offset + len(root)] = root

    fat_bytes = bytearray(fat_sectors * SECTOR)
    for index, value in enumerate(fat):
        if index * 4 >= len(fat_bytes):
            break
        struct.pack_into("<I", fat_bytes, index * 4, value)
    for copy in range(fats):
        offset = (reserved + copy * fat_sectors) * SECTOR
        image[offset:offset + len(fat_bytes)] = fat_bytes
    return bytes(image)


def read_payload_fat32(volume):
    """Return {name: bytes} for the root files of a volume make_payload_fat32
    built. Independent walk (boot-sector parameters, FAT chains), so verify
    does not trust the builder's in-memory state."""
    if volume[82:90] != b"FAT32   " or volume[510:512] != b"\x55\xaa":
        raise ValueError("payload is not the expected FAT32 volume")
    spc = volume[13]
    reserved = struct.unpack_from("<H", volume, 14)[0]
    fats = volume[16]
    fat_sectors = struct.unpack_from("<I", volume, 36)[0]
    data_start = reserved + fats * fat_sectors
    fat = volume[reserved * SECTOR:(reserved + fat_sectors) * SECTOR]
    cluster_bytes = spc * SECTOR

    def read_chain(first, size):
        output = bytearray()
        cluster = first
        while cluster < 0x0FFFFFF8 and len(output) < size:
            offset = (data_start + (cluster - 2) * spc) * SECTOR
            output += volume[offset:offset + cluster_bytes]
            cluster = struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
        return bytes(output[:size])

    files = {}
    # Follow the whole root-directory chain, and tolerate what one mount on a
    # host OS leaves behind: long-name entries (attr 0x0F), deleted markers
    # (0xE5), volume labels, and non-ASCII names from indexer droppings. The
    # named payload files are what this walk answers for; foreign entries are
    # skipped, never fatal -- a stick that has been mounted once must still
    # verify.
    root = read_chain(2, 1 << 30)
    for entry_offset in range(0, len(root), 32):
        entry = root[entry_offset:entry_offset + 32]
        if entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
            continue
        try:
            name = entry[:8].decode("ascii").rstrip()
            suffix = entry[8:11].decode("ascii").rstrip()
        except UnicodeDecodeError:
            continue
        cluster = struct.unpack_from("<H", entry, 20)[0] << 16 | struct.unpack_from("<H", entry, 26)[0]
        size = struct.unpack_from("<I", entry, 28)[0]
        files[name + ("." + suffix if suffix else "")] = read_chain(cluster, size)
    return files


def make_bundle_tgz(installer_dir, scripts_dir, intent_bytes, receipt_bytes, node_binary):
    """Deterministic gzip'd tar of the installer bundle. Determinism is what
    lets the receipt digest mean anything: same inputs, same bytes."""
    buffer = io.BytesIO()
    entries = []

    def add(name, data, mode):
        entries.append(("aiueos-installer/" + name, data, mode))

    for path in sorted(Path(installer_dir).iterdir()):
        if path.is_file():
            mode = 0o755 if path.suffix == ".sh" else 0o644
            add(path.name, path.read_bytes(), mode)
    for name in ("install-intent.cljs", "install-to-disk.cljs"):
        add(name, (Path(scripts_dir) / name).read_bytes(), 0o644)
    add("install-intent.json", intent_bytes, 0o644)
    add("release-receipt.json", receipt_bytes, 0o644)
    if node_binary:
        add("node-linux-x64", Path(node_binary).read_bytes(), 0o755)

    with gzip.GzipFile(fileobj=buffer, mode="wb", mtime=EPOCH) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            for name, data, mode in sorted(entries):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = mode
                info.mtime = EPOCH
                info.uname = info.gname = ""
                tar.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def gpt_header(current, backup, entries_lba, entries_crc, total_sectors):
    header = bytearray(SECTOR)
    last_usable = total_sectors - GPT_ENTRY_SECTORS - 2
    struct.pack_into("<8sIIIIQQQQ16sQIII", header, 0, b"EFI PART", 0x00010000, 92, 0, 0,
                     current, backup, 34, last_usable, DISK_GUID.bytes_le,
                     entries_lba, GPT_ENTRY_COUNT, GPT_ENTRY_SIZE, entries_crc)
    struct.pack_into("<I", header, 16, binascii.crc32(header[:92]) & 0xFFFFFFFF)
    return header


def build(args):
    release = Path(args.release_image).read_bytes()
    release_receipt_bytes = Path(args.release_receipt).read_bytes()
    receipt_json = json.loads(release_receipt_bytes)
    if len(release) != RELEASE_SECTORS * SECTOR:
        raise ValueError("release image is not the expected 64 MiB geometry")
    if (len(release) != receipt_json["disk"]["bytes"]
            or sha256_bytes(release) != receipt_json["disk"]["sha256"]):
        raise ValueError("release image does not match its build receipt")
    intent_bytes = Path(args.intent).read_bytes()
    intent = json.loads(intent_bytes)
    if intent.get("schema") != "aiueos.install-intent.v1":
        raise ValueError("intent is not aiueos.install-intent.v1")
    if intent["release"]["disk"]["sha256"] != receipt_json["disk"]["sha256"]:
        raise ValueError("intent names a different release image digest")

    scripts_dir = Path(__file__).resolve().parent
    bundle = make_bundle_tgz(args.installer_dir, scripts_dir, intent_bytes,
                             release_receipt_bytes, args.node_binary)
    files = {
        "RELEASE.IMG": release,
        "RECEIPT.JSN": release_receipt_bytes,
        "INTENT.JSN": intent_bytes,
        "INSTALL.TGZ": bundle,
    }
    sha_lines = "".join("%s  %s\n" % (sha256_bytes(files[n]), n)
                        for n in ["RELEASE.IMG", "RECEIPT.JSN", "INTENT.JSN", "INSTALL.TGZ"])
    files["SHA256S.TXT"] = sha_lines.encode("ascii")
    files["README.TXT"] = README_TEXT.encode("utf-8")

    content = sum(len(v) for v in files.values())
    payload_sectors = ((content + content // 4) // SECTOR // ALIGN + 9) * ALIGN
    payload_sectors = max(payload_sectors, 68 * 1024 * 2)  # FAT32 floor with margin
    payload = make_payload_fat32(payload_sectors, files)

    payload_last = PAYLOAD_FIRST + payload_sectors - 1
    total = payload_last + 1 + GPT_ENTRY_SECTORS + 1
    total = ((total + ALIGN - 1) // ALIGN) * ALIGN

    disk = bytearray(total * SECTOR)
    mbr = bytearray(release[:SECTOR])
    struct.pack_into("<II", mbr, 446 + 8, 1, total - 1)
    disk[:SECTOR] = mbr

    entries = bytearray(GPT_ENTRY_SECTORS * SECTOR)
    # ESP and recovery entries verbatim from the release image's own GPT:
    # same type GUIDs, partition GUIDs, LBAs and names, so the boot path the
    # usb-boot contract proved is untouched.
    entries[:256] = release[2 * SECTOR:2 * SECTOR + 256]
    name = "aiueos install payload".encode("utf-16le")
    entries[256:272] = BASIC_DATA_TYPE.bytes_le
    entries[272:288] = PAYLOAD_GUID.bytes_le
    struct.pack_into("<QQQ", entries, 288, PAYLOAD_FIRST, payload_last, 0)
    entries[312:312 + len(name)] = name
    entries_crc = binascii.crc32(entries) & 0xFFFFFFFF

    disk[2 * SECTOR:(2 + GPT_ENTRY_SECTORS) * SECTOR] = entries
    disk[SECTOR:2 * SECTOR] = gpt_header(1, total - 1, 2, entries_crc, total)
    backup_entries_lba = total - 1 - GPT_ENTRY_SECTORS
    disk[backup_entries_lba * SECTOR:(backup_entries_lba + GPT_ENTRY_SECTORS) * SECTOR] = entries
    disk[-SECTOR:] = gpt_header(total - 1, 1, backup_entries_lba, entries_crc, total)

    # ESP + recovery slices byte-identical; the release image's own backup GPT
    # (its last 33 sectors) is NOT copied -- a second EFI PART header mid-disk
    # is exactly the kind of ambiguity a recovery tool trips over.
    esp_first = 2048
    release_backup_lba = RELEASE_SECTORS - 1 - GPT_ENTRY_SECTORS
    disk[esp_first * SECTOR:release_backup_lba * SECTOR] = \
        release[esp_first * SECTOR:release_backup_lba * SECTOR]
    disk[PAYLOAD_FIRST * SECTOR:(payload_last + 1) * SECTOR] = payload

    Path(args.output).write_bytes(disk)
    verify_image(args.output, args.release_image)

    receipt = {
        "schema": "aiueos.install-usb-receipt.v1",
        "created": datetime.fromtimestamp(EPOCH, timezone.utc).isoformat().replace("+00:00", "Z"),
        "disk": {"bytes": len(disk), "sha256": sha256_bytes(bytes(disk))},
        "release": {"receiptSha256": sha256_bytes(release_receipt_bytes),
                    "diskSha256": receipt_json["disk"]["sha256"]},
        "intent": {"sha256": sha256_bytes(intent_bytes),
                   "hostname": intent.get("hostname"),
                   "mode": intent.get("mode")},
        "payload": {"firstLba": PAYLOAD_FIRST, "lastLba": payload_last,
                    "type": str(BASIC_DATA_TYPE), "guid": str(PAYLOAD_GUID),
                    "files": {n: {"bytes": len(files[n]), "sha256": sha256_bytes(files[n])}
                              for n in PAYLOAD_NAMES}},
    }
    Path(args.receipt).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n",
                                  encoding="utf-8")
    print(args.output)


def verify_image(path, release_image):
    disk = Path(path).read_bytes()
    release = Path(release_image).read_bytes()
    total = len(disk) // SECTOR
    if len(disk) % SECTOR:
        raise ValueError("image is not sector-aligned")
    header = disk[SECTOR:2 * SECTOR]
    if header[:8] != b"EFI PART":
        raise ValueError("missing GPT header")
    checked = bytearray(header[:92])
    stored = struct.unpack_from("<I", checked, 16)[0]
    struct.pack_into("<I", checked, 16, 0)
    if binascii.crc32(checked) & 0xFFFFFFFF != stored:
        raise ValueError("invalid primary GPT header CRC")
    if header[56:72] != DISK_GUID.bytes_le:
        raise ValueError("disk GUID is not the install-USB GUID")
    entries = disk[2 * SECTOR:(2 + GPT_ENTRY_SECTORS) * SECTOR]
    if binascii.crc32(entries) & 0xFFFFFFFF != struct.unpack_from("<I", header, 88)[0]:
        raise ValueError("invalid GPT entry-array CRC")
    if entries[:256] != release[2 * SECTOR:2 * SECTOR + 256]:
        raise ValueError("ESP/recovery entries differ from the release image")
    if entries[256:272] != BASIC_DATA_TYPE.bytes_le or entries[272:288] != PAYLOAD_GUID.bytes_le:
        raise ValueError("invalid payload partition entry")
    payload_first, payload_last = struct.unpack_from("<QQ", entries, 288)[:2]
    if payload_first != PAYLOAD_FIRST:
        raise ValueError("payload partition does not start at the release boundary")
    backup_entries_lba = total - 1 - GPT_ENTRY_SECTORS
    if disk[backup_entries_lba * SECTOR:(backup_entries_lba + GPT_ENTRY_SECTORS) * SECTOR] != entries:
        raise ValueError("backup GPT entry array differs")
    backup = bytearray(disk[-SECTOR:][:92])
    stored = struct.unpack_from("<I", backup, 16)[0]
    struct.pack_into("<I", backup, 16, 0)
    if disk[-SECTOR:][:8] != b"EFI PART" or binascii.crc32(backup) & 0xFFFFFFFF != stored:
        raise ValueError("invalid backup GPT header CRC")

    esp_first = 2048
    release_backup_lba = RELEASE_SECTORS - 1 - GPT_ENTRY_SECTORS
    if (disk[esp_first * SECTOR:release_backup_lba * SECTOR]
            != release[esp_first * SECTOR:release_backup_lba * SECTOR]):
        raise ValueError("ESP/recovery bytes differ from the release image")

    payload = disk[payload_first * SECTOR:(payload_last + 1) * SECTOR]
    files = read_payload_fat32(payload)
    for name in PAYLOAD_NAMES:
        if name not in files:
            raise ValueError("payload is missing " + name)
    if files["RELEASE.IMG"] != release:
        raise ValueError("embedded RELEASE.IMG differs from the release image")
    for line in files["SHA256S.TXT"].decode("ascii").splitlines():
        digest, name = line.split("  ", 1)
        if sha256_bytes(files[name]) != digest:
            raise ValueError("payload digest mismatch for " + name)
    intent = json.loads(files["INTENT.JSN"])
    release_receipt = json.loads(files["RECEIPT.JSN"])
    if intent["release"]["disk"]["sha256"] != release_receipt["disk"]["sha256"]:
        raise ValueError("embedded intent names a different release digest")
    if sha256_bytes(files["RELEASE.IMG"]) != release_receipt["disk"]["sha256"]:
        raise ValueError("embedded RELEASE.IMG does not match embedded receipt")
    print("AIUEOS_INSTALL_USB_IMAGE_OK bytes=%d files=%d" % (len(disk), len(files)))


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("--release-image", required=True)
    b.add_argument("--release-receipt", required=True)
    b.add_argument("--intent", required=True)
    b.add_argument("--installer-dir", required=True)
    b.add_argument("--node-binary")
    b.add_argument("--output", required=True)
    b.add_argument("--receipt", required=True)
    v = sub.add_parser("verify")
    v.add_argument("--image", required=True)
    v.add_argument("--release-image", required=True)
    args = parser.parse_args()
    if args.command == "build":
        build(args)
    else:
        verify_image(args.image, args.release_image)


if __name__ == "__main__":
    main()
