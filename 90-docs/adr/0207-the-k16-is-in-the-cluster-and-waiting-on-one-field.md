# ADR 0207: the K16 is in the inference cluster, and one field is between it and membership

Status: accepted. Date: 2026-09-08. Related: ADR-0202 (trust tiers), ADR-0205.

## Measured, not assumed

`GET /infer/nodes` on `api.murakumo.cloud`, authorized with the node's own
CACAO, 2026-09-08 evening. 66 nodes.

The K16's live record (`gmktec-k16-lan2`) is complete and correct:

```
liveness "fresh"   heartbeat-age-ms 553   live? true   enrolled? true
tier "native"      can ["aiueos-micro-infer"]
caps {engine "aiueos-native", qualification-model "aiueos-char-bigram-v1",
      physical-network "rtl8125-unisolated-qualification"}
connect "mac-relay"   needs-relay? true
trust-tier "community"
admission "pending"        <- the only gap
ready? false
```

Enrollment, heartbeat, capability, model and relay requirement all land. **Only
four nodes in the whole 66-node cluster are live** -- `dan`, `judah`, `levi`,
and this board. The other three are `accepted` and `ready? true`; the K16 is
`pending` and therefore not `ready?`.

## The boundary is real, and it was measured rather than inferred

| trust-tier | admission | count |
|---|---|---|
| `awai-secure` | **accepted** | **10** |
| `community` | pending | 9 |
| `community` | (absent) | 47 |

**No `community` node has ever been accepted -- 0 of 56.** All ten accepted
nodes carry `provider: did:web:awai.network`. So `community` is not a lower
rung of the same ladder that a node climbs by behaving well; it is the tier a
node can put itself in, and admission is the tier an operator puts it in.

## What makes this actionable rather than a wall

**The K16 already holds an admitted record.** `gmktec-k16` is
`awai-secure` / `accepted`, `provider did:web:awai.network`, and its `can` is
`["aiueos-micro-infer"]` -- this board's own capability, not a large-model
node's. It is stale (heartbeat 9.9 days old) because the live relay enrolls
under a different name and a different key:

| | did | provider | admission |
|---|---|---|---|
| `gmktec-k16` | `z6Mkn4C92R…` | `did:web:awai.network` | accepted |
| `gmktec-k16-lan2` | `z6Mkpqczt…` | itself | pending |

So the board is not waiting on a new judgement about whether an aiueos node
belongs in the cluster. That judgement was already made for this board. It is
waiting on the current identity being the one the operator vouches for.

## What is NOT the answer

**Do not move the relay onto the admitted name.** `~/.gftd/` holds only the
`lan2` key; the admitted record's key is not here, and enrolling under another
identity's name would be assuming it rather than being granted it -- which is
the precise thing a trust tier exists to prevent. ADR-0202's boundary
(`community` self-authorizable, `awai-secure` operator-only) is the reason the
gap exists, and routing around it would make the tier meaningless.

**Do not read `admission: pending` as a fault to fix in this repository.**
Nothing in `k16-pxe-server.py` can or should change it; the string appears in
that file exactly once, in an unrelated job-stage error.

## The decision, which is the owner's

Either `gmktec-k16-lan2` is promoted to `awai-secure` with
`provider did:web:awai.network`, or the K16 stays a `community`-tier enrolled
node -- live, heart-beating, capability-declared, and dispatched jobs of its
declared kind -- which is a coherent end state and may be the right one for a
board on an unisolated qualification network.

Until that is decided, "the K16 is a member of the inference cluster" is true
in every sense the cluster measures except `admission`, and saying it without
that qualifier would be an overclaim.
