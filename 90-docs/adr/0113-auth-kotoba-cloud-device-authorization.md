# ADR-0113 — `auth.kotoba.cloud` authorizes the account; the node proves its own key

Accepted for the hosted Kotoba Browser adapter on 2026-08-29. This decision
implements the AIUEOS side of the device authorization contract. It does not
claim the production authority is deployed, a real Passkey ceremony has run,
or the physical K16 has completed the flow.

## Context

AIUEOS needs two user experiences — continue with a Passkey on the current
screen or scan with a phone — without creating two identity systems. The OS
must add a device to an account, but must never receive or copy the account's
Passkey private key. A browser response such as `verified=true` is not an
authentication result.

The formal authority, origin and WebAuthn RP ID are fixed:

- authority and origin: `https://auth.kotoba.cloud`
- RP ID: `auth.kotoba.cloud`
- device-flow start: `POST /v1/aiueos/device/start`
- device-flow poll: `POST /v1/aiueos/device/poll`

`auth.itonami.cloud` and generic OIDC are not alternate OS-auth routes.
The `/v1/aiueos/device/*` namespace is also deliberate: the authority already
serves a separate generic Device Code contract at `/v1/device/*`, which AIUEOS
must neither replace nor reinterpret.

## Decision

1. AIUEOS mints a 256-bit challenge and starts a five-minute authority flow
   bound to the exact device DID, model, method and challenge.
2. The authority returns a public flow ID and approval URL plus a separate
   high-entropy poll token. AIUEOS keeps the poll token only in helper memory.
3. Passkey opens the approval URL. Phone scan locally renders the same URL as a
   QR. Neither path exposes the poll token, enrollment bearer token, account
   key, Passkey material or device private key.
4. The helper polls the authority directly. Client-supplied verification flags
   are ignored. A grant is admissible only when the result binds the exact
   flow, device DID, challenge, model, method, authority, RP ID, verified
   account principal/DID and an active Passkey-backed DID, with user presence
   and user verification.
5. A valid account result is not yet a node claim. AIUEOS signs the challenge
   with the device-owned Ed25519 key and verifies that proof locally. Only then
   does it persist a non-secret account/device receipt and project the node
   plan.
6. A successful authority result is consumed before local persistence. Replay,
   expiry, mismatched binding and missing local key possession fail closed.

The hosted helper uses a fixed-origin Java HTTP client with redirects disabled,
bounded timeouts and a 64 KiB response limit. QR rendering uses the pinned
`io.nayuki/qrcodegen` 1.8.0 jar locally; no third-party QR service sees a live
approval flow. The adapter, state machine and QR dependency are all recorded in
the TCB inventory.

## Evidence and limits

The focused suite injects the authority port and proves start, pending, grant,
one-time consumption, exact-origin/device refusal, local device-key proof, and
absence of the poll secret from HTTP responses, QR output and persisted state.
It also proves that browser-supplied `verified=true` cannot turn a pending
authority result into a grant.

This is hosted JVM evidence. It does not change `KERNEL.ELF`, qualify the K16,
install the SSD, activate Kekkai, mark Murakumo workload readiness, or start
storage replication. Each of those requires its own runtime or physical proof.
The production integration remains red until the authority routes are deployed
and the full ceremony is completed by a person with a real Passkey.
