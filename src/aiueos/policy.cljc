(ns aiueos.policy
  "The policy reasoner, ported from the retired `aiueos/src/policy.rs` Rust
  module to CLJC per ADR-2607022200 (aiueos's semantic authority moved from a
  Rust crate to CLJC/EDN; this namespace is the executable decision model the
  `:aiueos.policy/*` shape in `resources/aiueos/policy_contract.edn` and
  `aiueos.contract/validate-policy-decision` describe).

  Given a capability graph (`aiueos.graph`, who exports what) and a policy
  (kernel-provided primitives, per-component grants, per-trust
  forbiddances), `verify-component` decides whether a component is allowed
  to run and which capabilities it is actually granted. The output is always
  a policy-decision map (`:aiueos/decision :grant` or `:deny`) — never a
  silent pass."
  (:require [clojure.set :as set]
            [aiueos.graph :as graph]
            [aiueos.surface :as surface]))

(def default-kernel-caps
  "Primitive capabilities the kernel/broker hands out directly (no exporter
  component needed). These are the hardware/runtime seams. Mirrors
  `resources/aiueos/policy_contract.edn` :aiueos.policy/kernel-caps."
  #{:log/write :clock/monotonic :random/bytes
    :topic/publish :topic/subscribe
    :pci/config :dma/map :irq/subscribe :mmio/map})

