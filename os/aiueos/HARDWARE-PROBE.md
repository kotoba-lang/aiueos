# aiueos read-only UEFI hardware probe

This standalone x86_64 UEFI application is a bounded diagnostic, not an aiueos
installer and not evidence that the native kernel supports the machine. It
stays inside UEFI Boot Services, does not call `ExitBootServices`, and contains
no disk-write path. PCI configuration access is read-only and limited to the
first 64 handles exposed by `EFI_PCI_IO_PROTOCOL`; a larger set is reported as
an absent capability rather than traversed.

Build the deterministic GPT/FAT32 removable image:

```sh
./os/aiueos/scripts/build-hw-probe.sh
```

The output is `build/aiueos-hw-probe/aiueos-x86_64-gpt.img`. Its receipt uses
the disk fields consumed by the existing guarded USB writer. Inspect the exact
USB device first, then repeat it explicitly to permit the destructive flash:

```sh
AIUEOS_OUT="$PWD/build/aiueos-hw-probe" nbb os/aiueos/scripts/flash-usb.cljs --device /dev/diskN
AIUEOS_OUT="$PWD/build/aiueos-hw-probe" nbb os/aiueos/scripts/flash-usb.cljs \
  --device /dev/diskN --confirm /dev/diskN
```

Only the USB stick is overwritten by the second command. The probe itself
never writes the USB stick or an internal drive. Boot the PC's UEFI removable
entry with Secure Boot disabled if the unsigned image is rejected. Photograph
the screen while it shows the firmware vendor, GOP geometry, memory summary,
ACPI RSDP status, and bounded PCI identifiers.

Run the structural test and USB/OVMF smoke:

```sh
clojure -M:test -n aiueos.hw-probe-test
./os/aiueos/scripts/smoke-qemu-hw-probe.sh
```

The QEMU result proves only that OVMF booted the removable image and exposed
the expected UEFI protocols. It does not prove boot, input, storage, GPU, or
network operation on a physical Dospara machine.
