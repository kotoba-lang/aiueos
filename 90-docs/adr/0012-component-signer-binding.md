# ADR-0012 — Binding `:aiueos/component` ids to authorized signers

- Status: proposed (needs owner decision — see "Open decisions" below)
- Date: 2026-07-13

## Context

ADR-0003 added Ed25519 signatures over the identity↔artifact binding
(`<component-id>\n<wasm-sha256>`), verified by `aiueos.signing/verify`
against a flat `:aiueos.policy/signers` registry (`{signer-id -> pubkey-hex}`).
A security audit (2026-07-13, landed alongside ADR-0011) found that this
proves *authenticity of the bytes under a claimed id*, not *that the specific
signer is the one authorized to claim that id*:

- `aiueos.broker/authenticate` resolves a valid signature to a `signer-id`
  and returns it — but that `signer-id` is discarded before reaching
  capability decisions. `aiueos.broker/verify-one` calls
  `aiueos.policy/verify-component m-eff graph policy`, which never receives
  `signer`.
- `aiueos.policy/granted-to` looks up `(:aiueos.policy/grants policy)` by
  `id` (`(:aiueos/component m)`) alone:

  ```clojure
  (defn granted-to [policy m]
    (let [... id (:aiueos/component m)
          extra (get (:aiueos.policy/grants policy) id #{})]
      (set/union base extra)))
  ```

- `:aiueos.policy/signers` is flat: any registered signer's key can produce
  a validly-signed manifest for **any** `:aiueos/component` id, including one
  `:aiueos.policy/grants` elevates with real privilege.

Net effect: under a `:aiueos.policy/require-signed` policy today, a
compromised or malicious *registered* signer — not necessarily the one an
operator intended to own a given component id — can sign a manifest claiming
a privileged id and receive that id's full grant. Signing proves "some
registered signer vouches for these exact bytes under this id," not "the
signer authorized to speak for this id vouches for it."

This is a real gap, but fixing it requires a policy-shape decision with
several reasonable answers and real tradeoffs — ADR-0011 deliberately left it
unfixed rather than guess. This ADR lays out the mechanism and the specific
decisions needed to close it.

## Mechanism (proposed, independent of the open decisions below)

1. Add `:aiueos.policy/component-signers {component-id #{signer-id ...}}` to
   the policy shape (`resources/aiueos/policy_contract.edn`,
   `aiueos.policy/default-policy`) — a set, not a single signer, so key
   rotation and multi-maintainer ids don't require a structural change later
   (add the new signer's id, drop the old one, no schema migration).
2. Thread `signer` from `aiueos.broker/authenticate`'s result into
   `aiueos.policy/verify-component`/`granted-to`. `verify-component`'s
   arity grows to `[m graph policy signer]` (or a single opts map, matching
   this codebase's general preference for explicit positional args over
   growing option maps for a Pretty small, stable set of parameters — see
   existing 3-arg call sites); `broker/verify-one` already computes `signer`
   via `authenticate`, so this is a threading change, not new computation.
3. In `granted-to`, when `id` has an entry in `:aiueos.policy/component-signers`
   (a *bound* id), only apply the id-specific `extra` grant when `signer` is
   a member of that entry's set; a bound id claimed by an unregistered or
   wrong signer gets `base` only (kernel-caps ∩ surface), same as an
   unrecognized id today — not a hard deny, just no elevated grant (matching
   this codebase's existing "ungranted capability → the import simply fails
   to link" fail-closed pattern from ADR-0011, rather than inventing a new
   denial shape here).

## Open decisions (need owner sign-off)

### 1. Default behavior for an UNBOUND id (no `component-signers` entry)

- **(A) Open (recommended default)** — an id with no binding entry behaves
  exactly as today: `extra` applies regardless of signer identity (or lack
  of one, for unsigned components under a non-`require-signed` policy).
  Zero breaking change for every existing policy; the fix is opt-in per id.
  Tradeoff: the specific vulnerability (claim a privileged, unbound id)
  stays open until an operator explicitly enumerates that id's authorized
  signers — doesn't harden anything by default, only when used.
- **(B) Closed under `:aiueos.policy/require-signed`** — once a policy sets
  `require-signed true`, ANY id present in `:aiueos.policy/grants` but
  *absent* from `component-signers` gets `base` only, never `extra`,
  regardless of signature validity. Closes the gap by default for the
  policy flag that's supposed to mean "I want cryptographic guarantees."
  Tradeoff: breaking change for any *existing* `require-signed` policy that
  has ungrants without bindings — those components silently lose their
  elevated grants on upgrade. Given `:aiueos/require-signed` deployments are
  believed to be early/rare today, this is probably low real-world risk, but
  it is a behavior change an operator could be surprised by without a
  migration note.
- **Recommendation**: ship (A) as the default, and add a *separate*, explicit
  policy knob — `:aiueos.policy/require-signer-binding` (default `false`,
  independent of `require-signed`) — that an operator can opt into for (B)'s
  stricter behavior once they've audited their `component-signers` table.
  This avoids forcing the choice repo-wide and lets `require-signed`
  deployments adopt the stricter default on their own schedule.

### 2. Unsigned components under a non-`require-signed` (permissive/dev) policy

Not really in dispute, but stating it explicitly since it interacts with
decision 1: when a component is unsigned and the policy doesn't require
signatures, `signer` is `nil` and there is no identity to bind against — the
existing bare-id `granted-to` lookup should simply continue to apply
unchanged. Requiring signer identity for an unsigned component would be
incoherent (there is nothing to check), so this path is unaffected by
either option A or B above.

### 3. Rejected alternative — namespaced component ids

Considered and NOT recommended: instead of a separate binding table, require
`:aiueos/component` ids to be structurally scoped by owner (e.g.
`<signer-id>/<local-name>`, mirroring npm scoped packages or reverse-DNS
Java package names) so the owning signer is unforgeable by construction —
no table to keep in sync, no way to "forget" a binding. Rejected for now
because it is far more invasive (every existing component id would need
renaming; every manifest-authoring workflow and doc referencing bare ids
would need updating) for a benefit an explicit binding table already
delivers at much lower migration cost. Worth revisiting only if the
binding-table approach proves operationally painful to maintain at scale
(e.g. many ids, frequent rotation) once it has real usage data behind it.

## Consequences (once a decision is made and this is implemented)

- `aiueos.policy/verify-component`/`granted-to` gain a `signer` parameter;
  `aiueos.broker/verify-one` (the only caller with the resolved signer
  already in hand) threads it through for free.
- New policy field `:aiueos.policy/component-signers` (default `{}`, i.e. no
  ids bound, matching option A's open default for anyone not opting in).
- If option 1's recommendation is adopted, a new
  `:aiueos.policy/require-signer-binding` policy field (default `false`).
- `resources/aiueos/policy_contract.edn` and `aiueos.contract`'s
  policy-shape validation need the new field(s) added.
- No change to `aiueos.execute`/ADR-0011's link-time enforcement — this ADR
  only affects which capabilities `verify-component` decides to grant in the
  first place, not how a granted set is subsequently enforced at the wasm
  host-import boundary.
