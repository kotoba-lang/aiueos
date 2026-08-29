#!/usr/bin/env python3
"""Direct Mac-to-K16 UEFI PXE/HTTP boot and real-time UDP diagnostics.

Set AIUEOS_PXE_BOOT to the EFI application to serve.  The remaining
AIUEOS_PXE_* variables override the direct-link defaults used by the physical
K16 qualification setup.
"""

import argparse
import hashlib
import http.server
import json
import os
import re
import select
import socket
import struct
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path


INTERFACE = os.environ.get("AIUEOS_PXE_INTERFACE", "en11")
SERVER_IP = os.environ.get("AIUEOS_PXE_SERVER_IP", "10.77.0.1")
CLIENT_IP = os.environ.get("AIUEOS_PXE_CLIENT_IP", "10.77.0.10")
NETMASK = os.environ.get("AIUEOS_PXE_NETMASK", "255.255.255.0")
BROADCAST_IP = os.environ.get("AIUEOS_PXE_BROADCAST", "10.77.0.255")
BOOT_PATH = Path(os.environ.get("AIUEOS_PXE_BOOT", "bootx64.efi")).resolve()
BOOT_FILE = BOOT_PATH.name
HTTP_PORT = int(os.environ.get("AIUEOS_PXE_HTTP_PORT", "8000"))
HTTP_BOOT_URI = f"http://{SERVER_IP}:{HTTP_PORT}/{BOOT_FILE}"
NETLOG_PORT = int(os.environ.get("AIUEOS_PXE_NETLOG_PORT", "7777"))
CONTROL_PORT = int(os.environ.get("AIUEOS_PXE_CONTROL_PORT", "7778"))
CONTROL_STATE_PATH = Path(os.environ.get(
    "AIUEOS_PXE_CONTROL_STATE", "/tmp/aiueos-k16-pxe-control-nonce"))
NEXT_BOOT_STATE_PATH = Path(os.environ.get(
    "AIUEOS_PXE_NEXT_BOOT_STATE", "/tmp/aiueos-k16-pxe-next-boot"))
CONTROL_COMMANDS = ("ping", "reboot-pxe")
MURAKUMO_API = os.environ.get(
    "AIUEOS_MURAKUMO_API", "https://api.murakumo.cloud").rstrip("/")
MURAKUMO_NODE_NAME = os.environ.get(
    "AIUEOS_MURAKUMO_NODE_NAME", "gmktec-k16")
MURAKUMO_NODE_DID = os.environ.get("AIUEOS_MURAKUMO_NODE_DID", "")
MURAKUMO_SERVICE_TOKEN = os.environ.get(
    "AIUEOS_MURAKUMO_SERVICE_TOKEN",
    os.environ.get("MURAKUMO_SERVICE_TOKEN", ""))
MURAKUMO_EXPECTED_MAC = os.environ.get(
    "AIUEOS_MURAKUMO_EXPECTED_MAC", "70-70-fc-0b-b6-32").lower()
IP_BOUND_IF = 25
MAGIC = b"\x63\x82\x53\x63"
CONTROL_READY = re.compile(r"^AIUEOS_CONTROL_READY nonce=([0-9a-f]{16})\b")
NODE_HELLO = re.compile(
    r"^AIUEOS_NODE_HELLO_V1 boot=([0-9a-f]{16}) "
    r"mac=([0-9a-f]{2}(?:-[0-9a-f]{2}){5}) "
    r"profile=rtl8125-relay-test$")
NEXT_BOOT_LOCK = threading.Lock()
MURAKUMO_BOOT_LOCK = threading.Lock()
MURAKUMO_SEEN_BOOTS = set()


def ipv4(value):
    return socket.inet_aton(value)


def parse_options(packet):
    result = {}
    if len(packet) < 240 or packet[236:240] != MAGIC:
        return result
    position = 240
    while position < len(packet):
        code = packet[position]
        position += 1
        if code == 0:
            continue
        if code == 255:
            break
        if position >= len(packet):
            break
        length = packet[position]
        position += 1
        if position + length > len(packet):
            break
        result[code] = packet[position:position + length]
        position += length
    return result


def option(code, payload):
    if len(payload) > 255:
        raise ValueError("DHCP option is too long")
    return bytes((code, len(payload))) + payload


