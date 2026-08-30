# ADR-0127: Bound K16 direct TLS to its one-descriptor receive window

## Status

Accepted (2026-08-30)

## Context

The physical K16 repeatedly received a 2,781--2,783 byte Murakumo TLS server
flight, sent no ClientFinished, and returned Device-P256 code 8609.  The
Device-P256 profile added one to every internal TLS stage, so this was actually
receive-pump stage 8.  CertificateVerify was never reached; ADR-0125 and the
first diagnostic interpretation overlooked that transform.

The direct RTL8125 path owns one receive descriptor.  Its advertised 1,792-byte
window allowed the peer to send another TCP segment while AIUEOS was still
decrypting and parsing the first.  The descriptor was not rearmed yet, so the
second segment could not be DMAed.  The receive wait then expired before the
peer's retransmission timer.  QEMU and the host probe did not reproduce this
one-descriptor timing boundary.

## Decision

Advertise a 1,024-byte window on the physical RTL8125 direct path.  Each ACK
therefore re-opens at most one bounded receive slot, and the next descriptor is
rearmed before that ACK is transmitted.  Keep the larger cloud window for the
virtio-net path, which has a different queue mechanism.

Remove the Device-P256 `+1` transform so retained codes name the source stages
directly.  Code 8608 is receive-pump refusal and code 8609 is CertificateVerify
refusal for every direct profile.

## Evidence boundary

The change is source- and QEMU-checked until another physical K16 boot receives
the complete server flight, sends ClientFinished plus the signed POST, obtains
Murakumo HTTP 2xx, and returns 8160.  Only that physical result plus the server
record can prove node registration or report inference timing.
