# I3 partial evidence — second display (scanout 2), 2026-10-26

Run: AIUEOS_GUEST_SCANOUT_TWO=1 AIUEOS_GUEST_BROWSER=1 AIUEOS_SMOKE_FINAL_PROBE=0
AIUEOS_QEMU_DISPLAY=dbus (forced by the mode), reference VM.

## Guest markers (kernel-serial.log, copied alongside)

    AIUEOS_GUEST_SCANOUT_TWO_OK scanouts=2 resource-0=1 resource-1=2 kotoba-n=2
    AIUEOS_GUEST_GPU_TWO_OK

zero AIUEOS_EVIDENCE_STOP, zero AIUEOS_VIRTIO_GPU_FAIL.

## Host readback of both heads (DBus, org.qemu.Display1.Console GetAll)

    Console_0  Label=virtio-vga.0  Head=0  Width=1280 Height=800  pci/0000/06.0
    Console_1  Label=virtio-vga.1  Head=1  Width=32   Height=32   pci/0000/06.0

Head 1 reports 32x32 because that is the resource the guest bound to scanout 1
(VIRTIO_GPU_2D_W/H = 32x32, the second 2D resource of ADR-0095) — i.e. the
second display is enabled and showing guest content, not a disabled output.

## What is still missing for the I3 row (not flipped)

- the SCALE half in the same run (the ui-scale change is measured elsewhere —
  ADR-0261 / browser-desktop-ui-scale / C8's scale setting — but not in this
  receipt's run)
- an image readback of head 1 (the DBus console exposes Width/Height, not the
  framebuffer bytes, through the properties I could read here; QMP screendump
  needs a device id and the virtio-vga device is created with id "")
- a receipt.edn under receipts/ binding all of it to one commit

This directory is evidence, not a receipt: the I3 row stays :unachieved.
