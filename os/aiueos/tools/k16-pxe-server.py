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
import queue
import re
import select
import socket
import stat
import struct
import tempfile
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path


def private_file_text(path_value, label, max_bytes):
    """Read one owner-only regular file without following a symlink."""
    if not path_value:
        return ""
    path = Path(path_value).expanduser()
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        details = os.fstat(descriptor)
        if not stat.S_ISREG(details.st_mode):
            raise RuntimeError(f"{label} is not a regular file")
        if details.st_uid != os.getuid() or details.st_mode & 0o077:
            raise RuntimeError(f"{label} must be owned by this user with mode 0600")
        if details.st_size > max_bytes:
            raise RuntimeError(f"{label} is too large")
        value = os.read(descriptor, max_bytes + 1).decode("ascii").strip()
        if not value or len(value.encode("ascii")) > max_bytes:
            raise RuntimeError(f"{label} is empty or too large")
        return value
    finally:
        os.close(descriptor)


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
MURAKUMO_NODE_DID = os.environ.get("AIUEOS_MURAKUMO_NODE_DID", "") or \
    private_file_text(os.environ.get("AIUEOS_MURAKUMO_NODE_DID_FILE", ""),
                      "Murakumo node DID file", 256)
MURAKUMO_SERVICE_TOKEN = os.environ.get(
    "AIUEOS_MURAKUMO_SERVICE_TOKEN",
    os.environ.get("MURAKUMO_SERVICE_TOKEN", "")) or \
    private_file_text(os.environ.get("AIUEOS_MURAKUMO_SERVICE_TOKEN_FILE", ""),
                      "Murakumo service token file", 512)
MURAKUMO_EXPECTED_MAC = os.environ.get(
    "AIUEOS_MURAKUMO_EXPECTED_MAC", "70-70-fc-0b-b6-32").lower()
PXE_EXPECTED_MAC = os.environ.get(
    "AIUEOS_PXE_EXPECTED_MAC", "70:70:fc:0b:b6:32").lower().replace("-", ":")
MURAKUMO_JOB_QUALIFICATION = os.environ.get(
    "AIUEOS_MURAKUMO_JOB_QUALIFICATION", "0") == "1"
MURAKUMO_RESUME_BOOT = ""
MURAKUMO_JOB_KIND = "aiueos-micro-infer"
MURAKUMO_JOB_MODEL = "aiueos-char-bigram-v1"
MURAKUMO_JOB_PROMPT = "murakum"
MURAKUMO_JOB_CORPUS_SHA256 = \
    "6433aeadc179877f103fdc87672d967d5c68d3f531f8b2bf156e377ad84058cb"
# Compact projection of the frozen C transition matrix: last input byte ->
# (winning next token, winning count, row total). Rows with no evidence are
# deliberately absent, so the relay never claims a job the K16 model rejects.
MURAKUMO_MICRO_INFER_ROWS = {
    " ": ("a", 3, 13), "a": ("i", 2, 8), "c": ("e", 1, 1),
    "e": (" ", 2, 9), "f": ("e", 1, 1), "g": (" ", 1, 1),
    "i": ("n", 3, 8), "j": ("o", 1, 1), "k": ("u", 2, 3),
    "m": ("o", 2, 5), "n": (" ", 1, 7), "o": (" ", 2, 7),
    "p": ("e", 1, 1), "r": ("a", 3, 5), "s": (" ", 5, 7),
    "t": ("i", 2, 4), "u": ("e", 2, 6), "v": ("e", 1, 1),
    "w": ("m", 1, 1), "y": ("o", 1, 1),
}
IP_BOUND_IF = 25
MAGIC = b"\x63\x82\x53\x63"
CONTROL_READY = re.compile(r"^AIUEOS_CONTROL_READY nonce=([0-9a-f]{16})\b")
NODE_HELLO = re.compile(
    r"^AIUEOS_NODE_HELLO_V1 boot=([0-9a-f]{16}) "
    r"mac=([0-9a-f]{2}(?:-[0-9a-f]{2}){5}) "
    r"profile=rtl8125-relay-test$")
