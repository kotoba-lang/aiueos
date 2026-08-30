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
second segment could not be DMAed.  The 10,000,000-pause receive wait then
expired before the next paced segment or the peer's retransmission timer.  A
physical diagnostic run with the smaller window returned refined code 8631,
confirming timeout rather than TCP-layout, TLS-feed, or ACK-send refusal.  QEMU
and the host probe did not reproduce this one-descriptor timing boundary.

## Decision

Advertise a 1,024-byte window on the physical RTL8125 direct path.  Each ACK
therefore re-opens at most one bounded receive slot, and the next descriptor is
rearmed before that ACK is transmitted.  Keep the larger cloud window for the
virtio-net path, which has a different queue mechanism.

Extend each physical direct receive wait to 500,000,000 pause iterations.  It
remains finite, but now covers Mac scheduling and a peer TCP retransmission
instead of treating a sub-second gap as a failed TLS flight.

Remove the Device-P256 `+1` transform so retained codes name the source stages
directly.  Code 8608 is receive-pump refusal and code 8609 is CertificateVerify
refusal for every direct profile.

## Evidence boundary

A 2026-08-30 physical K16 diagnostic boot of artifact
`78898e94e9d2b085141c520cbad622a41178f230d41dc8d0ca8ad81a373c7153`
returned success code 8150.  The direct bridge observed 381 bytes from K16 and
3,534 bytes from Murakumo, instead of the former ClientHello-only 163 bytes.
This proves the bounded receive change completed the transport-only TLS and
HTTP qualification against `/infer/queue` on the physical RTL8125 path.

It does not prove the exact Qwen model result.  That requires a clean exact
model boot to send ClientFinished plus the signed device POST, obtain Murakumo
HTTP 2xx, and return 8160.  Only that physical result plus the server record can
prove node registration or report inference timing.

During physical diagnosis, codes 8631--8637 refine the former 8608 pump gate as
RX timeout, TCP-segment refusal, TCP-layout refusal, TLS-feed refusal, ACK-send
refusal, premature FIN, and bounded-attempt exhaustion respectively.