(def default-forbid-effects
  "Effects forbidden for a given trust level. The AI-generated/untrusted
  lockdown: no network, no secrets, no persistence for :ai-generated; no
  secrets for :untrusted. Mirrors :aiueos.policy/forbid."
  {:ai-generated #{:network :secrets :persistent-write}
   :untrusted #{:secrets}})

(def default-policy
  "The default policy: a conservative set of kernel primitives, and the
  AI-generated/untrusted lockdown."
  {:aiueos.policy/kernel-caps default-kernel-caps
   :aiueos.policy/grants {}
   :aiueos.policy/forbid-effects default-forbid-effects
   :aiueos.policy/signers {}
   :aiueos.policy/require-signed false
   :aiueos.policy/surface nil
   :aiueos.policy/net-allow #{}})

(defn- as-kw-set [x]
  (cond
    (nil? x) #{}
    (set? x) x
    (coll? x) (set x)
    :else #{x}))

(defn parse-policy
  "Parse a deployment policy overlay (the `:aiueos/*` EDN validated by
  `aiueos.contract/validate-deployment-policy`) into an effective policy.
  Everything is optional and *extends* the default policy: kernel-caps and
  net-allow are unioned, grants are merged per-component, forbid is
  *replaced* per-trust (an explicit `:aiueos/forbid` entry for a trust level
  overrides — not adds to — the default lockdown for that level, matching
  the retired `Policy::from_edn`), signers are merged.

  Callers should validate the overlay shape with
  `aiueos.contract/validate-deployment-policy` first; this function does not
  re-check unknown keys."
  ([] default-policy)
  ([overlay]
   (let [overlay (or overlay {})]
     (cond-> default-policy
       (:aiueos/kernel-caps overlay)
       (update :aiueos.policy/kernel-caps set/union (as-kw-set (:aiueos/kernel-caps overlay)))

       (:aiueos/grants overlay)
       (update :aiueos.policy/grants
               (fn [grants]
                 (reduce-kv (fn [acc id caps]
                              (update acc id set/union (as-kw-set caps)))
                            grants
                            (:aiueos/grants overlay))))

       (:aiueos/forbid overlay)
       (update :aiueos.policy/forbid-effects merge (:aiueos/forbid overlay))

       (:aiueos/signers overlay)
       (update :aiueos.policy/signers merge (:aiueos/signers overlay))

       (contains? overlay :aiueos/require-signed)
       (assoc :aiueos.policy/require-signed (boolean (:aiueos/require-signed overlay)))

       (:aiueos/surface overlay)
       (assoc :aiueos.policy/surface (name (:aiueos/surface overlay)))

       (:aiueos/net-allow overlay)
       (update :aiueos.policy/net-allow set/union (as-kw-set (:aiueos/net-allow overlay)))))))

(defn granted-to
  "Capabilities available to manifest `m`: kernel primitives ∪ explicit
  grants. With an active surface (ADR-0005), the kernel primitives are
  restricted to those the surface can actually back — an import that maps to
  an unoffered kernel cap becomes :unresolved-capability (the host refuses
  to provide what this surface shouldn't). Explicit grants are never
  surface-gated.

  KNOWN GAP, NOT FIXED (security audit 2026-07-13, tracked as a follow-up
  requiring an owner design decision -- see PR description /
  90-docs/adr/0011-link-time-capability-enforcement.md's closing note; do
  NOT invent a binding scheme here without that decision): `extra` below is
  looked up by `id` (`m`'s bare, self-declared `:aiueos/component`) alone,
  with NO binding to signer identity, even when `:aiueos.policy/require-signed`
  is true and `m` carries a valid `:aiueos/signature`
  (`aiueos.broker/authenticate` resolves a valid signature to a `signer` id,
  but that `signer` never flows into this lookup). `:aiueos.policy/signers`
  is a flat registry: ANY registered signer can produce a validly-signed
  manifest claiming ANY `:aiueos/component` id, including one this policy's
  `:aiueos.policy/grants` map has elevated privileges for — a compromised or
  malicious registered signer (not necessarily the one 'intended' to own
  that component id) can sign a manifest claiming a privileged id and
  receive that id's full grant. Signing today proves 'some registered
  signer vouches for these exact bytes under this id', not 'the signer
  authorized to speak for this id vouches for it'. A correct fix needs a
  new policy shape binding component ids to authorized signer ids (with a
  real decision on default behavior for ids with no such binding declared,
  and interaction with unsigned components under a non-require-signed
  policy) -- multiple reasonable shapes exist and picking one without owner
  input risks encoding the wrong default into every existing policy file."
  [policy m]
  (let [active-surface (:aiueos.policy/surface policy)
        kernel-caps (:aiueos.policy/kernel-caps policy)
        base (if-let [offered (and active-surface (surface/offered-by-id active-surface))]
               (set/intersection kernel-caps offered)
               kernel-caps)
        id (:aiueos/component m)
        extra (get (:aiueos.policy/grants policy) id #{})]
    (set/union base extra)))

(defn- violation
  ([component kind message]
   {:aiueos/component component :aiueos/kind kind :aiueos/message message}))

(def dma-family-imports
  "Kernel-cap import ids that structurally require the ADR-0001 DMA/IOMMU
  gate -- the device-access quartet from `default-kernel-caps`
  (`:pci/config`/`:dma/map`/`:irq/subscribe`/`:mmio/map`). A component whose
  `:aiueos/imports` contains ANY of these needs the gate, REGARDLESS of
  whether it also self-declares `:aiueos/effects #{:dma}` -- see
  `verify-component`'s `dma?` for why: `:aiueos/effects` alone was a
  self-declared, unenforced field with no structural link to the actual
  capability being requested (security audit, 2026-07-13) -- a manifest
  could import e.g. `:dma/map` while simply omitting
  `:aiueos/effects #{:dma}`, silently skipping the gate."
  #{:pci/config :dma/map :irq/subscribe :mmio/map})

(defn verify-component
  "Verify one component manifest `m` against `graph` (an `aiueos.graph/build`
  result) and `policy` (an effective policy from `parse-policy`). Returns a
  policy-decision map matching `aiueos.contract/validate-policy-decision`:
  `{:aiueos/decision :grant :aiueos/component id :aiueos/capabilities #{...}}`
  on success, or `{:aiueos/decision :deny :aiueos/component id
  :aiueos/violations [...]}` listing every violation (never just the first).

  The ADR-0001 DMA/IOMMU gate (`:dma-without-iommu`) fires when EITHER `m`
  self-declares `:aiueos/effects #{:dma}` OR `:aiueos/imports` contains any
  `dma-family-imports` id -- not effects alone. A manifest cannot skip the
  gate by simply omitting `:aiueos/effects #{:dma}` while still importing
  `:dma/map`/`:pci/config`/`:mmio/map`/`:irq/subscribe`."
  [m graph policy]
  (let [id (:aiueos/component m)
        granted (granted-to policy m)
        active-surface (:aiueos.policy/surface policy)
        targets-present? (contains? m :aiueos/surface)
        targets (as-kw-set (:aiueos/surface m))
        surface-violations
        (if (and active-surface targets-present? (not (contains? targets (keyword active-surface))))
          [(violation id :surface-mismatch
                      (str "component targets surfaces " targets
                           " but the active surface is " active-surface))]
          [])
        imports (as-kw-set (:aiueos/imports m))
        {:keys [resolved import-violations]}
        (reduce (fn [acc imp]
                  (let [by-graph (some #(not= % id) (graph/providers graph imp))
                        by-grant (contains? granted imp)]
                    (if (or by-graph by-grant)
                      (update acc :resolved conj imp)
                      (update acc :import-violations conj
                              (violation id :unresolved-capability
                                         (str "import " imp
                                              " has no provider, kernel cap, or grant"))))))
                {:resolved #{} :import-violations []}
                imports)
        effects (as-kw-set (:aiueos/effects m))
        trust (or (:aiueos/trust m) :untrusted)
        forbidden (get (:aiueos.policy/forbid-effects policy) trust #{})
        effect-violations
        (for [eff effects :when (contains? forbidden eff)]
          (violation id :forbidden-effect
                     (str "effect " eff " is forbidden for " (name trust) " components")))
        requires (as-kw-set (:aiueos/requires m))
        dma-by-effect? (contains? effects :dma)
        dma-by-import? (boolean (seq (set/intersection imports dma-family-imports)))
        dma? (or dma-by-effect? dma-by-import?)
        requires-iommu? (contains? requires :iommu)
        has-iommu? (or (contains? granted :iommu) (contains? resolved :iommu))
        dma-violations
        (if (and dma? (not (and requires-iommu? has-iommu?)))
          [(violation id :dma-without-iommu
                      (if dma-by-import?
                        (str "DMA-family import(s) "
                             (set/intersection imports dma-family-imports)
                             " require `:requires #{:iommu}` and an :iommu grant"
                             (when-not dma-by-effect?
                               " (this manifest never declared :aiueos/effects #{:dma} either)"))
                        "DMA requires `:requires #{:iommu}` and an :iommu grant"))]
          [])
        violations (vec (concat surface-violations import-violations effect-violations dma-violations))]
    (if (seq violations)
      {:aiueos/decision :deny :aiueos/component id :aiueos/violations violations}
      (let [caps (cond-> resolved
                   (and requires-iommu? (contains? granted :iommu)) (conj :iommu))]
        {:aiueos/decision :grant :aiueos/component id :aiueos/capabilities caps}))))

(defn verify-system
  "Verify every component in `components` (a vector of manifest maps) against
  a shared capability graph built from all of them. Returns a vector of
  policy-decision maps, one per component, in input order."
  [components policy]
  (let [g (graph/build components)]
    (mapv #(verify-component % g policy) components)))