JOB_RESULT = re.compile(
    r"^AIUEOS_JOB_RESULT_V1 boot=([0-9a-f]{16}) id=([0-9]{1,20}) "
    r"model=aiueos-char-bigram-v1 token=([0-9a-f]{2}) "
    r"score=([0-9]{1,5}) total=([0-9]{1,5})$")
NODE_PONG = re.compile(
    r"^AIUEOS_NODE_PONG_V1 boot=([0-9a-f]{16}) "
    r"seq=([0-9]{1,10}) state=ready$")
NEXT_BOOT_LOCK = threading.Lock()
MURAKUMO_BOOT_LOCK = threading.Lock()
MURAKUMO_SEEN_BOOTS = set()
MURAKUMO_JOB_RESULTS = queue.Queue()
MURAKUMO_LIVENESS_RESULTS = queue.Queue()


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


def expected_dhcp_client(packet):
    """Accept DHCP only from the one physical K16 under qualification."""
    return len(packet) >= 34 and mac_address(packet) == PXE_EXPECTED_MAC


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
            "qualification-model": MURAKUMO_JOB_MODEL,
            "physical-network": "rtl8125-unisolated-qualification",
        },
        "node/can": [MURAKUMO_JOB_KIND],
    }


def murakumo_heartbeat(ready=False):
    # A relay round trip is liveness, not inference readiness. Capacity and a
    # model are withheld until a real K16 job has completed and returned.
    heartbeat = {
        "did": MURAKUMO_NODE_DID,
        "node/name": MURAKUMO_NODE_NAME,
        "node/ready?": ready,
        "node/engine": "aiueos-native-relay",
    }
    if ready:
        heartbeat["node/model"] = MURAKUMO_JOB_MODEL
        heartbeat["node/capacity"] = {
            "slots-total": 1,
            "slots-free": 1,
        }
    return heartbeat


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


def murakumo_get(path, opener=urllib.request.urlopen):
    request = urllib.request.Request(
        MURAKUMO_API + path,
        method="GET",
        headers={
            "authorization": f"Bearer {MURAKUMO_SERVICE_TOKEN}",
            "accept": "application/json",
            "user-agent": "aiueos-k16-relay/1",
        })
    try:
        with opener(request, timeout=8) as response:
            payload = response.read(65536)
            return response.status, json.loads(payload or b"[]")
    except urllib.error.HTTPError as error:
        payload = error.read(4096)
        try:
            parsed = json.loads(payload or b"{}")
        except (json.JSONDecodeError, UnicodeDecodeError):
            parsed = {"error": "non-json response"}
        return error.code, parsed


def job_payload(boot, job):
    job_id = str(job.get("job-id", ""))
    kind = job.get("kind")
    input_value = job.get("input")
    prompt = input_value.get("prompt") if isinstance(input_value, dict) else None
    model = input_value.get("model") if isinstance(input_value, dict) else None
    if not re.fullmatch(r"[0-9]{1,20}", job_id) or kind != MURAKUMO_JOB_KIND:
        return None
    if model != MURAKUMO_JOB_MODEL or micro_infer_expected(prompt) is None:
        return None
    return (f"AIUEOS_JOB_V1 boot={boot} id={job_id} kind={kind} "
            f"prompt={prompt.encode('ascii').hex()}").encode("ascii")


def committed_payload(boot, job_id):
    if not re.fullmatch(r"[0-9a-f]{16}", boot or "") or \
            not re.fullmatch(r"[0-9]{1,20}", str(job_id)):
        return None
    return (f"AIUEOS_JOB_COMMIT_V1 boot={boot} id={job_id} "
            "state=recorded").encode("ascii")


def node_ping_payload(boot, sequence):
    if not re.fullmatch(r"[0-9a-f]{16}", boot or "") or \
            not isinstance(sequence, int) or not (0 <= sequence <= 0xffffffff):
        return None
    return f"AIUEOS_NODE_PING_V1 boot={boot} seq={sequence}".encode("ascii")