def dhcp_reply(request, message_type):
    if len(request) < 240:
        raise ValueError("short DHCP request")
    reply = bytearray(236)
    reply[:236] = request[:236]
    reply[0] = 2                         # BOOTREPLY
    reply[3] = 0                         # hops
    reply[12:16] = b"\0" * 4            # ciaddr
    reply[16:20] = ipv4(CLIENT_IP)       # yiaddr
    reply[20:24] = ipv4(SERVER_IP)       # siaddr
    reply[44:108] = b"\0" * 64
    server_name = b"AIUEOS PXE"
    reply[44:44 + len(server_name)] = server_name
    reply[108:236] = b"\0" * 128
    request_options = parse_options(request)
    vendor = request_options.get(60, b"")
    http_boot = vendor.startswith(b"HTTPClient")
    filename = (HTTP_BOOT_URI if http_boot else BOOT_FILE).encode("ascii")
    reply[108:108 + len(filename)] = filename
    values = [
        option(53, bytes((message_type,))),
        option(54, ipv4(SERVER_IP)),
        option(51, struct.pack("!I", 3600)),
        option(1, ipv4(NETMASK)),
        option(3, ipv4(SERVER_IP)),
        option(28, ipv4(BROADCAST_IP)),
        option(66, SERVER_IP.encode("ascii")),
        option(67, filename),
    ]
    architecture = request_options.get(93)
    if architecture:
        values.append(option(93, architecture))
    if http_boot:
        values.append(option(60, b"HTTPClient"))
    values.append(b"\xff")
    packet = bytes(reply) + MAGIC + b"".join(values)
    return packet + b"\0" * max(0, 300 - len(packet))


def mac_address(packet):
    return ":".join(f"{byte:02x}" for byte in packet[28:34])


def bind_interface(sock, port, address=""):
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.IPPROTO_IP, IP_BOUND_IF,
                    socket.if_nametoindex(INTERFACE))
    sock.bind((address, port))


def extract_control_nonce(message):
    match = CONTROL_READY.match(message)
    return match.group(1) if match else None


def control_payload(command, nonce):
    if command not in CONTROL_COMMANDS:
        raise ValueError(f"unsupported control command: {command}")
    if not re.fullmatch(r"[0-9a-f]{16}", nonce or ""):
        raise ValueError("control nonce must be 16 lowercase hex digits")
    return f"AIUEOS_CTL_V1 nonce={nonce} command={command}".encode("ascii")


def node_ack_payload(message):
    """Return a request-bound diagnostic ACK, never a fleet enrollment claim."""
    match = NODE_HELLO.fullmatch(message)
    if not match:
        return None
    boot, _mac = match.groups()
    return f"AIUEOS_NODE_ACK_V1 boot={boot} state=accepted".encode("ascii")


def murakumo_relay_configured():
    return bool(MURAKUMO_SERVICE_TOKEN and
                MURAKUMO_NODE_DID.startswith("did:key:"))


def murakumo_enrollment():
    return {
        "node/name": MURAKUMO_NODE_NAME,
        "node/did": MURAKUMO_NODE_DID,
        "node/tier": "native",
        "node/connect": "mac-relay",
        "node/needs-relay?": True,
        "node/trust-tier": "awai-secure",
        "node/caps": {
            "engine": "aiueos-native",
            "mem-bytes": 35185754112,
            "gpu": "amd-1002:1681-unqualified",
        },
        "node/can": [
            "host-large-model", "low-latency-pipeline",
            "media-generate", "full-shard",
        ],
    }


def murakumo_heartbeat():
    # A relay round trip is liveness, not inference readiness. Capacity and a
    # model are withheld until a real K16 job has completed and returned.
    return {
        "did": MURAKUMO_NODE_DID,
        "node/name": MURAKUMO_NODE_NAME,
        "node/ready?": False,
        "node/engine": "aiueos-native-relay",
    }


