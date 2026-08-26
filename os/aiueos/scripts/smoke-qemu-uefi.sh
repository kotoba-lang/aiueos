#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
aiueos="$repo/os/aiueos"
out=${AIUEOS_OUT:-"$repo/build/aiueos"}
log="$out/uefi-debug.log"
serial_log="$out/kernel-serial.log"
blk_image="$out/virtio-blk-smoke.img"
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}

if [ "${AIUEOS_PLC_RT_SMOKE:-0}" = 1 ]; then
  AIUEOS_PLC_ELF=${AIUEOS_PLC_ELF:-"$repo/build/plc-motor/program.elf"}
  AIUEOS_PLC_RECEIPT=${AIUEOS_PLC_RECEIPT:-"$repo/build/plc-motor/program-receipt.json"}
  export AIUEOS_PLC_ELF AIUEOS_PLC_RECEIPT
fi

if [ "${AIUEOS_GUEST_INPUT:-0}" = 1 ]; then
  AIUEOS_CATALOG_POLICY_SELFTEST=1 \
    "$aiueos/scripts/build-uefi.sh" >/dev/null
else
  AIUEOS_INPUT_SMOKE_SYNTHETIC=1 AIUEOS_CATALOG_POLICY_SELFTEST=1 \
    "$aiueos/scripts/build-uefi.sh" >/dev/null
fi
if [ "${AIUEOS_CORRUPT_KERNEL:-0}" = 1 ]; then
  python3 - "$out/esp/EFI/AIUEOS/KERNEL.ELF" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
data[-1] ^= 0x01
path.write_bytes(data)
PY
fi
if [ "${AIUEOS_CORRUPT_INITRAMFS:-0}" = 1 ]; then
  python3 - "$out/esp/EFI/AIUEOS/INITRD.IMG" <<'PYC'
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
data[-1] ^= 0x01
path.write_bytes(data)
PYC
fi
command -v "$qemu" >/dev/null 2>&1 || {
  echo "error: qemu-system-x86_64 is required" >&2
  exit 1
}