def verified_node_pong(message, boot, sequence):
    match = NODE_PONG.fullmatch(message)
    return bool(match and match.group(1) == boot and
                int(match.group(2)) == sequence)


def micro_infer_expected(prompt):
    if not isinstance(prompt, str) or not (1 <= len(prompt) <= 64) or \
            not re.fullmatch(r"[ a-z]+", prompt):
        return None
    return MURAKUMO_MICRO_INFER_ROWS.get(prompt[-1])


def verified_job_result(message, boot, job_id, prompt):
    match = JOB_RESULT.fullmatch(message)
    if not match:
        return None
    got_boot, got_id, token_hex, score, total = match.groups()
    if got_boot != boot or got_id != str(job_id):
        return None
    token = bytes.fromhex(token_hex).decode("ascii", "strict")
    expected = micro_infer_expected(prompt)
    if expected is None or (token, int(score), int(total)) != expected:
        return None
    return {"text": token, "model": MURAKUMO_JOB_MODEL,
            "score": int(score), "total": int(total),
            "prompt": prompt,
            "corpus-sha256": MURAKUMO_JOB_CORPUS_SHA256,
            "boot": boot}


def dispatch_murakumo_job(
        boot, job, sock, peer, opener=urllib.request.urlopen,
        result_queue=MURAKUMO_JOB_RESULTS, sleeper=time.sleep):
    job_id = str(job.get("job-id", "")) if isinstance(job, dict) else ""
    payload = job_payload(boot, job or {})
    if not payload:
        return {"state": "failed", "stage": "job-admission",
                "job-id": job_id}
    prompt = job["input"]["prompt"]
    claim_status, _ = murakumo_post(
        f"/infer/queue/{job_id}/claim", {"did": MURAKUMO_NODE_DID}, opener)
    if claim_status != 201:
        return {"state": "raced" if claim_status == 409 else "failed",
                "stage": "claim", "status": claim_status,
                "job-id": job_id}
    for _ in range(5):
        sock.sendto(payload, peer)
        sleeper(0.2)
    deadline = time.monotonic() + 30
    output = None
    while output is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return {"state": "failed", "stage": "k16-result-timeout",
                    "job-id": job_id}
        try:
            candidate, candidate_peer = result_queue.get(timeout=remaining)
        except queue.Empty:
            return {"state": "failed", "stage": "k16-result-timeout",
                    "job-id": job_id}
        if candidate_peer == peer:
            output = verified_job_result(candidate, boot, job_id, prompt)
    result_status, _ = murakumo_post(
        f"/infer/queue/{job_id}/result",
        {"did": MURAKUMO_NODE_DID, "output": output, "ms": 0}, opener)
    if result_status != 201:
        return {"state": "failed", "stage": "result", "status": result_status,
                "job-id": job_id}
    ready_status, _ = murakumo_post(
        f"/infer/nodes/{MURAKUMO_NODE_NAME}/heartbeat",
        murakumo_heartbeat(True), opener)
    if ready_status != 201:
        return {"state": "failed", "stage": "ready-heartbeat",
                "status": ready_status, "job-id": job_id}
    commit = committed_payload(boot, job_id)
    for _ in range(5):
        sock.sendto(commit, peer)
        sleeper(0.2)
    return {"state": "ready", "boot": boot, "job-id": job_id,
            "model": MURAKUMO_JOB_MODEL, "token": output["text"],
            "prompt": prompt}