def murakumo_post(path, body, opener=urllib.request.urlopen):
    request = urllib.request.Request(
        MURAKUMO_API + path,
        data=json.dumps(body, separators=(",", ":")).encode("utf-8"),
        method="POST",
        headers={
            "authorization": f"Bearer {MURAKUMO_SERVICE_TOKEN}",
            "content-type": "application/json",
            "user-agent": "aiueos-k16-relay/1",
        })
    try:
        with opener(request, timeout=8) as response:
            payload = response.read(4096)
            return response.status, json.loads(payload or b"{}")
    except urllib.error.HTTPError as error:
        payload = error.read(4096)
        try:
            parsed = json.loads(payload or b"{}")
        except (json.JSONDecodeError, UnicodeDecodeError):
            parsed = {"error": "non-json response"}
        return error.code, parsed


def register_murakumo_hello(message, opener=urllib.request.urlopen):
    match = NODE_HELLO.fullmatch(message)
    if not match:
        return {"state": "ignored", "reason": "invalid-hello"}
    boot, mac = match.groups()
    if mac != MURAKUMO_EXPECTED_MAC:
        return {"state": "ignored", "reason": "unexpected-mac"}
    if not murakumo_relay_configured():
        return {"state": "disabled", "reason": "identity-or-token-unset"}
    with MURAKUMO_BOOT_LOCK:
        if boot in MURAKUMO_SEEN_BOOTS:
            return {"state": "duplicate", "boot": boot}
        MURAKUMO_SEEN_BOOTS.add(boot)
    enrollment_status, _ = murakumo_post(
        "/infer/nodes", murakumo_enrollment(), opener)
    if enrollment_status not in (200, 201):
        with MURAKUMO_BOOT_LOCK:
            MURAKUMO_SEEN_BOOTS.discard(boot)
        return {"state": "failed", "stage": "enroll",
                "status": enrollment_status, "boot": boot}
    heartbeat_status, _ = murakumo_post(
        f"/infer/nodes/{MURAKUMO_NODE_NAME}/heartbeat",
        murakumo_heartbeat(), opener)
    if heartbeat_status != 201:
        with MURAKUMO_BOOT_LOCK:
            MURAKUMO_SEEN_BOOTS.discard(boot)
        return {"state": "failed", "stage": "heartbeat",
                "status": heartbeat_status, "boot": boot}
    return {"state": "live-not-ready", "boot": boot,
            "enrollment-status": enrollment_status,
            "heartbeat-status": heartbeat_status}


def relay_murakumo_hello(message):
    try:
        result = register_murakumo_hello(message)
        fields = " ".join(f"{key}={value}" for key, value in result.items())
        print(f"AIUEOS_MURAKUMO_RELAY {fields}", flush=True)
    except Exception as error:
        print(f"AIUEOS_MURAKUMO_RELAY state=failed stage=client "
              f"error={type(error).__name__}", flush=True)


def send_control(command, nonce):
    payload = control_payload(command, nonce)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.IPPROTO_IP, IP_BOUND_IF,
                    socket.if_nametoindex(INTERFACE))
    sock.bind((SERVER_IP, 0))
    for _ in range(5):
        sock.sendto(payload, (CLIENT_IP, CONTROL_PORT))
        time.sleep(0.2)
    print(f"AIUEOS_CONTROL_TX target={CLIENT_IP}:{CONTROL_PORT} "
          f"command={command} nonce={nonce}", flush=True)
    sock.close()


def artifact_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def next_boot_path():
    with NEXT_BOOT_LOCK:
        if not NEXT_BOOT_STATE_PATH.is_file():
            return BOOT_PATH
        try:
            selected = Path(NEXT_BOOT_STATE_PATH.read_text(
                encoding="utf-8").strip()).resolve()
        except (OSError, ValueError):
            return BOOT_PATH
        return selected if selected.is_file() else BOOT_PATH


def arm_next_boot(path):
    selected = Path(path).resolve()
    if not selected.is_file():
        raise SystemExit(f"missing next-boot EFI: {selected}")
    if selected.stat().st_size > 16 * 1024 * 1024:
        raise SystemExit(f"next-boot EFI exceeds 16 MiB: {selected}")
    with NEXT_BOOT_LOCK:
        NEXT_BOOT_STATE_PATH.write_text(str(selected) + "\n", encoding="utf-8")
    print(f"AIUEOS_PXE_NEXT_BOOT_ARMED path={selected} "
          f"bytes={selected.stat().st_size} sha256={artifact_sha256(selected)}",
          flush=True)


