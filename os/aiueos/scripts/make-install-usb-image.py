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
LIVE_ESP_GUID = uuid.uuid5(NAMESPACE, "aiueos-live-esp-v1")
BASIC_DATA_TYPE = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
ESP_TYPE = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
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


def make_payload_fat32(total_sectors, files, first_lba=PAYLOAD_FIRST):
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
                     0xF8, 0, 63, 255, first_lba, total_sectors)
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


def make_live_esp(total_sectors, uki):
    """A FAT32 ESP whose whole content is EFI/BOOT/BOOTX64.EFI = the live
    installer UKI. Same construction as the payload volume, plus the two
    directory levels the removable-media fallback path requires."""
    reserved, fats = 32, 2
    spc = 8
    while spc > 1 and (total_sectors - reserved) // spc < 65525 + 8:
        spc //= 2
    denominator = spc * SECTOR + fats * 4
    fat_sectors = ((total_sectors - reserved + 2 * spc) * 4 + denominator - 1) // denominator
    data_start = reserved + fats * fat_sectors
    clusters = (total_sectors - data_start) // spc
    if clusters < 65525:
        raise ValueError("live ESP is too small for FAT32")

    image = bytearray(total_sectors * SECTOR)
    boot = bytearray(SECTOR)
    boot[:3] = b"\xeb\x58\x90"
    boot[3:11] = b"AIUEOS  "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, spc, reserved, fats, 0, 0,
                     0xF8, 0, 63, 255, 2048, total_sectors)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x4C495645)
    boot[71:82] = b"AIUEOS LIVE"
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
    root_cluster, efi_cluster, boot_cluster = 2, 3, 4
    for cluster in (root_cluster, efi_cluster, boot_cluster):
        fat[cluster] = 0x0FFFFFFF
    next_cluster = 5
    cluster_bytes = spc * SECTOR
    count = max(1, (len(uki) + cluster_bytes - 1) // cluster_bytes)
    uki_cluster = next_cluster
    for index in range(count):
        cluster = next_cluster + index
        fat[cluster] = 0x0FFFFFFF if index == count - 1 else cluster + 1
        offset = (data_start + (cluster - 2) * spc) * SECTOR
        chunk = uki[index * cluster_bytes:(index + 1) * cluster_bytes]
        image[offset:offset + len(chunk)] = chunk

    def write_dir(cluster, payload):
        offset = (data_start + (cluster - 2) * spc) * SECTOR
        image[offset:offset + len(payload)] = payload

    write_dir(root_cluster, dirent("EFI", 0x10, efi_cluster))
    write_dir(efi_cluster, dirent(".", 0x10, efi_cluster) + dirent("..", 0x10, 0) +
              dirent("BOOT", 0x10, boot_cluster))
    write_dir(boot_cluster, dirent(".", 0x10, boot_cluster) + dirent("..", 0x10, efi_cluster) +
              dirent("BOOTX64.EFI", 0x20, uki_cluster, len(uki)))

    fat_bytes = bytearray(fat_sectors * SECTOR)
    for index, value in enumerate(fat):
        if index * 4 >= len(fat_bytes):
            break
        struct.pack_into("<I", fat_bytes, index * 4, value)
    for copy in range(fats):
        offset = (reserved + copy * fat_sectors) * SECTOR
        image[offset:offset + len(fat_bytes)] = fat_bytes
    return bytes(image)


def read_live_esp_uki(volume):
    """Independent walk of a make_live_esp volume: return the BOOTX64.EFI
    bytes by following EFI/BOOT through the FAT."""
    if volume[82:90] != b"FAT32   ":
        raise ValueError("live ESP is not FAT32")
    spc = volume[13]
    reserved = struct.unpack_from("<H", volume, 14)[0]
    fats = volume[16]
    fat_sectors = struct.unpack_from("<I", volume, 36)[0]
    data_start = reserved + fats * fat_sectors
    fat = volume[reserved * SECTOR:(reserved + fat_sectors) * SECTOR]
    cluster_bytes = spc * SECTOR

    def cluster_data(cluster):
        offset = (data_start + (cluster - 2) * spc) * SECTOR
        return volume[offset:offset + cluster_bytes]

    def find(directory_cluster, name):
        entries = cluster_data(directory_cluster)
        wanted = fat_name(name)
        for entry_offset in range(0, len(entries), 32):
            entry = entries[entry_offset:entry_offset + 32]
            if entry[0] == 0:
                break
            if entry[:11] == wanted:
                cluster = struct.unpack_from("<H", entry, 20)[0] << 16 | \
                    struct.unpack_from("<H", entry, 26)[0]
                return cluster, struct.unpack_from("<I", entry, 28)[0]
        raise ValueError("live ESP is missing " + name)

    efi_dir, _ = find(2, "EFI")
    boot_dir, _ = find(efi_dir, "BOOT")
    cluster, size = find(boot_dir, "BOOTX64.EFI")
    output = bytearray()
    while cluster < 0x0FFFFFF8 and len(output) < size:
        output += cluster_data(cluster)
        cluster = struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
    return bytes(output[:size])


def release_partition_entries(release):
    """The release image's own GPT entries (ESP at 0, recovery at 1) with
    their first/last LBAs, read from the image rather than re-derived."""
    entries = release[2 * SECTOR:2 * SECTOR + 256]
    esp_first, esp_last = struct.unpack_from("<QQ", entries, 32)[:2]
    rec_first, rec_last = struct.unpack_from("<QQ", entries, 160)[:2]
    return entries, (esp_first, esp_last), (rec_first, rec_last)


def make_bundle_tgz(installer_dir, scripts_dir, intent_bytes, receipt_bytes, node_binary,
                    nbb_dir=None):
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
    live_dir = Path(installer_dir) / "live"
    if live_dir.is_dir():
        for path in sorted(live_dir.iterdir()):
            if path.is_file():
                add(path.name, path.read_bytes(), 0o644)
    for name in ("install-intent.cljs", "install-to-disk.cljs"):
        add(name, (Path(scripts_dir) / name).read_bytes(), 0o644)
    add("install-intent.json", intent_bytes, 0o644)
    add("release-receipt.json", receipt_bytes, 0o644)
    if node_binary:
        add("node-linux-x64", Path(node_binary).read_bytes(), 0o755)
    if nbb_dir:
        base = Path(nbb_dir)
        for path in sorted(base.rglob("*")):
            if path.is_file() and ".bin" not in path.parts:
                add("nbb-bundle/node_modules/" + path.relative_to(base).as_posix(),
                    path.read_bytes(), 0o644)
        # PATH shims for the live environment: install-to-disk.cljs spawns
        # `nbb` and `node` by name, and the tmpfs the bundle lands in is the
        # only writable, executable place on the box.
        add("bin/node", b'#!/bin/sh\nDIR=$(dirname "$(readlink -f "$0")")/..\n'
                        b'exec "$DIR/node-linux-x64" "$@"\n', 0o755)
        add("bin/nbb", b'#!/bin/sh\nDIR=$(dirname "$(readlink -f "$0")")/..\n'
                       b'exec "$DIR/node-linux-x64" "$DIR/nbb-bundle/node_modules/nbb/cli.js" "$@"\n',
            0o755)

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
                             release_receipt_bytes, args.node_binary, args.nbb_dir)
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

    uki = Path(args.live_uki).read_bytes() if args.live_uki else None
    if uki is not None:
        # Live-installer layout: p1 boots the installer UKI, p2 keeps the
        # release recovery volume (aiueos itself, as the independent
        # fallback), p3 carries the payload. LBAs are fresh; only the
        # recovery *bytes* and its type/GUID/name are the release image's.
        _, _, (rec_first, rec_last) = release_partition_entries(release)
        rec_sectors = rec_last - rec_first + 1
        esp_sectors = max(((len(uki) * 2) // SECTOR // ALIGN + 2) * ALIGN, 68 * 1024 * 2)
        esp_first = 2048
        p2_first = ((esp_first + esp_sectors + ALIGN - 1) // ALIGN) * ALIGN
        payload_first = ((p2_first + rec_sectors + ALIGN - 1) // ALIGN) * ALIGN
    else:
        payload_first = PAYLOAD_FIRST
    payload = make_payload_fat32(payload_sectors, files, payload_first)

    payload_last = payload_first + payload_sectors - 1
    total = payload_last + 1 + GPT_ENTRY_SECTORS + 1
    total = ((total + ALIGN - 1) // ALIGN) * ALIGN

    disk = bytearray(total * SECTOR)
    mbr = bytearray(release[:SECTOR])
    struct.pack_into("<II", mbr, 446 + 8, 1, total - 1)
    disk[:SECTOR] = mbr

    entries = bytearray(GPT_ENTRY_SECTORS * SECTOR)
    if uki is not None:
        esp = make_live_esp(esp_sectors, uki)
        name = "aiueos live esp".encode("utf-16le")
        entries[:16] = ESP_TYPE.bytes_le
        entries[16:32] = LIVE_ESP_GUID.bytes_le
        struct.pack_into("<QQQ", entries, 32, esp_first, esp_first + esp_sectors - 1, 0)
        entries[56:56 + len(name)] = name
        entries[128:256] = release[2 * SECTOR + 128:2 * SECTOR + 256]
        struct.pack_into("<QQ", entries, 160, p2_first, p2_first + rec_sectors - 1)
        disk[esp_first * SECTOR:(esp_first + esp_sectors) * SECTOR] = esp
        disk[p2_first * SECTOR:(p2_first + rec_sectors) * SECTOR] = \
            release[rec_first * SECTOR:(rec_last + 1) * SECTOR]
    else:
        # ESP and recovery entries verbatim from the release image's own GPT:
        # same type GUIDs, partition GUIDs, LBAs and names, so the boot path
        # the usb-boot contract proved is untouched.
        entries[:256] = release[2 * SECTOR:2 * SECTOR + 256]
        esp_first = 2048
        release_backup_lba = RELEASE_SECTORS - 1 - GPT_ENTRY_SECTORS
        disk[esp_first * SECTOR:release_backup_lba * SECTOR] = \
            release[esp_first * SECTOR:release_backup_lba * SECTOR]
    name = "aiueos install payload".encode("utf-16le")
    entries[256:272] = BASIC_DATA_TYPE.bytes_le
    entries[272:288] = PAYLOAD_GUID.bytes_le
    struct.pack_into("<QQQ", entries, 288, payload_first, payload_last, 0)
    entries[312:312 + len(name)] = name
    entries_crc = binascii.crc32(entries) & 0xFFFFFFFF

    disk[2 * SECTOR:(2 + GPT_ENTRY_SECTORS) * SECTOR] = entries
    disk[SECTOR:2 * SECTOR] = gpt_header(1, total - 1, 2, entries_crc, total)
    backup_entries_lba = total - 1 - GPT_ENTRY_SECTORS
    disk[backup_entries_lba * SECTOR:(backup_entries_lba + GPT_ENTRY_SECTORS) * SECTOR] = entries
    disk[-SECTOR:] = gpt_header(total - 1, 1, backup_entries_lba, entries_crc, total)
    disk[payload_first * SECTOR:(payload_last + 1) * SECTOR] = payload

    Path(args.output).write_bytes(disk)
    verify_image(args.output, args.release_image)

    receipt = {
        "schema": "aiueos.install-usb-receipt.v1",
        "created": datetime.fromtimestamp(EPOCH, timezone.utc).isoformat().replace("+00:00", "Z"),
        "boot": ({"mode": "live-installer",
                  "espFirstLba": esp_first, "espSectors": esp_sectors,
                  "uki": {"bytes": len(uki), "sha256": sha256_bytes(uki)}}
                 if uki is not None else {"mode": "aiueos-direct"}),
        "disk": {"bytes": len(disk), "sha256": sha256_bytes(bytes(disk))},
        "release": {"receiptSha256": sha256_bytes(release_receipt_bytes),
                    "diskSha256": receipt_json["disk"]["sha256"]},
        "intent": {"sha256": sha256_bytes(intent_bytes),
                   "hostname": intent.get("hostname"),
                   "mode": intent.get("mode")},
        "payload": {"firstLba": payload_first, "lastLba": payload_last,
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
    live = entries[16:32] == LIVE_ESP_GUID.bytes_le
    if live:
        if entries[:16] != ESP_TYPE.bytes_le:
            raise ValueError("live ESP entry has the wrong type GUID")
        esp_first, esp_last = struct.unpack_from("<QQ", entries, 32)[:2]
        uki = read_live_esp_uki(disk[esp_first * SECTOR:(esp_last + 1) * SECTOR])
        if uki[:2] != b"MZ" or b".linux" not in uki[:4096] or b".initrd" not in uki[:4096]:
            raise ValueError("live ESP BOOTX64.EFI is not a UKI")
        rel_entries, _, (rec_first, rec_last) = release_partition_entries(release)
        if entries[128:160] != rel_entries[128:160] or \
                entries[184:256] != rel_entries[184:256]:
            raise ValueError("recovery entry type/GUID/name differ from the release image")
        p2_first, p2_last = struct.unpack_from("<QQ", entries, 160)[:2]
        if (disk[p2_first * SECTOR:(p2_last + 1) * SECTOR]
                != release[rec_first * SECTOR:(rec_last + 1) * SECTOR]):
            raise ValueError("recovery bytes differ from the release image")
    else:
        if entries[:256] != release[2 * SECTOR:2 * SECTOR + 256]:
            raise ValueError("ESP/recovery entries differ from the release image")
    if entries[256:272] != BASIC_DATA_TYPE.bytes_le or entries[272:288] != PAYLOAD_GUID.bytes_le:
        raise ValueError("invalid payload partition entry")
    payload_first, payload_last = struct.unpack_from("<QQ", entries, 288)[:2]
    if payload_first % ALIGN:
        raise ValueError("payload partition is not 1 MiB aligned")
    if not live and payload_first != PAYLOAD_FIRST:
        raise ValueError("payload partition does not start at the release boundary")
    backup_entries_lba = total - 1 - GPT_ENTRY_SECTORS
    if disk[backup_entries_lba * SECTOR:(backup_entries_lba + GPT_ENTRY_SECTORS) * SECTOR] != entries:
        raise ValueError("backup GPT entry array differs")
    backup = bytearray(disk[-SECTOR:][:92])
    stored = struct.unpack_from("<I", backup, 16)[0]
    struct.pack_into("<I", backup, 16, 0)
    if disk[-SECTOR:][:8] != b"EFI PART" or binascii.crc32(backup) & 0xFFFFFFFF != stored:
        raise ValueError("invalid backup GPT header CRC")

    if not live:
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
    print("AIUEOS_INSTALL_USB_IMAGE_OK mode=%s bytes=%d files=%d"
          % ("live-installer" if live else "aiueos-direct", len(disk), len(files)))


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("--release-image", required=True)
    b.add_argument("--release-receipt", required=True)
    b.add_argument("--intent", required=True)
    b.add_argument("--installer-dir", required=True)
    b.add_argument("--node-binary")
    b.add_argument("--nbb-dir")
    b.add_argument("--live-uki")
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