def qualify_murakumo_job(message, sock, peer, opener=urllib.request.urlopen,
                         result_queue=MURAKUMO_JOB_RESULTS, sleeper=time.sleep):
    match = NODE_HELLO.fullmatch(message)
    if not match or not MURAKUMO_JOB_QUALIFICATION:
        return {"state": "disabled", "reason": "job-qualification-off"}
    boot, mac = match.groups()
    if mac != MURAKUMO_EXPECTED_MAC or not murakumo_relay_configured():
        return {"state": "disabled", "reason": "identity-token-or-mac"}
    last_race = None
    for _attempt in range(8):
        enqueue_status, enqueue = murakumo_post(
            "/infer/queue",
            {"kind": MURAKUMO_JOB_KIND,
             "input": {"model": MURAKUMO_JOB_MODEL,
                       "prompt": MURAKUMO_JOB_PROMPT},
             "price": 0}, opener)
        job_id = str(enqueue.get("job-id", "")) \
            if isinstance(enqueue, dict) else ""
        if enqueue_status != 201 or not re.fullmatch(r"[0-9]{1,20}", job_id):
            return {"state": "failed", "stage": "enqueue",
                    "status": enqueue_status}
        list_status, jobs = murakumo_get("/infer/queue", opener)
        job = next((candidate for candidate in jobs
                    if str(candidate.get("job-id", "")) == job_id), None) \
            if list_status == 200 and isinstance(jobs, list) else None
        if not job_payload(boot, job or {}):
            return {"state": "failed", "stage": "queue-observe",
                    "status": list_status, "job-id": job_id}
        result = dispatch_murakumo_job(
            boot, job, sock, peer, opener, result_queue, sleeper)
        if result.get("state") != "raced":
            return result
        last_race = result
    return last_race or {"state": "failed", "stage": "claim-retry"}


def resume_murakumo_job(boot, sock, peer):
    hello = (f"AIUEOS_NODE_HELLO_V1 boot={boot} "
             f"mac={MURAKUMO_EXPECTED_MAC} profile=rtl8125-relay-test")
    result = qualify_murakumo_job(hello, sock, peer)
    fields = " ".join(f"{key}={value}" for key, value in result.items())
    print(f"AIUEOS_MURAKUMO_RESUME {fields}", flush=True)
    if result.get("state") == "ready":
        threading.Thread(
            target=maintain_murakumo_liveness,
            args=(boot, sock, peer), daemon=True).start()


def maintain_murakumo_liveness(
        boot, sock, peer, opener=urllib.request.urlopen,
        result_queue=MURAKUMO_LIVENESS_RESULTS, sleeper=time.sleep,
        rounds=None, interval=30, timeout=10,
        job_result_queue=MURAKUMO_JOB_RESULTS):
    sequence = 1
    renewed = 0
    executed = 0
    while rounds is None or renewed < rounds:
        queue_status, jobs = (200, []) if renewed == 0 else \
            murakumo_get("/infer/queue", opener)
        if queue_status != 200 or not isinstance(jobs, list):
            return {"state": "failed", "stage": "queue-poll",
                    "status": queue_status, "boot": boot,
                    "renewed": renewed, "executed": executed}
        job = next((candidate for candidate in jobs
                    if job_payload(boot, candidate)), None)
        if job is not None:
            work = dispatch_murakumo_job(
                boot, job, sock, peer, opener, job_result_queue, sleeper)
            if work.get("state") == "ready":
                renewed += 1
                executed += 1
                print(f"AIUEOS_MURAKUMO_WORK state=recorded boot={boot} "
                      f"job-id={work['job-id']} count={executed}", flush=True)
                if rounds is None or renewed < rounds:
                    sleeper(interval)
                continue
            if work.get("state") != "raced":
                murakumo_post(
                    f"/infer/nodes/{MURAKUMO_NODE_NAME}/heartbeat",
                    murakumo_heartbeat(False), opener)
                return {"state": "failed", "stage": "queued-work",
                        "work-stage": work.get("stage"), "boot": boot,
                        "renewed": renewed, "executed": executed}
        ping = node_ping_payload(boot, sequence)
        if not ping:
            return {"state": "failed", "stage": "liveness-ping",
                    "renewed": renewed, "executed": executed}
        sock.sendto(ping, peer)
        deadline = time.monotonic() + timeout
        valid = False
        while not valid:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                candidate, candidate_peer = result_queue.get(timeout=remaining)
            except queue.Empty:
                break
            valid = candidate_peer == peer and \
                verified_node_pong(candidate, boot, sequence)
        if not valid:
            murakumo_post(
                f"/infer/nodes/{MURAKUMO_NODE_NAME}/heartbeat",
                murakumo_heartbeat(False), opener)
            return {"state": "stale", "stage": "liveness-timeout",
                    "boot": boot, "sequence": sequence, "renewed": renewed,
                    "executed": executed}
        status, _ = murakumo_post(
            f"/infer/nodes/{MURAKUMO_NODE_NAME}/heartbeat",
            murakumo_heartbeat(True), opener)
        if status != 201:
            return {"state": "failed", "stage": "heartbeat-renew",
                    "status": status, "boot": boot, "sequence": sequence,
                    "renewed": renewed, "executed": executed}
        renewed += 1
        print(f"AIUEOS_MURAKUMO_LIVENESS state=renewed boot={boot} "
              f"sequence={sequence} count={renewed}", flush=True)
        sequence = (sequence + 1) & 0xffffffff
        if rounds is None or renewed < rounds:
            sleeper(interval)
    return {"state": "live", "boot": boot, "renewed": renewed,
            "executed": executed}


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