def consume_next_boot(selected):
    with NEXT_BOOT_LOCK:
        if not NEXT_BOOT_STATE_PATH.is_file():
            return
        try:
            armed = Path(NEXT_BOOT_STATE_PATH.read_text(
                encoding="utf-8").strip()).resolve()
        except (OSError, ValueError):
            return
        if armed != selected.resolve():
            return
        NEXT_BOOT_STATE_PATH.unlink()
    print(f"AIUEOS_PXE_NEXT_BOOT_CONSUMED path={selected} "
          f"fallback={BOOT_PATH}", flush=True)


def dhcp_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_interface(sock, 67)
    print(f"AIUEOS_PXE_DHCP_READY interface={INTERFACE} server={SERVER_IP} "
          f"offer={CLIENT_IP}", flush=True)
    while True:
        packet, address = sock.recvfrom(4096)
        values = parse_options(packet)
        if len(packet) < 34 or packet[0] != 1 or 53 not in values:
            continue
        message_type = values[53][0]
        architecture = struct.unpack("!H", values.get(93, b"\xff\xff")[:2])[0]
        vendor = values.get(60, b"").decode("ascii", "replace")
        mac = mac_address(packet)
        print(f"AIUEOS_PXE_DHCP_RX mac={mac} type={message_type} "
              f"arch={architecture} vendor={vendor!r}", flush=True)
        if message_type == 1:
            reply_type, label = 2, "OFFER"
        elif message_type == 3:
            selected = values.get(54)
            requested = values.get(50)
            if selected and selected != ipv4(SERVER_IP):
                continue
            if requested and requested != ipv4(CLIENT_IP):
                continue
            reply_type, label = 5, "ACK"
        else:
            continue
        reply = dhcp_reply(packet, reply_type)
        sock.sendto(reply, ("255.255.255.255", 68))
        boot = HTTP_BOOT_URI if vendor.startswith("HTTPClient") else BOOT_FILE
        print(f"AIUEOS_PXE_DHCP_{label} mac={mac} address={CLIENT_IP} "
              f"boot={boot}", flush=True)


class HttpBootHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        print(f"AIUEOS_HTTP_GET from={self.client_address[0]} path={self.path}",
              flush=True)
        if self.path.split("?", 1)[0] != f"/{BOOT_FILE}":
            self.send_error(404)
            return
        selected = next_boot_path()
        content = selected.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "application/efi")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)
        self.wfile.flush()
        consume_next_boot(selected)

    def log_message(self, fmt, *args):
        print(f"AIUEOS_HTTP_RESULT from={self.client_address[0]} "
              f"message={fmt % args}", flush=True)


def http_server():
    server = http.server.ThreadingHTTPServer((SERVER_IP, HTTP_PORT), HttpBootHandler)
    print(f"AIUEOS_HTTP_READY interface={INTERFACE} uri={HTTP_BOOT_URI} "
          f"bytes={BOOT_PATH.stat().st_size}", flush=True)
    server.serve_forever()


def netlog_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_interface(sock, NETLOG_PORT, SERVER_IP)
    print(f"AIUEOS_NETLOG_READY interface={INTERFACE} "
          f"listen={SERVER_IP}:{NETLOG_PORT}", flush=True)
    while True:
        payload, peer = sock.recvfrom(4096)
        message = payload.decode("ascii", "replace").rstrip("\r\n")
        print(f"AIUEOS_NETLOG_RX from={peer[0]}:{peer[1]} "
              f"message={message}", flush=True)
        ack = node_ack_payload(message)
        if ack and peer[0] == CLIENT_IP:
            sock.sendto(ack, peer)
            print(f"AIUEOS_NODE_RELAY_ACK to={peer[0]}:{peer[1]} "
                  f"bytes={len(ack)} scope=diagnostic-only", flush=True)
            threading.Thread(target=relay_murakumo_hello, args=(message,),
                             daemon=True).start()
        nonce = extract_control_nonce(message)
        if nonce and peer[0] == CLIENT_IP:
            CONTROL_STATE_PATH.write_text(nonce + "\n", encoding="ascii")
            print(f"AIUEOS_CONTROL_STATE nonce={nonce} "
                  f"path={CONTROL_STATE_PATH}", flush=True)