if [ -z "${OVMF_CODE:-}" ]; then
  for candidate in \
    /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
    /opt/homebrew/Cellar/qemu/*/share/qemu/edk2-x86_64-code.fd \
    /usr/share/OVMF/OVMF_CODE_4M.fd \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/edk2/x64/OVMF_CODE.fd; do
    if [ -f "$candidate" ]; then OVMF_CODE=$candidate; break; fi
  done
fi
[ -f "${OVMF_CODE:-}" ] || {
  echo "error: OVMF firmware not found; set OVMF_CODE" >&2
  exit 1
}

rm -f "$log" "$serial_log"
if [ "${AIUEOS_PRESERVE_BLK_IMAGE:-0}" != 1 ] || [ ! -f "$blk_image" ]; then
python3 "$aiueos/scripts/make-aiuefs-image.py" \
  --entry "app/hello,$aiueos/kotoba/user-smoke.elf,$aiueos/kotoba/user-smoke.sig" \
  --entry "app/worker,$aiueos/kotoba/user-smoke.elf,$aiueos/kotoba/user-smoke.sig" \
  --catalog-signature "$aiueos/kotoba/app-catalog.sig" --output "$blk_image"
fi
if [ "${AIUEOS_CORRUPT_KOTOBA_APP:-0}" = 1 ]; then
python3 - "$blk_image" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes()); b[4*512+64]^=1; p.write_bytes(b)
PY
fi
if [ "${AIUEOS_CORRUPT_KOTOBA_SIGNATURE:-0}" = 1 ]; then
python3 - "$blk_image" <<'PY'
from pathlib import Path
import struct, sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes()); catalog_sector=struct.unpack_from('<I',b,36)[0]
signature_sector=struct.unpack_from('<I',b,catalog_sector*512+16+56)[0]
b[signature_sector*512+17]^=1; p.write_bytes(b)
PY
fi
if [ "${AIUEOS_CORRUPT_KOTOBA_CATALOG:-0}" = 1 ]; then
python3 - "$blk_image" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]);b=bytearray(p.read_bytes());sector=struct.unpack_from('<I',b,36)[0]
b[sector*512+20]^=1;p.write_bytes(b)
PY
fi
# Boot transport (ADR-0019): `disk` attaches the medium as a fixed drive the
# firmware enumerates directly; `usb` attaches it behind an xHCI controller as
# a REMOVABLE USB mass-storage device, which is the path a physical USB stick
# actually takes — the firmware must run its USB stack, enumerate the device,
# and boot the removable-media fallback `\EFI\BOOT\BOOTX64.EFI`. Only the
# transport differs: every other device and every evidence assertion below is
# identical, so the two runs' logs are directly comparable.
boot_transport=${AIUEOS_BOOT_TRANSPORT:-disk}
case "$boot_transport" in
  disk|usb) ;;
  *)
    echo "error: AIUEOS_BOOT_TRANSPORT must be disk or usb: $boot_transport" >&2
    exit 1
    ;;
esac
usb_args=
if [ -n "${AIUEOS_CDROM_IMAGE:-}" ]; then
  [ -f "$AIUEOS_CDROM_IMAGE" ] || {
    echo "error: AIUEOS_CDROM_IMAGE does not exist: $AIUEOS_CDROM_IMAGE" >&2
    exit 1
  }
  [ "$boot_transport" = disk ] || {
    echo "error: AIUEOS_BOOT_TRANSPORT=usb does not apply to an El Torito ISO" >&2
    exit 1
  }
  # El Torito boot from the release ISO; cdrom media is opened read-only.
  boot_drive="format=raw,media=cdrom,file=$AIUEOS_CDROM_IMAGE"
elif [ -n "${AIUEOS_DISK_IMAGE:-}" ]; then
  [ -f "$AIUEOS_DISK_IMAGE" ] || {
    echo "error: AIUEOS_DISK_IMAGE does not exist: $AIUEOS_DISK_IMAGE" >&2
    exit 1
  }
  # OVMF may open the boot medium writable; snapshot keeps the release artifact immutable.
  if [ "$boot_transport" = usb ]; then
    boot_drive="if=none,id=aiueosusb,format=raw,snapshot=on,file=$AIUEOS_DISK_IMAGE"
    usb_args="-device qemu-xhci,id=xhci -device usb-storage,bus=xhci.0,drive=aiueosusb,removable=on"
  else
    boot_drive="format=raw,snapshot=on,file=$AIUEOS_DISK_IMAGE"
  fi
else
  [ "$boot_transport" = disk ] || {
    echo "error: AIUEOS_BOOT_TRANSPORT=usb requires AIUEOS_DISK_IMAGE" >&2
    exit 1
  }
  boot_drive="format=raw,file=fat:rw:$out/esp"
fi
iommu_args=
if [ "${AIUEOS_TEST_DMAR:-0}" = 1 ]; then iommu_args="-device intel-iommu,intremap=on"; fi
# A NIC is attached only when asked for, so every existing gate keeps booting
# the exact machine it booted before. SLIRP ("-netdev user") is a real peer with
# a fixed topology — it answers ARP for 10.0.2.2 — which is what lets the first
# packet aiueos ever sends be checked against an actual reply rather than a
# loopback of itself. No host network access is involved.
net_args=
if [ "${AIUEOS_TEST_NET:-0}" = 1 ]; then
  # `guestfwd` gives the guest a TCP peer without any external network: QEMU
  # accepts a connection to 10.0.2.100:9000 and pipes the stream through
  # `cat`, so it echoes. That is what lets the TCP gate prove a real
  # handshake -- sequence numbers, ACKs and both checksums all have to be
  # right or nothing comes back -- rather than a self-echo the OS produced.
  net_args="-netdev user,id=aiueosnet,guestfwd=tcp:10.0.2.100:9000-cmd:/bin/cat -device virtio-net-pci,netdev=aiueosnet,disable-legacy=on"
  # AIUEOS_SSH_HOSTFWD=<host-port> additionally forwards that host port into the
  # guest's :22, so an external client can reach the SSH listener (ADR-0102).
  # It only ADDS a hostfwd to the same netdev; the guestfwd the other net gates
  # depend on is untouched.
  if [ -n "${AIUEOS_SSH_HOSTFWD:-}" ]; then
    net_args="-netdev user,id=aiueosnet,guestfwd=tcp:10.0.2.100:9000-cmd:/bin/cat,hostfwd=tcp:127.0.0.1:${AIUEOS_SSH_HOSTFWD}-10.0.2.15:22 -device virtio-net-pci,netdev=aiueosnet,disable-legacy=on"
  fi
fi
# A hung guest must fail fast with diagnostics rather than pinning CI until
# the job-level timeout. 124 from timeout(1) is handled below.
qemu_timeout=${AIUEOS_QEMU_TIMEOUT:-600}
# The ring-3 process phase has an occasional lost-wakeup hang on slow TCG
# runners (kotoba-lang/aiueos#108) that clears on a fresh boot. Retry ONLY on
# a timeout (status 124) — every deterministic exit status is a real result
# and is never retried. Each attempt restarts from a pristine data disk so a
# partially-written disk from a hung boot cannot change the retry's outcome.
qemu_attempts=${AIUEOS_QEMU_ATTEMPTS:-3}
# QEMU 10.1 virtio-gpu sets enabled_output_bitmask=1 at realize. Extra
# heads become enabled only when a UI frontend calls ui_info with a
# non-zero size (hw/display/virtio-gpu-base.c). `-display none` never
# does that, so GET_DISPLAY_INFO stays one-scanout. cocoa only ui_info's
# the front window (head 0). dbus SetUIInfo on Console_1 enables head 1.
# Default stays none so gpu/guest-gpu-two do not pop a window.
display_backend=${AIUEOS_QEMU_DISPLAY:-none}
aiueos_dbus_pid=
if [ "${AIUEOS_GUEST_SCANOUT_TWO:-0}" = 1 ]; then
  display_backend=dbus
  command -v gdbus >/dev/null 2>&1 || {
    echo "error: gdbus is required for guest-scanout-two" >&2
    exit 1
  }
  command -v dbus-daemon >/dev/null 2>&1 || {
    echo "error: dbus-daemon is required for guest-scanout-two (brew install dbus)" >&2
    exit 1
  }
  # Own a bus under $out. An inherited DBUS_SESSION_BUS_ADDRESS may
  # point at a dead unix socket (macOS launchd session socket is empty;
  # a previous probe in the same shell leaves a stale path). QEMU then
  # fails with "failed to connect to DBus" before GET_DISPLAY_INFO.
  rm -f "$out/guest-scanout.sock" "$out/dbus.addr" "$out/dbus.pid"
  dbus-daemon --session --fork --nopidfile \
    --address="unix:path=$out/guest-scanout.sock" \
    --print-address=3 --print-pid=4 3>"$out/dbus.addr" 4>"$out/dbus.pid"
  DBUS_SESSION_BUS_ADDRESS=$(cat "$out/dbus.addr")
  export DBUS_SESSION_BUS_ADDRESS
  unset DBUS_LAUNCHD_SESSION_BUS_SOCKET || true
  aiueos_dbus_pid=$(cat "$out/dbus.pid")
fi
if [ "$display_backend" = dbus ]; then
  display_opt="dbus,gl=off"
else
  display_opt="$display_backend"
fi
qmp_path="$out/guest-input.qmp"
qmp_args=""
kbd_args="-device virtio-keyboard-pci,disable-legacy=on"
if [ "${AIUEOS_GUEST_INPUT:-0}" = 1 ]; then
  kbd_args="-device virtio-keyboard-pci,disable-legacy=on,id=kbd0"
  qmp_args="-qmp unix:${qmp_path},server,nowait"
fi
pristine_blk=
if [ -f "$blk_image" ]; then
  pristine_blk="$blk_image.pristine"
  cp "$blk_image" "$pristine_blk"
fi
attempt=1
while :; do
  [ -n "$pristine_blk" ] && cp "$pristine_blk" "$blk_image"
  inject_pid=
  if [ "${AIUEOS_GUEST_INPUT:-0}" = 1 ]; then
    rm -f "$qmp_path"
    AIUEOS_QMP_PATH="$qmp_path" AIUEOS_QMP_LOG="$out/guest-input-qmp.log" python3 - <<'PY' &
import json, os, socket, sys, time
path = os.environ["AIUEOS_QMP_PATH"]
log_path = os.environ.get("AIUEOS_QMP_LOG", "")
def log(msg):
    if not log_path:
        return
    try:
        with open(log_path, "a") as f:
            f.write(msg + "\n")
    except OSError:
        pass
deadline = time.time() + 60
sock = None
while time.time() < deadline:
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(path)
        break
    except OSError:
        time.sleep(0.05)
        sock = None
if sock is None:
    log("connect-timeout")
    sys.exit(0)
sock.settimeout(1.0)
buf = b""
def recv_obj():
    global buf
    while True:
        nl = buf.find(b"\n")
        if nl >= 0:
            line, buf = buf[:nl], buf[nl+1:]
            line = line.strip()
            if line:
                return json.loads(line)
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            return None
        if not chunk:
            return None
        buf += chunk
banner = recv_obj()
log("banner " + json.dumps(banner)[:400])
sock.sendall(b'{"execute":"qmp_capabilities"}\n')
log("caps " + json.dumps(recv_obj())[:400])
# QEMU 10.1 input-send-event `device` is a *display console* name, not the
# virtio-keyboard-pci id. Passing id=kbd0 aborts with
# qemu-fixed-text-console.device. Broadcast (no device) reaches the
# virtio-keyboard input handler. send-key is the PS2 path and is not this gate.
status_deadline = time.time() + 30
while time.time() < status_deadline:
    sock.sendall(b'{"execute":"query-status"}\n')
    st = recv_obj()
    if st and st.get("return", {}).get("running"):
        log("running")
        break
    time.sleep(0.1)
key = {"type": "qcode", "data": "a"}
events = [
    {"type": "key", "data": {"down": True, "key": key}},
    {"type": "key", "data": {"down": False, "key": key}},
]
payload = json.dumps({"execute": "input-send-event", "arguments": {"events": events}}) + "\n"
end = time.time() + 90
i = 0
while time.time() < end:
    try:
        sock.sendall(payload.encode())
        reply = recv_obj()
        if reply and i < 8:
            log("reply " + json.dumps(reply)[:400])
        i += 1
    except OSError as e:
        log("send-error " + str(e))
        break
    time.sleep(0.05)
log("sent " + str(i))
PY
    inject_pid=$!
  fi
  scanout_pid=
  if [ "${AIUEOS_GUEST_SCANOUT_TWO:-0}" = 1 ]; then
    AIUEOS_SCANOUT_DBUS_LOG="$out/guest-scanout-dbus.log" python3 - <<'PY' &
import os, subprocess, time
log_path = os.environ.get("AIUEOS_SCANOUT_DBUS_LOG", "")
def log(msg):
    if not log_path:
        return
    try:
        with open(log_path, "a") as f:
            f.write(msg + "\n")
    except OSError:
        pass
def gdbus(path, xoff):
    return subprocess.run(
        ["gdbus", "call", "--session", "-d", "org.qemu",
         "-o", path, "-m", "org.qemu.Display1.Console.SetUIInfo",
         "0", "0", str(xoff), "0", "1280", "800"],
        capture_output=True, text=True)
end = time.time() + 90
while time.time() < end:
    ping = subprocess.run(
        ["gdbus", "call", "--session", "-d", "org.qemu",
         "-o", "/org/qemu/Display1/VM",
         "-m", "org.freedesktop.DBus.Peer.Ping"],
        capture_output=True, text=True)
    if ping.returncode == 0:
        a = gdbus("/org/qemu/Display1/Console_0", 0)
        b = gdbus("/org/qemu/Display1/Console_1", 1280)
        log("set0 rc=%s %r set1 rc=%s %r" % (
            a.returncode, (a.stdout + a.stderr)[:200],
            b.returncode, (b.stdout + b.stderr)[:200]))
    time.sleep(0.05)
PY
    scanout_pid=$!
  fi
  set +e
  # shellcheck disable=SC2086 # intentional optional groups of QEMU arguments
  timeout "$qemu_timeout" "$qemu" \
    -machine q35,accel=tcg -cpu max -m 128M -smp 2 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive "$boot_drive" \
    -device isa-debugcon,iobase=0xe9,chardev=debug \
    -chardev file,id=debug,path="$log" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    $iommu_args \
    $usb_args \
    $net_args \
    -device virtio-rng-pci \
    -drive if=none,id=aiueosblk,format=raw,file="$blk_image" \
    -device virtio-blk-pci,drive=aiueosblk,disable-legacy=on \
    $kbd_args \
    -device virtio-vga,disable-legacy=on,max_outputs=2 \
    $qmp_args \
    -display "$display_opt" -serial "file:$serial_log" -monitor none -no-reboot
  status=$?
  if [ -n "$inject_pid" ]; then
    kill "$inject_pid" 2>/dev/null || true
    wait "$inject_pid" 2>/dev/null || true
  fi
  if [ -n "$scanout_pid" ]; then
    kill "$scanout_pid" 2>/dev/null || true
    wait "$scanout_pid" 2>/dev/null || true
  fi
  set -e
  if [ "$status" -eq 124 ] && [ "$attempt" -lt "$qemu_attempts" ]; then
    echo "warning: QEMU hung on attempt ${attempt}/${qemu_attempts} (known flake kotoba-lang/aiueos#108); retrying" >&2
    attempt=$((attempt + 1))
    continue
  fi
  break
done
[ -n "$pristine_blk" ] && rm -f "$pristine_blk"
if [ -n "$aiueos_dbus_pid" ]; then
  kill "$aiueos_dbus_pid" 2>/dev/null || true
fi

if [ "$status" -eq 124 ]; then
  echo "error: QEMU did not terminate within ${qemu_timeout}s (hung guest)" >&2
  echo "--- debug log tail ---" >&2
  test -f "$log" && tail -40 "$log" >&2
  echo "--- serial log tail ---" >&2
  test -f "$serial_log" && sed 's/\x1b\[[0-9;=]*[A-Za-z]//g' "$serial_log" | tail -40 >&2
  exit 1
fi

if [ "${AIUEOS_PLC_RT_SMOKE:-0}" = 1 ]; then
  [ "$status" -eq 133 ] || {
    echo "error: PLC RT smoke produced unexpected QEMU status $status" >&2
    test -f "$serial_log" && tail -30 "$serial_log" >&2
    exit 1
  }
  grep -F "AIUEOS_PLC_RT_OK profile=aiueos-plc-v1 scheduler=fixed-priority-preemptive priority=5 program=receipt-bound-cpl3-elf scans=2 outputs=1:42,0:100 transport=syscall release=apic-absolute-ticks cycle=10ticks input=snapshot output=atomic-safe-state capabilities=16,17,18,19 failures=stage,watchdog,budget,deadline,program timing=logical-unqualified" "$serial_log" >/dev/null || {
    echo "error: native PLC RT provider evidence was not observed" >&2
    exit 1
  }
  echo "AIUEOS_PLC_RT_QEMU_OK scans=2 fixed-priority-preemption receipt-bound-cpl3-elf native-provider apic-release transactional-output safe-state"
  exit 0
fi

if [ "${AIUEOS_CORRUPT_KERNEL:-0}" = 1 ]; then
  [ "$status" -eq 255 ] || {
    echo "error: corrupted kernel produced unexpected QEMU status $status" >&2
    exit 1
  }
  grep -F "AIUEOS_LOADER_FAIL kernel-sha256" "$log" >/dev/null || {
    echo "error: corrupted kernel was not rejected by loader" >&2
    exit 1
  }
  echo "AIUEOS_KERNEL_INTEGRITY_REJECTION_OK"
  exit 0
fi

if [ "${AIUEOS_CORRUPT_INITRAMFS:-0}" = 1 ]; then
  [ "$status" -eq 255 ] || {
    echo "error: corrupted initramfs produced unexpected QEMU status $status" >&2
    exit 1
  }
  grep -F "AIUEOS_LOADER_FAIL initramfs-sha256" "$log" >/dev/null || {
    echo "error: corrupted initramfs was not rejected by loader" >&2
    exit 1
  }
  echo "AIUEOS_INITRAMFS_INTEGRITY_REJECTION_OK"
  exit 0
fi

if [ "${AIUEOS_CORRUPT_RECOVERY_SIG:-0}" = 1 ]; then
  # The kernel policy rejection writes 0x68; isa-debug-exit maps it to 209.
  [ "$status" -eq 209 ] || {
    echo "error: corrupted recovery signature produced unexpected QEMU status $status" >&2
    test -f "$serial_log" && tail -20 "$serial_log" >&2
    exit 1
  }
  grep -F "AIUEOS_INITRAMFS_RECOVERY_ADMISSION_FAIL rsa2048-policy" "$serial_log" >/dev/null || {
    echo "error: recovery payload with a bad signature was not rejected by the kernel policy" >&2
    exit 1
  }
  echo "AIUEOS_INITRAMFS_RECOVERY_REJECTION_OK rsa2048-policy"
  exit 0
fi

if [ "${AIUEOS_EXPECT_FAULT:-0}" = 1 ]; then
  # The unexpected-exception path writes 0x7d; isa-debug-exit maps it to 251.
  [ "$status" -eq 251 ] || {
    echo "error: synthetic fault produced unexpected QEMU status $status" >&2
    test -f "$serial_log" && tail -20 "$serial_log" >&2
    exit 1
  }
  grep -F "AIUEOS_FAULT_SMOKE synthetic unexpected-ud2" "$serial_log" >/dev/null || {
    echo "error: synthetic fault trigger marker was not observed" >&2
    exit 1
  }
  grep -F "AIUEOS_FAULT_RECEIPT_OK polled try-lock written readback pending" "$serial_log" >/dev/null || {
    echo "error: fault-context crash receipt write evidence was not observed" >&2
    exit 1
  }
  echo "AIUEOS_FAULT_BOOT_OK synthetic-fault polled-receipt-written"
  exit 0
fi

if [ "${AIUEOS_EXPECT_CRASH:-0}" = 1 ]; then
  # The synthetic panic writes 0x5c; isa-debug-exit maps it to (0x5c << 1) | 1.
  [ "$status" -eq 185 ] || {
    echo "error: synthetic panic produced unexpected QEMU status $status" >&2
    test -f "$serial_log" && tail -20 "$serial_log" >&2
    exit 1
  }
  grep -F "AIUEOS_PANIC synthetic reason=42" "$serial_log" >/dev/null || {
    echo "error: synthetic panic marker was not observed" >&2
    exit 1
  }
  grep -F "AIUEOS_PANIC_RECEIPT_OK synthetic reason=42 written readback pending" "$serial_log" >/dev/null || {
    echo "error: durable crash receipt write evidence was not observed" >&2
    exit 1
  }
  echo "AIUEOS_CRASH_PANIC_BOOT_OK synthetic-panic receipt-written"
  exit 0
fi

# The #UD handler writes 0x30; isa-debug-exit maps it to (0x30 << 1) | 1 = 97.
[ "$status" -eq 97 ] || {
  if [ "${AIUEOS_CORRUPT_KOTOBA_CATALOG:-0}" = 1 ] && [ "$status" -eq 227 ]; then
    ! grep -F "AIUEOS_KOTOBA_APP_ADMISSION_OK" "$serial_log" >/dev/null || {
      echo "error: corrupted Kotoba catalog reached admission" >&2; exit 1;
    }
    echo "AIUEOS_KOTOBA_APP_AUTH_REJECTION_OK catalog"
    exit 0
  fi
  echo "error: unexpected QEMU exit status $status" >&2
  test -f "$log" && sed -n '1,80p' "$log" >&2
  exit 1
}

# A corrupted application payload or signature is no longer fatal: the kernel
# must restore it from the initramfs recovery materials under the catalog
# digest and RSA policy, then pass the complete evidence gate below.
if [ "${AIUEOS_CORRUPT_KOTOBA_APP:-0}" = 1 ] || [ "${AIUEOS_CORRUPT_KOTOBA_SIGNATURE:-0}" = 1 ]; then
  grep -F "AIUEOS_OBJECT_STORE_RESTORE_OK apps=1 source=initramfs catalog-digest-bound rsa2048 write-readback" "$serial_log" >/dev/null || {
    echo "error: corrupted application was not restored from the initramfs" >&2
    exit 1
  }
fi
grep -F "AIUEOS_LOADER_OK" "$log" >/dev/null || {
  echo "error: loader identity was not observed" >&2
  exit 1
}
grep -F "AIUEOS_GOP_HANDOFF_OK framebuffer-v1" "$log" >/dev/null || {
  echo "error: loader did not hand off a validated GOP mode" >&2
  exit 1
}
grep -F "AIUEOS_LOADER_INTEGRITY_OK sha256-v1" "$log" >/dev/null || {
  echo "error: kernel integrity evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KERNEL_OK memory-map-v1" "$log" >/dev/null || {
  echo "error: kernel handoff was not observed" >&2
  exit 1
}
grep -F "AIUEOS_INITRAMFS_OK newc entries=3 sha256-admitted bounded" "$serial_log" >/dev/null || {
  echo "error: bounded initramfs validation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_INITRAMFS_RECOVERY_ADMISSION_OK elf digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key" "$serial_log" >/dev/null || {
  echo "error: recovery payload admission evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERIAL_OK stack-v1 memory-map-v1" "$serial_log" >/dev/null || {
  echo "error: kernel COM1 evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,80p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_NATIVE_OK elf64-relocatable sysv-v1 result=42" "$serial_log" >/dev/null || {
  echo "error: Kotoba compiler-emitted native probe did not execute" >&2
  exit 1
}
grep -F "AIUEOS_DESCRIPTOR_TABLES_OK gdt-v1 idt-v1" "$serial_log" >/dev/null || {
  echo "error: kernel descriptor-table evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PAGING_OK cr3-owned wx-v1 nx-wp" "$serial_log" >/dev/null || {
  echo "error: kernel-owned paging evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_FRAMEBUFFER_OK gop-owned retained-rectangles hash-verified" "$serial_log" >/dev/null || {
  echo "error: kernel did not validate and render the GOP framebuffer" >&2
  exit 1
}
grep -F "AIUEOS_DESKTOP_SURFACE_OK envelope-v1 opaque-handle full-damage hash-verified" "$serial_log" >/dev/null || {
  echo "error: bounded desktop surface envelope was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PHYSICAL_ALLOCATOR_OK pages=2 zeroed" "$serial_log" >/dev/null || {
  echo "error: physical page allocator evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_ACPI_OK rsdp-xsdt-madt cpu>=2" "$serial_log" >/dev/null || {
  echo "error: validated ACPI CPU discovery evidence was not observed" >&2
  exit 1
}
if [ "${AIUEOS_TEST_DMAR:-0}" = 1 ]; then
  grep -F "AIUEOS_VTD_OK tes=1 root-context-slpt domain=1 aperture=128MiB" "$serial_log" >/dev/null || {
    echo "error: VT-d translation-enable register evidence was not observed" >&2; exit 1;
  }
  grep -F "AIUEOS_DMA_POLICY_OK dmar=validated dma=vtd-isolated" "$serial_log" >/dev/null || {
    echo "error: isolated VT-d DMA policy evidence was not observed" >&2; exit 1;
  }
else
  grep -F "AIUEOS_DMA_POLICY_OK dmar=absent test-only-unisolated" "$serial_log" >/dev/null || {
    echo "error: explicit no-IOMMU test DMA policy evidence was not observed" >&2; exit 1;
  }
fi
grep -F "AIUEOS_APIC_TIMER_OK vector=32 eoi-v1" "$serial_log" >/dev/null || {
  echo "error: Local APIC timer interrupt evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SMP_OK cpus=2 init-sipi-v1 per-cpu-stack" "$serial_log" >/dev/null || {
  echo "error: BSP-to-AP INIT/SIPI evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PCI_OK bounded-scan virtio-vendor=1af4" "$serial_log" >/dev/null || {
  echo "error: bounded PCI/virtio discovery evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,160p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_RNG_OK modern-pci caps-bounded dma=4pages completion=32" "$serial_log" >/dev/null || {
  echo "error: modern virtio-rng DMA completion evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,120p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_RNG_MSIX_OK vector=34 irq=1 table-pba-bounded" "$serial_log" >/dev/null || {
  echo "error: interrupt-driven virtio-rng MSI-X evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,140p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_BLK_OK capacity-bounded sector=0 bytes=512 readonly" "$serial_log" >/dev/null || {
  echo "error: modern virtio-blk bounded read evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,140p' "$serial_log" >&2
  exit 1
}
grep -F "AIUEOS_VIRTIO_BLK_MSIX_OK vector=35 irq-completions-bounded table-pba-bounded" "$serial_log" >/dev/null || {
  echo "error: interrupt-driven virtio-blk MSI-X completion evidence was not observed" >&2
  test -f "$serial_log" && sed -n '1,160p' "$serial_log" >&2
  exit 1
}
if [ "${AIUEOS_TEST_DMAR:-0}" = 1 ]; then
  grep -F "AIUEOS_VTD_IR_OK irta=256 source-validated vector=35 remappable-msix" "$serial_log" >/dev/null || {
    echo "error: VT-d interrupt-remapped MSI-X evidence was not observed" >&2; exit 1;
  }
fi
grep -F "AIUEOS_OBJECT_STORE_OK aiuefs-v3 objects=3 catalog=2apps" "$serial_log" >/dev/null || {
  echo "error: bounded read-only object-store evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_APP_ADMISSION_OK catalog=rsa2048 apps=2 digest=kotoba-sha256 signature=kotoba-rsa2048-pkcs1 policy=public-key" "$serial_log" >/dev/null || {
  echo "error: authenticated object-store Kotoba app admission was not observed" >&2
  exit 1
}
# The catalog-policy self-test is a test-only compile gate. The update-flow
# smoke boots a previous-version image built without it and asserts the
# marker's absence to prove which version booted.
if [ "${AIUEOS_EXPECT_CATALOG_POLICY_SELFTEST:-1}" = 1 ]; then
  grep -F "AIUEOS_KOTOBA_CATALOG_POLICY_SELFTEST_OK malformed=6" "$serial_log" >/dev/null || {
    echo "error: Kotoba catalog policy malformed-input evidence missing" >&2
    cat "$serial_log" >&2
    exit 1
  }
else
  ! grep -F "AIUEOS_KOTOBA_CATALOG_POLICY_SELFTEST_OK" "$serial_log" >/dev/null || {
    echo "error: unexpected catalog-policy self-test evidence in previous-version boot" >&2
    exit 1
  }
fi
grep -F "AIUEOS_JOURNAL_OK dual-slot committed append-readback" "$serial_log" >/dev/null || {
  echo "error: journal write/readback evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_OBJECT_TXN_OK journal-first sector=3 apply-readback route=kotoba fixed-stack" "$serial_log" >/dev/null || {
  echo "error: journal-backed object transaction evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_JOURNAL_PLAN_OK" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native journal planning evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_FNV_OK bounded-load journal-object-validation" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native bounded FNV evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_RECORD_VALIDATION_OK journal transaction bounded-u32" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native record validation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_STORAGE_READ_VALIDATION_OK superblock mutable-object" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native storage read validation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_STORAGE_WRITE_OK journal mutable-object bounded-store" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native storage write evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_PCI_PLANNER_OK cap extent msix-region" "$serial_log" >/dev/null || {
  echo "error: Kotoba-native PCI planner evidence was not observed" >&2
  exit 1
}
if [ "${AIUEOS_GUEST_INPUT:-0}" = 1 ]; then
  grep -F "AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured used-ring" "$serial_log" >/dev/null || {
    echo "error: virtio-input used-ring evidence was not observed" >&2; exit 1;
  }
  grep -F "AIUEOS_GUEST_INPUT_OK eventq-used=1 synthetic=0" "$serial_log" >/dev/null || {
    echo "error: guest-input used-ring serial was not observed" >&2; exit 1;
  }
else
  grep -F "AIUEOS_VIRTIO_INPUT_OK modern-pci eventq configured synthetic-smoke" "$serial_log" >/dev/null || {
    echo "error: modern virtio-input configuration/synthetic transport evidence was not observed" >&2; exit 1;
  }
fi
grep -F "AIUEOS_DESKTOP_INPUT_OK envelope-v1 sequence=1 kind=key ime-neutral" "$serial_log" >/dev/null || {
  echo "error: validated browser desktop input envelope was not observed" >&2; exit 1;
}
grep -F "AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded" "$serial_log" >/dev/null || {
  echo "error: bounded virtio-gpu display-info completion was not observed" >&2
  exit 1
}
grep -F "AIUEOS_BROWSER_DESKTOP_TRANSPORT_OK surface-v1 gpu-scanout-bound input-v1" "$serial_log" >/dev/null || {
  echo "error: framebuffer/browser desktop transport binding was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SCHEDULER_OK tasks=2 policy=round-robin preemption=apic-timer" "$serial_log" >/dev/null || {
  echo "error: preemptive round-robin scheduler evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SCHEDULER_CR3_OK roots=3 private-pages=2 kernel-return" "$serial_log" >/dev/null || {
  echo "error: scheduler-driven address-space switching evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERVICE_RUNTIME_OK services=2 descriptors=8 kotoba-policy spawn-restart-terminate task=generic generation=2 budget=bounded" "$serial_log" >/dev/null || {
  echo "error: persistent service runtime evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERVICE_IPC_OK mailbox=bounded capability=owner-domain cross-cr3 sequence=1" "$serial_log" >/dev/null || {
  echo "error: capability-checked cross-address-space service IPC evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SERVICE_REGISTRY_OK journal-object ids=2 generation=2,1 restart=1,0 decoder=kotoba fixed-stack" "$serial_log" >/dev/null || {
  echo "error: durable service registry transaction evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_IOAPIC_OK pit-gsi vector=33 eoi-v1" "$serial_log" >/dev/null || {
  echo "error: IOAPIC external timer IRQ evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_SYSCALL_OK int80-cpl0 abi-v1" "$serial_log" >/dev/null || {
  echo "error: CPL0 syscall evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_SYSCALL_PLANNER_OK bootstrap user overflow" "$serial_log" >/dev/null || {
  echo "error: Kotoba syscall range planner evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_COPY_IN_OK cpl0 hash bounded-256" "$serial_log" >/dev/null || {
  echo "error: Kotoba bounded copy-in evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_CAPABILITY_OK table owner generation type rights revoke reissue derivation=multi-hop recursive-revoke" "$serial_log" >/dev/null || {
  echo "error: capability negative evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_DYNAMIC_CAPABILITY_OK page-backed slots>=256 owner=3 reuse generation retirement" "$serial_log" >/dev/null || {
  echo "error: dynamic page-backed capability table evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PROCESS_FOUNDATION_OK tss-descriptor user-wx guard-page" "$serial_log" >/dev/null || {
  echo "error: process isolation foundation evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_ADDRESS_SPACE_OK processes=2 distinct-cr3 private-pages cross-access-fault" "$serial_log" >/dev/null || {
  echo "error: per-process address-space isolation evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_RING3_OK processes=2 preemptive roots=2 domains=2,3 kernel-stacks=2 syscall-sysret" "$serial_log" >/dev/null || {
  echo "error: CPL3 syscall and kernel-return evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_PROCESS_CREATE_OK descriptors=8 entry-argument-stack domain-address-space-task" "$serial_log" >/dev/null || {
  echo "process-create ABI evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_SYSRET_OK star-lstar-fmask canonical-rip-rsp rflags-sanitized per-task-stack" "$serial_log" >/dev/null || {
  echo "error: native syscall/sysret evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_CAPABILITY_TRANSFER_OK source=2 target=3 attenuated atomic-claim transferred-use owner-exit=descendants-revoked" "$serial_log" >/dev/null || {
  echo "error: atomic process capability transfer evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PROCESS_REAP_OK tasks=4 services=2-persistent process-slots=8 task-slots=8 generations=reused owner-caps-revoked allocator-pages=24 stack-pages=reused zero-reused" "$serial_log" >/dev/null || {
  echo "error: process exit/reap/reuse evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_ELF_PROCESS_OK source=catalog apps=2 et-exec segments=rx,rw result=42 domains=4,5" "$serial_log" >/dev/null || {
  echo "Kotoba ELF process evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_USER_RUNTIME_OK abi=v2 transport=syscall capabilities=2,3,4,5 object=service-registry,user-store service-ipc=mailbox domains=4,5 result=42" "$serial_log" >/dev/null || {
  echo "error: Kotoba user runtime syscall evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_SERVICE_IPC_OK senders=4,5 recipients=service0,service1 payload=42 sequence=1 bounded=2 persistent-services=2" "$serial_log" >/dev/null || {
  echo "error: Kotoba to persistent service IPC evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_KOTOBA_OBJECT_WRITE_OK domains=4,5 journals=44-47 objects=42,43 value=42 receipt=readback transaction=journal-first serializer=kotoba validator=kotoba decoder=kotoba materializer=kotoba fixed-stack" "$serial_log" >/dev/null || {
  echo "error: Kotoba user object transaction evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_APP_CATALOG_LOOKUP_OK ids=app/hello,app/worker unknown=denied extents=nonoverlap" "$serial_log" >/dev/null || {
  echo "catalog lookup evidence missing" >&2
  exit 1
}
grep -F "AIUEOS_USER_SYSCALL_OK valid-log copied-payload too-big stale-generation foreign-owner wrong-type no-rights invalid-pointer" "$serial_log" >/dev/null || {
  echo "error: CPL3 syscall positive/negative evidence was not observed" >&2; exit 1;
}
grep -F "AIUEOS_COPYIN_OK noncanonical-and-unmapped-denied" "$serial_log" >/dev/null || {
  echo "error: invalid-pointer evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PAGE_FAULT_OK write-protect vector=14" "$serial_log" >/dev/null || {
  echo "error: write-protect page-fault evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_PAGE_FAULT_OK no-execute vector=14" "$serial_log" >/dev/null || {
  echo "error: no-execute page-fault evidence was not observed" >&2
  exit 1
}
grep -F "AIUEOS_EXCEPTION_OK vector=6 invalid-opcode" "$serial_log" >/dev/null || {
  echo "error: kernel exception dispatch evidence was not observed" >&2
  exit 1
}
# With a NIC attached, QEMU always runs a DHCP server, so a boot that fails to
# configure an address has a real defect. Asserted HERE and not only in
# smoke-qemu-dhcp.sh, because a check that lives only in a gate nobody runs by
# habit is a check that goes quiet. Skipped when the reply is being deliberately
# broken -- those runs are supposed to be refused, and that is what the DHCP gate
# asserts instead.
if [ "${AIUEOS_TEST_NET:-0}" = 1 ] && \
   { [ -z "${AIUEOS_DHCP_TAMPER:-}" ] || [ "${AIUEOS_DHCP_TAMPER}" = 0 ]; }; then
  grep -F "AIUEOS_DHCP_OK offer-ack kotoba-admitted address=10.0.2.15 mask=255.255.255.0 router=10.0.2.2 server=10.0.2.2 lease=" "$serial_log" >/dev/null || {
    echo "error: DHCPv4 lease evidence was not observed" >&2
    sed 's/\r$//' "$serial_log" | grep -E '^AIUEOS_DHCP_' >&2 || \
      echo "       (no AIUEOS_DHCP_ marker at all)" >&2
    exit 1
  }
  grep -F "AIUEOS_DHCP_CONSUMED src=10.0.2.15" "$serial_log" >/dev/null || {
    echo "error: DHCPv4 lease was recorded but not consumed as a source address" >&2
    sed 's/\r$//' "$serial_log" | grep -E '^AIUEOS_DHCP_CONSUMED' >&2 || \
      echo "       (no AIUEOS_DHCP_CONSUMED marker at all)" >&2
    exit 1
  }
fi
echo "AIUEOS_UEFI_SMOKE_OK"