def relay_murakumo_hello(message, sock=None, peer=None):
    try:
        result = register_murakumo_hello(message)
        fields = " ".join(f"{key}={value}" for key, value in result.items())
        print(f"AIUEOS_MURAKUMO_RELAY {fields}", flush=True)
        if result.get("state") == "live-not-ready" and sock and peer and \
                MURAKUMO_JOB_QUALIFICATION:
            job_result = qualify_murakumo_job(message, sock, peer)
            job_fields = " ".join(
                f"{key}={value}" for key, value in job_result.items())
            print(f"AIUEOS_MURAKUMO_JOB {job_fields}", flush=True)
            if job_result.get("state") == "ready":
                threading.Thread(
                    target=maintain_murakumo_liveness,
                    args=(job_result["boot"], sock, peer), daemon=True).start()
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


def murakumo_preflight(opener=urllib.request.urlopen):
    if not murakumo_relay_configured():
        raise RuntimeError("Murakumo node DID or service token is unavailable")
    if not BOOT_PATH.is_file():
        raise RuntimeError(f"PXE boot image is unavailable: {BOOT_PATH}")
    socket.if_nametoindex(INTERFACE)
    status, jobs = murakumo_get("/infer/queue", opener)
    if status != 200 or not isinstance(jobs, list):
        raise RuntimeError(f"Murakumo queue authentication failed: HTTP {status}")
    return {"did": MURAKUMO_NODE_DID,
            "boot-sha256": artifact_sha256(BOOT_PATH),
            "queued-jobs": len(jobs)}


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
        if not expected_dhcp_client(packet):
            print(f"AIUEOS_PXE_DHCP_IGNORED mac={mac} "
                  f"expected={PXE_EXPECTED_MAC}", flush=True)
            continue
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
    if MURAKUMO_RESUME_BOOT:
        threading.Thread(
            target=resume_murakumo_job,
            args=(MURAKUMO_RESUME_BOOT, sock, (CLIENT_IP, 7779)),
            daemon=True).start()
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
            threading.Thread(target=relay_murakumo_hello,
                             args=(message, sock, peer),
                             daemon=True).start()
        if JOB_RESULT.fullmatch(message) and peer[0] == CLIENT_IP:
            MURAKUMO_JOB_RESULTS.put((message, peer))
        if NODE_PONG.fullmatch(message) and peer[0] == CLIENT_IP:
            MURAKUMO_LIVENESS_RESULTS.put((message, peer))
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
    with tempfile.TemporaryDirectory() as directory:
        private_path = Path(directory) / "credential"
        private_path.write_text("bounded-value\n", encoding="ascii")
        private_path.chmod(0o600)
        assert private_file_text(private_path, "selftest credential", 32) == \
            "bounded-value"
        private_path.chmod(0o640)
        try:
            private_file_text(private_path, "selftest credential", 32)
            raise AssertionError("group-readable credential accepted")
        except RuntimeError:
            pass

    request = bytearray(240)
    request[0:4] = bytes((1, 1, 6, 0))
    request[4:8] = b"TEST"
    request[28:34] = bytes.fromhex(PXE_EXPECTED_MAC.replace(":", ""))
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
    assert expected_dhcp_client(bytes(request))
    unexpected_request = bytearray(request)
    unexpected_request[28:34] = b"\x2c\x9e\x00\x72\x74\x16"
    assert not expected_dhcp_client(bytes(unexpected_request))

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
            def __init__(self, status, body=b"{}"):
                self.status = status
                self.body = body
            def read(self, _limit):
                return self.body
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

        globals()["MURAKUMO_JOB_QUALIFICATION"] = True
        job_captured = []
        class FakeSocket:
            def __init__(self):
                self.sent = []
            def sendto(self, payload, peer):
                self.sent.append((payload, peer))
        fake_socket = FakeSocket()
        fake_results = queue.Queue()
        fake_results.put((
            "AIUEOS_JOB_RESULT_V1 boot=0123456789abcdef id=209 "
            "model=aiueos-char-bigram-v1 token=6f score=2 total=5",
            (CLIENT_IP, 7779)))
        def fake_job_open(request, timeout):
            job_captured.append((request.full_url, request.method, request.data,
                                 request.get_header("Authorization"), timeout))
            if request.method == "GET":
                return FakeResponse(200, json.dumps([{
                    "job-id": "209", "kind": MURAKUMO_JOB_KIND,
                    "input": {"model": MURAKUMO_JOB_MODEL,
                              "prompt": MURAKUMO_JOB_PROMPT},
                    "price": 0}]).encode("utf-8"))
            if request.full_url.endswith("/infer/queue"):
                return FakeResponse(201, b'{"job-id":"209"}')
            return FakeResponse(201)
        qualified = qualify_murakumo_job(
            hello, fake_socket, (CLIENT_IP, 7779), fake_job_open,
            fake_results, lambda _seconds: None)
        assert qualified["state"] == "ready" and qualified["job-id"] == "209"
        assert [entry[1] for entry in job_captured] == \
            ["POST", "GET", "POST", "POST", "POST"]
        assert all(entry[3] == "Bearer selftest-token" for entry in job_captured)
        assert fake_socket.sent[0][0] == \
            b"AIUEOS_JOB_V1 boot=0123456789abcdef id=209 kind=aiueos-micro-infer prompt=6d7572616b756d"
        assert fake_socket.sent[-1][0] == \
            b"AIUEOS_JOB_COMMIT_V1 boot=0123456789abcdef id=209 state=recorded"
        ready_body = json.loads(job_captured[-1][2])
        assert ready_body["node/ready?"] is True
        assert ready_body["node/model"] == MURAKUMO_JOB_MODEL
        assert ready_body["node/capacity"] == {
            "slots-total": 1, "slots-free": 1}
        assert micro_infer_expected("awai") == ("n", 3, 8)
        assert micro_infer_expected("ends-in-b") is None
        race_calls = []
        race_enqueue = [0]
        race_results = queue.Queue()
        race_results.put((
            "AIUEOS_JOB_RESULT_V1 boot=0123456789abcdef id=212 "
            "model=aiueos-char-bigram-v1 token=6f score=2 total=5",
            (CLIENT_IP, 7779)))
        def fake_race_open(request, timeout):
            race_calls.append((request.full_url, request.method))
            if request.method == "GET":
                job_id = str(211 + race_enqueue[0] - 1)
                return FakeResponse(200, json.dumps([{
                    "job-id": job_id, "kind": MURAKUMO_JOB_KIND,
                    "input": {"model": MURAKUMO_JOB_MODEL,
                              "prompt": MURAKUMO_JOB_PROMPT},
                    "price": 0}]).encode("utf-8"))
            if request.full_url.endswith("/infer/queue"):
                job_id = str(211 + race_enqueue[0])
                race_enqueue[0] += 1
                return FakeResponse(201, json.dumps(
                    {"job-id": job_id}).encode("utf-8"))
            if request.full_url.endswith("/211/claim"):
                return FakeResponse(409)
            return FakeResponse(201)
        retried = qualify_murakumo_job(
            hello, fake_socket, (CLIENT_IP, 7779), fake_race_open,
            race_results, lambda _seconds: None)
        assert retried["state"] == "ready" and retried["job-id"] == "212"
        assert sum(url.endswith("/claim") for url, _method in race_calls) == 2
        liveness_results = queue.Queue()
        liveness_results.put((
            "AIUEOS_NODE_PONG_V1 boot=0123456789abcdef seq=1 state=ready",
            (CLIENT_IP, 7779)))
        worker_results = queue.Queue()
        worker_results.put((
            "AIUEOS_JOB_RESULT_V1 boot=0123456789abcdef id=210 "
            "model=aiueos-char-bigram-v1 token=6e score=3 total=8",
            (CLIENT_IP, 7779)))
        liveness_captured = []
        liveness_gets = [0]
        def fake_liveness_open(request, timeout):
            liveness_captured.append((request.full_url, request.method,
                                      request.data, timeout))
            if request.method == "GET":
                liveness_gets[0] += 1
                jobs = [{"job-id": "210", "kind": MURAKUMO_JOB_KIND,
                         "input": {"model": MURAKUMO_JOB_MODEL,
                                   "prompt": "awai"}, "price": 1}] \
                    if liveness_gets[0] == 1 else []
                return FakeResponse(200, json.dumps(jobs).encode("utf-8"))
            return FakeResponse(201)
        liveness = maintain_murakumo_liveness(
            "0123456789abcdef", fake_socket, (CLIENT_IP, 7779),
            fake_liveness_open, liveness_results, lambda _seconds: None,
            rounds=2, interval=0, timeout=0.1,
            job_result_queue=worker_results)
        assert liveness == {"state": "live", "boot": "0123456789abcdef",
                            "renewed": 2, "executed": 1}
        assert any(payload ==
                   b"AIUEOS_JOB_V1 boot=0123456789abcdef id=210 kind=aiueos-micro-infer prompt=61776169"
                   for payload, _peer in fake_socket.sent)
        assert any(payload ==
                   b"AIUEOS_NODE_PING_V1 boot=0123456789abcdef seq=1"
                   for payload, _peer in fake_socket.sent)
        assert fake_socket.sent[-1][0] == \
            b"AIUEOS_JOB_COMMIT_V1 boot=0123456789abcdef id=210 state=recorded"
        assert [entry[1] for entry in liveness_captured] == \
            ["POST", "GET", "POST", "POST", "POST"]
        assert len(job_captured) == 5
    finally:
        globals()["MURAKUMO_NODE_DID"] = old_did
        globals()["MURAKUMO_SERVICE_TOKEN"] = old_token
        globals()["MURAKUMO_JOB_QUALIFICATION"] = False
        MURAKUMO_SEEN_BOOTS.clear()
    try:
        control_payload("reboot", "0123456789abcdef")
        raise AssertionError("unsupported control command accepted")
    except ValueError:
        pass
    print("AIUEOS_PXE_SELFTEST_OK dhcp=pxe+http+mac-bound tftp=oack control=token-bound "
          "node-relay=request-bound murakumo=qualify+poll+claim+result+renew "
          "interface-bound=yes")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--control", choices=CONTROL_COMMANDS)
    parser.add_argument("--nonce")
    parser.add_argument("--next-boot")
    parser.add_argument("--resume-boot")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return
    if args.preflight:
        result = murakumo_preflight()
        print("AIUEOS_MURAKUMO_PREFLIGHT_OK " +
              " ".join(f"{key}={value}" for key, value in result.items()))
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
    if args.resume_boot:
        if not re.fullmatch(r"[0-9a-f]{16}", args.resume_boot):
            raise SystemExit("resume boot must be 16 lowercase hex digits")
        globals()["MURAKUMO_RESUME_BOOT"] = args.resume_boot
    if not BOOT_PATH.is_file():
        raise SystemExit(f"missing boot file: {BOOT_PATH}")
    threading.Thread(target=tftp_server, daemon=True).start()
    threading.Thread(target=http_server, daemon=True).start()
    threading.Thread(target=netlog_server, daemon=True).start()
    dhcp_server()


if __name__ == "__main__":
    main()