def tftp_oack(options, size):
    accepted = []
    block_size = 512
    if "blksize" in options:
        try:
            block_size = min(1468, max(8, int(options["blksize"])))
            accepted += ["blksize", str(block_size)]
        except ValueError:
            block_size = 512
    if "tsize" in options:
        accepted += ["tsize", str(size)]
    if not accepted:
        return None, block_size
    payload = b"\0".join(part.encode("ascii") for part in accepted) + b"\0"
    return struct.pack("!H", 6) + payload, block_size


def wait_for_ack(sock, peer, block, payload):
    for _ in range(6):
        sock.sendto(payload, peer)
        ready, _, _ = select.select([sock], [], [], 1.0)
        if not ready:
            continue
        response, source = sock.recvfrom(2048)
        if source == peer and len(response) >= 4 and response[:2] == b"\0\4" and \
                struct.unpack("!H", response[2:4])[0] == block:
            return True
    return False


def tftp_transfer(peer, request):
    fields = request[2:].split(b"\0")
    if len(fields) < 3:
        return
    filename = fields[0].decode("ascii", "replace").lstrip("/\\").lower()
    mode = fields[1].decode("ascii", "replace").lower()
    options = {}
    for index in range(2, len(fields) - 1, 2):
        if index + 1 < len(fields) and fields[index]:
            options[fields[index].decode("ascii", "replace").lower()] = \
                fields[index + 1].decode("ascii", "replace")
    if filename not in (BOOT_FILE, "efi/boot/bootx64.efi") or mode != "octet":
        return
    selected = next_boot_path()
    content = selected.read_bytes()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_interface(sock, 0, SERVER_IP)
    print(f"AIUEOS_PXE_TFTP_RRQ from={peer[0]}:{peer[1]} file={filename} "
          f"artifact={selected.name} bytes={len(content)} options={options}",
          flush=True)
    oack, block_size = tftp_oack(options, len(content))
    if oack is not None and not wait_for_ack(sock, peer, 0, oack):
        print("AIUEOS_PXE_TFTP_FAIL stage=oack", flush=True)
        sock.close()
        return
    block = 1
    position = 0
    while True:
        chunk = content[position:position + block_size]
        payload = struct.pack("!HH", 3, block) + chunk
        if not wait_for_ack(sock, peer, block, payload):
            print(f"AIUEOS_PXE_TFTP_FAIL stage=data block={block}", flush=True)
            sock.close()
            return
        position += len(chunk)
        if len(chunk) < block_size:
            break
        block = (block + 1) & 0xffff
    print(f"AIUEOS_PXE_TFTP_OK file={filename} bytes={position}", flush=True)
    sock.close()
    consume_next_boot(selected)


def tftp_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_interface(sock, 69)
    print(f"AIUEOS_PXE_TFTP_READY interface={INTERFACE} server={SERVER_IP} "
          f"file={BOOT_FILE} bytes={BOOT_PATH.stat().st_size}", flush=True)
    while True:
        request, peer = sock.recvfrom(4096)
        if request[:2] == b"\0\1":
            threading.Thread(target=tftp_transfer, args=(peer, request),
                             daemon=True).start()


