# ADR-0041 — aiueos is a cloud-premised OS: the local plane boots and brokers, the cloud plane stores and infers

Date: 2026-08-18

## Status

Accepted as positioning. It names an ordered gap ledger and implements none of
it. Extends ADR-0019 (which split boot authority from workload authority) and
ADR-0030 (origin allowlists). It does not decide install-to-internal-disk.

## Context

The intent is the Chrome-desktop shape: the machine boots and installs its own
verified system, and storage and inference live in the cloud. Two of that
shape's three legs do not exist yet. Measured on `origin/main` `d2ba6e7`:

**Boot.** The C-free native path builds `BOOTX64.EFI` and `KERNEL.ELF`; USB
removable boot is proved by transport equivalence — the same image booted over
`disk` and `usb` must produce byte-identical aiueos evidence (ADR-0019, 33
identical lines, `PciRoot(0x0)/Pci(0x2,0x0)/USB(0x0,0x0)`). That evidence is
**OVMF-under-QEMU only**; no physical machine has booted this image. And there
is no install path: `os/aiueos/scripts/flash-usb.cljs` writes a release image
to a *removable* device and refuses internal disks by design. Today "boot
install" means "flash a stick and boot it", not "install onto the machine".

**Network.** Bare metal has the virtio-net link layer (ADR-0020), ARP + IPv4 +
ICMP (ADR-0021), one TCP connection that completes (ADR-0022), and RFC 9293
§3.10.7.4 segment acceptance (`7182047`). It has **no DHCP, no DNS resolver, no
TLS, and no HTTP client**. `src/aiueos/net.cljc` is 41 lines of URL allowlist
for *host* adapters; `:net/fetch` is policy, not a bare-metal provider.

**Storage.** `aiueos.sealed-audit` and `aiueos.sealed-state` are local
AES-256-GCM sealed stores keyed by a KMS/HSM adapter; bare metal has an aiuefs
data partition. There is no client for any remote store in this repository.

**Inference.** None. `os/aiueos/kotoba/murakumo-join-plan.kotoba` *decides*
murakumo participation — 777 parity assertions against `murakumo.infer.join` —
and cannot act on it. ADR-0019 named that gap rather than letting the phrase
"AI mining server OS" imply serving.

Recording the positioning now matters because the difference between a
cloud-premised OS and a local OS is settled at the capability boundary, not at
the end.

## Decision

1. **aiueos is cloud-premised.** The local plane owns exactly: firmware
   handoff, memory/paging/W^X, scheduling, processes and syscalls, device
   admission, the capability broker and its policy, device identity and
   enrolment, and enough network to reach a named origin. Everything above that
   is a client of a remote plane.

2. **Storage authority is remote and content-addressed** — kotobase
   (`net-kotobase`, `kotobase.net`). Local storage is a cache plus the sealed
   state that must survive with no network: key material, enrolment receipts,
   the audit chain head, and the last admitted catalog. Apply the root deletion
   test (ADR-2608039000): erase the local disk, and a re-enrolled machine must
   come back. Anything that fails that test is a local premise and needs its own
   ADR saying so.

3. **Inference authority is remote** — murakumo (`api.murakumo.cloud`). aiueos
   never hardcodes a model id (root CLAUDE.md / ADR-2607173100): it resolves the
   `murakumo-main` alias, or takes an endpoint and follows whatever that
   endpoint serves. No inference runtime is admitted into the bare-metal
   profile.

4. **The seam is the existing capability broker, not a new subsystem.** Cloud
   storage and cloud inference arrive as network-reaching capabilities under the
   ADR-0030 origin allowlist, deny-by-default, one grant per origin. There is no
   ambient cloud, and a machine with no grant reaches nothing.

5. **Offline behaviour is a stated property, not a leftover.** A machine with
   its uplink down must still: boot to a verified kernel, verify its own
   enrolment, refuse admission of anything not already admitted, and keep
   appending to its local audit chain. Everything else may fail closed. This
   floor is a gate — boot with the NIC absent and require the terminal state —
   not a sentence in a README.

## Ordered gap ledger

What has to exist for the bare-metal profile to actually reach the cloud. Each
step ends in QEMU evidence; each has a spec owner already registered in this
workspace, so none of it starts from a blank page.

| # | Gap | Owner |
|---|---|---|
| 1 | Address configuration (DHCP, or static bound to the enrolment record) | unowned |
| 2 | DNS resolver | `kotoba-lang/org-ietf-dns`, `org-ietf-dnssec` |
| 3 | TCP from one connection to a usable stream: retransmit, window, close | this repo (ADR-0022 continues) |
| 4 | TLS 1.3 client and chain validation | `capability-crypto-tls`, `org-ietf-x509`; `kotoba/x25519.kotoba`, `sha256.kotoba`, `rsa2048.kotoba` already exist natively |
| 5 | HTTP/1.1 client | `capability-http-fetch`, `capability-http-post` |
| 6 | kotobase client: block get/put by CID, ref read | `net-kotobase`; CID verification stays the store's job |
| 7 | murakumo client: alias resolve, then `/v1/messages` | `kotoba-lang/murakumo`; `murakumo-join-plan.kotoba` finally acts on its own decision |

Steps 1–5 are the whole of the work. Once TLS and HTTP exist, 6 and 7 are two
small protocol clients over an already-proved transport. The hosted Linux PID-1
profile (ADR-0011) has steps 1–5 for free and is where the two cloud clients
should be proved first — the same split ADR-0019 made between boot authority
and workload authority.

## Non-decisions

- **Install-to-internal-disk is not decided here.** A cloud-premised OS still
  has to land somewhere. Whether that is A/B partitions on internal storage —
  Chrome's answer, and the natural fit for the OTA admission and device claim
  already landed — or whether the machine stays removable-media-only, is a
  separate ADR with its own evidence. The pull toward persistence is real: OTA
  update classes and enrolment receipts both assume state that survives reboot.
- **"Cloud-premised" is not "single-vendor-premised."** Root ADR-2608039000
  forbids a single vendor's conditional write as a premise on a distributed
  path. The storage plane here is content-addressed, and its ref plane must
  survive losing any one host.

## Consequences

- Read every cloud claim about aiueos against this ADR. Today the machine can
  boot from a stick, decide its participation, and speak TCP to a peer on its
  own segment. It cannot resolve a name, cannot open a TLS connection, and has
  never contacted `kotobase.net` or `api.murakumo.cloud`.
- The local plane's scope is now bounded from above: work that would put a
  durable authority or an inference runtime on the machine needs an ADR that
  argues against this one, rather than arriving as an implementation detail.
- Steps 1–5 are ordinary, well-specified network work with existing spec
  owners. The risk is not difficulty; it is drift — a partial TLS or a resolver
  without validation would pass a smoke test and fail closed only in the field.
