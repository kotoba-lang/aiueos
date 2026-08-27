#!/usr/bin/env python3
"""Build one read-only qualification USB: UEFI probe -> native core.

The removable-media fallback path starts the probe as BOOTX64.EFI.  The probe
does not call ExitBootServices and performs no writes; after its visible delay
it starts EFI/AIUEOS/BOOTFULL.EFI from the same volume.  BOOTFULL is the normal
AIUEOS loader, paired with the physical-qualification kernel and initramfs.
"""

import argparse
import binascii
import hashlib
import importlib.util
import json
import os
import struct
from datetime import datetime, timezone
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "aiueos_release_image", HERE / "make-release-image.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def make_fat32(probe, loader, kernel, initramfs):
    sector = release.SECTOR
    reserved, fats = 32, 2
    denominator = sector + fats * 4
    fat_sectors = ((release.ESP_SECTORS - reserved + 2) * 4 + denominator - 1) // denominator
    data_start = reserved + fats * fat_sectors
    clusters = release.ESP_SECTORS - data_start
    if clusters < 65525:
        raise ValueError("qualification ESP is too small for FAT32")

    image = bytearray(release.ESP_SECTORS * sector)
    boot = bytearray(sector)
    boot[:3], boot[3:11] = b"\xeb\x58\x90", b"AIUEOSQ "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, sector, 1, reserved, fats, 0, 0,
                     0xF8, 0, 63, 255, release.ESP_FIRST, release.ESP_SECTORS)
    struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
    boot[64], boot[66] = 0x80, 0x29
    struct.pack_into("<I", boot, 67, release.VOLUME_ID)
    boot[71:82], boot[82:90] = b"AIUEOS QUAL", b"FAT32   "
    boot[510:512] = b"\x55\xaa"
    image[:sector] = boot
    image[6 * sector:7 * sector] = boot
    fsinfo = bytearray(sector)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<II", fsinfo, 488, 0xFFFFFFFF, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    image[sector:2 * sector] = fsinfo
    image[7 * sector:8 * sector] = fsinfo

    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
    for cluster in range(2, 6):
        fat[cluster] = 0x0FFFFFFF
    next_cluster = 6

    def allocate(payload):
        nonlocal next_cluster
        count = max(1, (len(payload) + sector - 1) // sector)
        first = next_cluster
        for index in range(count):
            cluster = next_cluster + index
            fat[cluster] = 0x0FFFFFFF if index == count - 1 else cluster + 1
            chunk = payload[index * sector:(index + 1) * sector]
            offset = (data_start + cluster - 2) * sector
            image[offset:offset + len(chunk)] = chunk
        next_cluster += count
        return first

    probe_cluster = allocate(probe)
    loader_cluster = allocate(loader)
    kernel_cluster = allocate(kernel)
    initramfs_cluster = allocate(initramfs)
    root, efi_dir, boot_dir, aiueos_dir = 2, 3, 4, 5
    directories = {
        root: release.dirent("EFI", 0x10, efi_dir),
        efi_dir: (release.dirent(".", 0x10, efi_dir) + release.dirent("..", 0x10, root) +
                  release.dirent("BOOT", 0x10, boot_dir) +
                  release.dirent("AIUEOS", 0x10, aiueos_dir)),
        boot_dir: (release.dirent(".", 0x10, boot_dir) + release.dirent("..", 0x10, efi_dir) +
                   release.dirent("BOOTX64.EFI", 0x20, probe_cluster, len(probe))),
        aiueos_dir: (release.dirent(".", 0x10, aiueos_dir) + release.dirent("..", 0x10, efi_dir) +
                     release.dirent("BOOTFULL.EFI", 0x20, loader_cluster, len(loader)) +
                     release.dirent("KERNEL.ELF", 0x20, kernel_cluster, len(kernel)) +
                     release.dirent("INITRD.IMG", 0x20, initramfs_cluster, len(initramfs))),
    }
    for cluster, payload in directories.items():
        offset = (data_start + cluster - 2) * sector
        image[offset:offset + len(payload)] = payload
    fat_bytes = bytearray(fat_sectors * sector)
    for index, value in enumerate(fat):
        if index * 4 >= len(fat_bytes):
            break
        struct.pack_into("<I", fat_bytes, index * 4, value)
    for copy in range(fats):
        offset = (reserved + copy * fat_sectors) * sector
        image[offset:offset + len(fat_bytes)] = fat_bytes
    return bytes(image)


def make_fat16(probe, loader, kernel, initramfs):
    sector = release.SECTOR
    image = bytearray(release.ISO_BOOT_SECTORS * sector)
    boot = bytearray(sector)
    boot[:3], boot[3:11] = b"\xeb\x3c\x90", b"AIUEOSQ "
    struct.pack_into("<HBHBHHBHHHII", boot, 11, sector, release.FAT16_CLUSTER_SECTORS,
                     release.FAT16_RESERVED, release.FAT16_FATS,
                     release.FAT16_ROOT_SECTORS * sector // 32,
                     release.ISO_BOOT_SECTORS, 0xF8, release.FAT16_FAT_SECTORS,
                     63, 255, 0, 0)
    boot[36], boot[38] = 0x80, 0x29
    struct.pack_into("<I", boot, 39, release.VOLUME_ID)
    boot[43:54], boot[54:62] = b"AIUEOS QUAL", b"FAT16   "
    boot[510:512] = b"\x55\xaa"
    image[:sector] = boot
    fat = [0] * (release.FAT16_CLUSTERS + 2)
    fat[0], fat[1] = 0xFFF8, 0xFFFF
    next_cluster = 2

    def allocate(payload, cluster_count=None):
        nonlocal next_cluster
        cluster_bytes = release.FAT16_CLUSTER_SECTORS * sector
        count = cluster_count or max(1, (len(payload) + cluster_bytes - 1) // cluster_bytes)
        first = next_cluster
        for index in range(count):
            cluster = next_cluster + index
            fat[cluster] = 0xFFFF if index == count - 1 else cluster + 1
            chunk = payload[index * cluster_bytes:(index + 1) * cluster_bytes]
            offset = (release.FAT16_DATA_START +
                      (cluster - 2) * release.FAT16_CLUSTER_SECTORS) * sector
            image[offset:offset + len(chunk)] = chunk
        next_cluster += count
        return first

    efi_dir = allocate(b"", 1)
    boot_dir = allocate(b"", 1)
    aiueos_dir = allocate(b"", 1)
    probe_cluster = allocate(probe)
    loader_cluster = allocate(loader)
    kernel_cluster = allocate(kernel)
    initramfs_cluster = allocate(initramfs)

    def write_dir(cluster, payload):
        offset = (release.FAT16_DATA_START +
                  (cluster - 2) * release.FAT16_CLUSTER_SECTORS) * sector
        image[offset:offset + len(payload)] = payload

    write_dir(efi_dir, release.dirent(".", 0x10, efi_dir) + release.dirent("..", 0x10, 0) +
              release.dirent("BOOT", 0x10, boot_dir) + release.dirent("AIUEOS", 0x10, aiueos_dir))
    write_dir(boot_dir, release.dirent(".", 0x10, boot_dir) + release.dirent("..", 0x10, efi_dir) +
               release.dirent("BOOTX64.EFI", 0x20, probe_cluster, len(probe)))
    write_dir(aiueos_dir, release.dirent(".", 0x10, aiueos_dir) + release.dirent("..", 0x10, efi_dir) +
               release.dirent("BOOTFULL.EFI", 0x20, loader_cluster, len(loader)) +
               release.dirent("KERNEL.ELF", 0x20, kernel_cluster, len(kernel)) +
               release.dirent("INITRD.IMG", 0x20, initramfs_cluster, len(initramfs)))
    root_offset = (release.FAT16_RESERVED +
                   release.FAT16_FATS * release.FAT16_FAT_SECTORS) * sector
    image[root_offset:root_offset + 32] = release.dirent("EFI", 0x10, efi_dir)
    fat_bytes = bytearray(release.FAT16_FAT_SECTORS * sector)
    for index, value in enumerate(fat):
        struct.pack_into("<H", fat_bytes, index * 2, value)
    for copy in range(release.FAT16_FATS):
        offset = (release.FAT16_RESERVED + copy * release.FAT16_FAT_SECTORS) * sector
        image[offset:offset + len(fat_bytes)] = fat_bytes
    return bytes(image)


def fat32_read(disk, path):
    sector = release.SECTOR
    esp = disk[release.ESP_FIRST * sector:(release.ESP_LAST + 1) * sector]
    reserved = struct.unpack_from("<H", esp, 14)[0]
    fats, fat_sectors = esp[16], struct.unpack_from("<I", esp, 36)[0]
    data_start = reserved + fats * fat_sectors
    fat = esp[reserved * sector:(reserved + fat_sectors) * sector]

    def cluster_bytes(cluster):
        return esp[(data_start + cluster - 2) * sector:(data_start + cluster - 1) * sector]

    cluster, size = 2, 0
    for component in path:
        wanted = release.fat_name(component)
        for offset in range(0, sector, 32):
            entry = cluster_bytes(cluster)[offset:offset + 32]
            if entry[:11] == wanted:
                cluster = (struct.unpack_from("<H", entry, 20)[0] << 16 |
                           struct.unpack_from("<H", entry, 26)[0])
                size = struct.unpack_from("<I", entry, 28)[0]
                break
        else:
            raise ValueError("missing primary qualification path: " + component)
    output = bytearray()
    while cluster < 0x0FFFFFF8 and len(output) < size:
        output += cluster_bytes(cluster)
        cluster = struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
    return bytes(output[:size])


def fat16_read(volume, path):
    cluster, size, fat, cluster_bytes = release.fat16_locate(volume, path)
    output = bytearray()
    while cluster < 0xFFF8 and len(output) < size:
        output += cluster_bytes(cluster)
        cluster = struct.unpack_from("<H", fat, cluster * 2)[0]
    return bytes(output[:size])


def verify(path, probe_path, loader_path, kernel_path, initramfs_path):
    expected = {
        "probe": Path(probe_path).read_bytes(),
        "loader": Path(loader_path).read_bytes(),
        "kernel": Path(kernel_path).read_bytes(),
        "initramfs": Path(initramfs_path).read_bytes(),
    }
    release.verify_image(path, probe_path, kernel_path, initramfs_path,
                         recovery_efi=probe_path, recovery_kernel=kernel_path,
                         recovery_initramfs=initramfs_path)
    disk = Path(path).read_bytes()
    primary = {
        "probe": fat32_read(disk, ("EFI", "BOOT", "BOOTX64.EFI")),
        "loader": fat32_read(disk, ("EFI", "AIUEOS", "BOOTFULL.EFI")),
        "kernel": fat32_read(disk, ("EFI", "AIUEOS", "KERNEL.ELF")),
        "initramfs": fat32_read(disk, ("EFI", "AIUEOS", "INITRD.IMG")),
    }
    recovery_volume = disk[release.RECOVERY_FIRST * release.SECTOR:
                           (release.RECOVERY_LAST + 1) * release.SECTOR]
    recovery_files = {
        "probe": fat16_read(recovery_volume, ("EFI", "BOOT", "BOOTX64.EFI")),
        "loader": fat16_read(recovery_volume, ("EFI", "AIUEOS", "BOOTFULL.EFI")),
        "kernel": fat16_read(recovery_volume, ("EFI", "AIUEOS", "KERNEL.ELF")),
        "initramfs": fat16_read(recovery_volume, ("EFI", "AIUEOS", "INITRD.IMG")),
    }
    if primary != expected or recovery_files != expected:
        raise ValueError("qualification boot artifacts do not match both volumes")
    print("AIUEOS_PHYSICAL_QUALIFICATION_USB_OK probe-to-native-core read-only")


def build(args):
    probe = Path(args.probe).read_bytes()
    loader = Path(args.loader).read_bytes()
    kernel = Path(args.kernel).read_bytes()
    initramfs = Path(args.initramfs).read_bytes()
    if probe[:2] != b"MZ" or loader[:2] != b"MZ" or kernel[:4] != b"\x7fELF":
        raise ValueError("invalid qualification artifact magic")
    disk = bytearray(release.DISK_SECTORS * release.SECTOR)
    mbr = bytearray(release.SECTOR)
    mbr[:len(release.BIOS_STUB)] = release.BIOS_STUB
    mbr[446 + 4] = 0xEE
    struct.pack_into("<II", mbr, 446 + 8, 1, release.DISK_SECTORS - 1)
    mbr[510:512] = b"\x55\xaa"
    disk[:release.SECTOR] = mbr
    entries = bytearray(release.GPT_ENTRY_SECTORS * release.SECTOR)
    entries[:16], entries[16:32] = release.ESP_TYPE.bytes_le, release.ESP_GUID.bytes_le
    struct.pack_into("<QQQ", entries, 32, release.ESP_FIRST, release.ESP_LAST, 0)
    name = "aiueos K16 qualify".encode("utf-16le")
    entries[56:56 + len(name)] = name
    entries[128:144], entries[144:160] = release.ESP_TYPE.bytes_le, release.RECOVERY_GUID.bytes_le
    struct.pack_into("<QQQ", entries, 160, release.RECOVERY_FIRST, release.RECOVERY_LAST, 0)
    recovery_name = "aiueos K16 recovery".encode("utf-16le")
    entries[184:184 + len(recovery_name)] = recovery_name
    entries_crc = binascii.crc32(entries) & 0xFFFFFFFF
    disk[2 * release.SECTOR:(2 + release.GPT_ENTRY_SECTORS) * release.SECTOR] = entries
    backup = release.GPT_BACKUP_ENTRIES * release.SECTOR
    disk[backup:backup + len(entries)] = entries
    disk[release.SECTOR:2 * release.SECTOR] = release.gpt_header(
        1, release.DISK_SECTORS - 1, 2, entries_crc)
    disk[-release.SECTOR:] = release.gpt_header(
        release.DISK_SECTORS - 1, 1, release.GPT_BACKUP_ENTRIES, entries_crc)
    disk[release.ESP_FIRST * release.SECTOR:(release.ESP_LAST + 1) * release.SECTOR] = \
        make_fat32(probe, loader, kernel, initramfs)
    disk[release.RECOVERY_FIRST * release.SECTOR:(release.RECOVERY_LAST + 1) * release.SECTOR] = \
        make_fat16(probe, loader, kernel, initramfs)
    Path(args.output).write_bytes(disk)
    verify(args.output, args.probe, args.loader, args.kernel, args.initramfs)
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    receipt = {
        "schema": "aiueos.physical-qualification-usb-receipt.v1",
        "created": datetime.fromtimestamp(epoch, timezone.utc).isoformat().replace("+00:00", "Z"),
        "profile": "k16-native-core-read-only-v1",
        "source": {"commit": os.environ.get("AIUEOS_SOURCE_COMMIT", "UNVERIFIED")},
        "disk": {"bytes": len(disk), "sha256": digest(disk)},
        "boot_order": ["uefi-read-only-hardware-probe", "native-core-read-only"],
        "artifacts": {
            "EFI/BOOT/BOOTX64.EFI": {"bytes": len(probe), "sha256": digest(probe)},
            "EFI/AIUEOS/BOOTFULL.EFI": {"bytes": len(loader), "sha256": digest(loader)},
            "EFI/AIUEOS/KERNEL.ELF": {"bytes": len(kernel), "sha256": digest(kernel)},
            "EFI/AIUEOS/INITRD.IMG": {"bytes": len(initramfs), "sha256": digest(initramfs)},
        },
        "safety": {"internal_disk_writes": False, "block_driver_reached": False,
                   "ssd_install": False},
    }
    Path(args.receipt).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n",
                                  encoding="utf-8")


def parser():
    top = argparse.ArgumentParser()
    sub = top.add_subparsers(dest="command", required=True)
    for command in ("build", "verify"):
        item = sub.add_parser(command)
        item.add_argument("--probe", required=True)
        item.add_argument("--loader", required=True)
        item.add_argument("--kernel", required=True)
        item.add_argument("--initramfs", required=True)
        item.add_argument("--output", required=True)
        if command == "build":
            item.add_argument("--receipt", required=True)
    return top


def main():
    args = parser().parse_args()
    if args.command == "build":
        build(args)
    else:
        verify(args.output, args.probe, args.loader, args.kernel, args.initramfs)


if __name__ == "__main__":
    main()