def selftest():
    request = bytearray(240)
    request[0:4] = bytes((1, 1, 6, 0))
    request[4:8] = b"TEST"
    request[28:34] = b"\x10\xec\x81\x25\x00\x01"
    request[236:240] = MAGIC
    request += option(53, b"\1") + option(60, b"PXEClient") + \
        option(93, struct.pack("!H", 9)) + b"\xff"
    reply = dhcp_reply(bytes(request), 2)
    values = parse_options(reply)
    assert reply[0] == 2 and reply[16:20] == ipv4(CLIENT_IP)
    assert reply[20:24] == ipv4(SERVER_IP)
    assert values[53] == b"\2" and values[67] == BOOT_FILE.encode("ascii")
    assert 60 not in values
    assert reply[108:108 + len(BOOT_FILE)] == BOOT_FILE.encode("ascii")

    http_request = bytearray(request[:240])
    http_request += option(53, b"\1") + option(60, b"HTTPClient") + \
        option(93, struct.pack("!H", 16)) + b"\xff"
    http_reply = dhcp_reply(bytes(http_request), 2)
    http_values = parse_options(http_reply)
    http_uri = HTTP_BOOT_URI.encode("ascii")
    assert http_values[60] == b"HTTPClient"
    assert http_values[67] == http_uri
    assert http_values[93] == struct.pack("!H", 16)
    assert http_reply[108:108 + len(http_uri)] == http_uri
    oack, size = tftp_oack({"blksize": "1024", "tsize": "0"}, 13312)
    assert size == 1024 and b"tsize\00013312\000" in oack
    ready = "AIUEOS_CONTROL_READY nonce=0123456789abcdef commands=ping,reboot-pxe"
    assert extract_control_nonce(ready) == "0123456789abcdef"
    assert control_payload("ping", "0123456789abcdef") == \
        b"AIUEOS_CTL_V1 nonce=0123456789abcdef command=ping"
    hello = ("AIUEOS_NODE_HELLO_V1 boot=0123456789abcdef "
             "mac=70-70-fc-0b-b6-32 profile=rtl8125-relay-test")
    assert node_ack_payload(hello) == \
        b"AIUEOS_NODE_ACK_V1 boot=0123456789abcdef state=accepted"
    assert node_ack_payload(hello.replace("0123456789abcdef", "short")) is None
    old_did, old_token = MURAKUMO_NODE_DID, MURAKUMO_SERVICE_TOKEN
    try:
        globals()["MURAKUMO_NODE_DID"] = "did:key:z6MkK16Selftest"
        globals()["MURAKUMO_SERVICE_TOKEN"] = "selftest-token"
        captured = []

        class FakeResponse:
            def __init__(self, status):
                self.status = status
            def read(self, _limit):
                return b"{}"
            def __enter__(self):
                return self
            def __exit__(self, *_args):
                return False

        def fake_open(request, timeout):
            captured.append((request.full_url, request.data,
                             request.get_header("Authorization"), timeout))
            return FakeResponse(201)

        result = register_murakumo_hello(hello, fake_open)
        assert result["state"] == "live-not-ready"
        assert len(captured) == 2
        assert captured[0][0].endswith("/infer/nodes")
        assert captured[1][0].endswith("/gmktec-k16/heartbeat")
        assert captured[0][2] == "Bearer selftest-token"
        enrollment = json.loads(captured[0][1])
        heartbeat = json.loads(captured[1][1])
        assert enrollment["node/trust-tier"] == "awai-secure"
        assert enrollment["node/needs-relay?"] is True
        assert heartbeat["node/ready?"] is False
        assert "node/capacity" not in heartbeat and "node/model" not in heartbeat
        assert register_murakumo_hello(hello, fake_open)["state"] == "duplicate"
    finally:
        globals()["MURAKUMO_NODE_DID"] = old_did
        globals()["MURAKUMO_SERVICE_TOKEN"] = old_token
        MURAKUMO_SEEN_BOOTS.clear()
    try:
        control_payload("reboot", "0123456789abcdef")
        raise AssertionError("unsupported control command accepted")
    except ValueError:
        pass
    print("AIUEOS_PXE_SELFTEST_OK dhcp=pxe+http tftp=oack control=token-bound "
          "node-relay=request-bound murakumo=live-not-ready interface-bound=yes")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--control", choices=CONTROL_COMMANDS)
    parser.add_argument("--nonce")
    parser.add_argument("--next-boot")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return
    if args.next_boot:
        arm_next_boot(args.next_boot)
    if args.control:
        nonce = args.nonce
        if not nonce:
            if not CONTROL_STATE_PATH.is_file():
                raise SystemExit(f"missing control nonce: {CONTROL_STATE_PATH}")
            nonce = CONTROL_STATE_PATH.read_text(encoding="ascii").strip()
        send_control(args.control, nonce)
        return
    if args.next_boot:
        return
    if not BOOT_PATH.is_file():
        raise SystemExit(f"missing boot file: {BOOT_PATH}")
    threading.Thread(target=tftp_server, daemon=True).start()
    threading.Thread(target=http_server, daemon=True).start()
    threading.Thread(target=netlog_server, daemon=True).start()
    dhcp_server()


if __name__ == "__main__":
    main()
