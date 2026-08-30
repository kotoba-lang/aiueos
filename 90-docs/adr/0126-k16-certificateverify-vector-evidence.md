# ADR-0126: Return the refused K16 CertificateVerify public vector

## Status

Accepted (2026-08-30)

## Context

The physical K16 twice returned code 8609 after receiving complete TLS 1.3
server flights on three fresh connections.  ADR-0125 makes that code uniquely
mean that the P-256 CertificateVerify admission refused.  The same Kotoba
verifier admitted a live Murakumo vector and three independent vectors under
QEMU, so reproducing the exact physical input is required.

The ECDSA signature, transcript digest, and certificate public key are all
public handshake evidence.  The device private key, signed Murakumo result,
model bytes, tokens, and account material are unrelated and must not be logged.

## Decision

Immediately before CertificateVerify admission, copy the public vector into
caller-owned stack storage and persist the latest attempt in the dedicated
`AIUEOSTLSCertVerifyEvidence` UEFI variable.  The one-shot PXE control image
reads that public diagnostic record after reset.  When and only when admission
then refuses in the physical direct qualification path, also attempt one
bounded UDP netlog record containing the 64-byte
`r||s` signature, 32-byte digest, and 64-byte uncompressed P-256 coordinates as
lowercase hexadecimal.  Label each of the three bounded attempts.

The generic TLS engine exposes a copy-only 160-byte evidence accessor that is
available only after CertificateVerify and leaf-key parsing.  Pre-admission
copying and pre-admission persistence are required because the defect under
investigation may mutate TLS state or adjacent verifier scratch before
returning failure.  The physical sender uses the existing port-7777 netlog
frame and does not alter success
criteria: only a valid signature followed by Murakumo HTTP 2xx returns code
8160.

## Evidence boundary

This record is diagnostic public data, not authentication, registration, or a
successful HTTPS exchange.  It exists to replay the exact refusal under QEMU
and should be removed from release profiles once the verifier defect is fixed.
