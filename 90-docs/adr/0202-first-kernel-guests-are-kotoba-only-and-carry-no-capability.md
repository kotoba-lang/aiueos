# ADR-0202 — the first kernel-adjacent namespaces are Kotoba guests, and they carry no capability

- Status: accepted
- Date: 2026-09-07
- `aiueos.topic`, `aiueos.os-update` and `aiueos.model-channel` now compile as
  pure Kotoba guests, alongside their `.cljc` oracles.
- The capability question, answered by measurement: a guest that makes no
  effect declares none, writes none, and compiles with `:effects #{}`.

## Decision

Three namespaces whose decisions are arithmetic and keywords, not bytes, are
now **kotoba-only** — the guest is the component, the `.cljc` stays as the
parity oracle:

| guest | oracle | owns |
|---|---|---|
| `aiueos/topic.kotoba` | `src/aiueos/topic.cljc` | the whole topic bus: publish / latest / take-sample / pending / topic-count / tick / advance |
| `aiueos/os_update.kotoba` | `src/aiueos/os_update.cljc` | health-status, boot-selection, non-regex manifest faults |
| `aiueos/model_channel.kotoba` | `src/aiueos/model_channel.cljc` | continuity-errors (the four sequence-history rules), boot-decision |

All three compile (`amu compile --target wasm32-browser`), all pass
`amu check`, none declares or calls a capability.

## The capability question, measured

The question this ADR answers directly: **does kotoba-only migration now
require writing capabilities?** No — and this is not a loosening, it is the
language working as specified:

1. **Effect inference is the default.** `infer-effects` derives the effect
   row from the body. A namespace that only moves immutable documents
   infers `:effects #{}` — measured on all three guests and on the nine
   earlier guests across org-ietf-{smtp,pop3,imap,ed25519,x25519,ical,cbor},
   mail, mailer.
2. **Capability syntax appears only when an effect exists.** `perform
   :kind/op v` desugars to `cap-call`; the namespace must then declare
   `:capabilities`. Wire IDs are never source vocabulary.
3. **The pure-product profile forbids the declaration outright** — a guest
   compiled under it that declares `:capabilities` is rejected
   (`pure-product profile rejects :capabilities (guest must be effect-free)`).
4. **Unison-shaped, not Unison-identical**: the effect row is statically
   tracked like an Unison ability, but authority stays a named host boundary
   (`:cap/kind` / `:cap/resource` / `:cap/holder`), not a first-class
   ability value. Ambient authority remains permanently excluded
   (ADR-2608650000).

The practical rule this ADR records: **a pure guest is written with zero
capability lines; a guest that touches the world declares exactly the
capabilities it uses, through `perform`, and nothing else.**

## The measured bounds that shaped the port

Each is stated in the guest's own header so the next reader does not
re-derive it:

- `:container-items 32` — a topic queue and the topic count cap at 32; the
  33rd sample traps `document-vector-too-large`, fail-closed, not a silent
  drop. The oracle's EDN maps are unbounded; this is the one honest
  difference, and it is the same bound org-ietf-pop3 states for its body
  channel.
- `max-parameters 5` — booleans travel as an i64 flags vector beside the
  sequences; ladder state travels as one 5-lane vector.
- `document-is-null` has no admitted lowering — absence is read as
  `document-count = 0`.
- CID strings cannot be bounded guest values — the guest receives CID
  EQUALITY as booleans; the sha256 / immutable-url / CID regexes stay in
  the host, where the grant.publisher admission owns them.
- `rem` / `mod` have no admitted lowering — spelled `quot`-and-subtract.

## What stays in the host, deliberately

- `grant.ota/admit`, `grant.update/advance`, `grant.update/rollback-required?`
  — closures over publisher state and clocks. Mechanism.
- `sha256-pattern`, `immutable-url-pattern`, CID/IPNS patterns — regexes.
  A pattern that can match the wire shape can match a great deal else; the
  positions that matter are checked where the bytes live.
- The clock, the socket, the disk — as before.

## Consequences

- The three guests are compiled artifacts with definition CIDs; their
  oracles remain the executable spec, checked by parity, not deleted.
- The bus the runtime executes (`aiueos.execute.cljc`'s `topic-*` host
  functions threading `aiueos.topic`'s atom) is unchanged; the guest now
  speaks the same semantics the host's bus implements, so a component and
  the kernel share one topic vocabulary instead of two hand-copied ones.
- Future ports follow the same test: if a namespace's decisions are
  arithmetic and keywords, it is a guest; if it opens a socket, holds a
  clock or runs a regex over wire bytes, it stays in the host until the
  capability kit for it exists.
