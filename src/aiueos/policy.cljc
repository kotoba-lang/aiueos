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
            [clojure.string :as str]
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
   :aiueos.policy/component-signers {}
   :aiueos.policy/require-signed false
   :aiueos.policy/surface nil
   :aiueos.policy/net-allow #{}})

(defn- as-kw-set [x]
  (cond
    (nil? x) #{}
    (set? x) x
    (coll? x) (set x)
    :else #{x}))

(defn- as-string-set [x]
  (cond
    (nil? x) #{}
    (set? x) (into #{} (map str) x)
    (coll? x) (into #{} (map str) x)
    (string? x) #{x}
    :else #{}))

(defn- url-host
  "Portable host extraction for http(s) URLs without java.net (CLJC)."
  [url]
  (let [url (str url)
        without-scheme (cond
                         (str/starts-with? url "https://") (subs url 8)
                         (str/starts-with? url "http://") (subs url 7)
                         :else nil)]
    (when without-scheme
      (let [authority (first (str/split without-scheme #"[/?#]" 2))
            host (first (str/split authority #":" 2))]
        (when (seq host) host)))))

(defn net-url-allowed?
  "Whether URL is permitted by POLICY's `:aiueos.policy/net-allow` origin
  allowlist (ADR-0010 scoped net/fetch).

  Empty allowlist fails closed (deny all) — operators must opt in to network
  origins. An entry matches when it equals the URL's host, is a suffix of the
  host (`isekai.network` covers `api.isekai.network`), or is a full URL/prefix
  of the request URL. Surface/host `fetch` providers should share this one
  decision function rather than reimplementing SSRF checks."
  [policy url]
  (let [allow (as-string-set (or (:aiueos.policy/net-allow policy)
                                 (:aiueos/net-allow policy)))
        url (str url)
        host (url-host url)]
    (cond
      (empty? allow) false
      (contains? allow "*") true
      :else
      (boolean
       (some (fn [entry]
               (or (= entry url)
                   (str/starts-with? url entry)
                   (and (seq host)
                        (or (= entry host)
                            (str/ends-with? host (str "." entry))))))
             allow)))))

(defn parse-policy
  "Parse a deployment policy overlay (the `:aiueos/*` EDN validated by
  `aiueos.contract/validate-deployment-policy`) into an effective policy.
  Everything is optional and *extends* the default policy: kernel-caps and
  net-allow are unioned, grants and component-signers are merged
  per-component, forbid is *replaced* per-trust (an explicit `:aiueos/forbid`
  entry for a trust level overrides — not adds to — the default lockdown for
  that level, matching the retired `Policy::from_edn`), signers are merged.

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

       (:aiueos/component-signers overlay)
       (update :aiueos.policy/component-signers
               (fn [component-signers]
                 (reduce-kv (fn [acc id signers]
                              (update acc id set/union (as-kw-set signers)))
                            component-signers
                            (:aiueos/component-signers overlay))))

       (contains? overlay :aiueos/require-signed)
       (assoc :aiueos.policy/require-signed (boolean (:aiueos/require-signed overlay)))

       (:aiueos/surface overlay)
       (assoc :aiueos.policy/surface (name (:aiueos/surface overlay)))

       (:aiueos/net-allow overlay)
       (update :aiueos.policy/net-allow set/union (as-kw-set (:aiueos/net-allow overlay)))))))

(defn granted-to
  "Capabilities available to manifest `m`, presented by `signer` (the
  `:aiueos.broker/signer` `aiueos.broker/authenticate` resolved a valid
  signature to, or `nil` for an unsigned component / a caller with no
  authentication context): kernel primitives ∪ explicit grants. With an
  active surface (ADR-0005), the kernel primitives are restricted to those
  the surface can actually back — an import that maps to an unoffered
  kernel cap becomes :unresolved-capability (the host refuses to provide
  what this surface shouldn't). Explicit grants are never surface-gated.

  ADR-0012 (2026-07-13): `id`'s elevated grant (`extra`, from
  `:aiueos.policy/grants`) is gated against `:aiueos.policy/component-signers`
  ({component-id -> #{authorized signer-id ...}}), fixing the prior gap
  where ANY registered signer could claim ANY component id's grants (`:aiueos.policy/signers`
  is a flat registry with no per-id restriction on its own).

  - `id` BOUND (has a `component-signers` entry): `extra` applies only when
    `signer` is a member of that entry's set — enforced unconditionally,
    independent of `:aiueos.policy/require-signed` (an explicit binding
    declaration is meant to be honored, not silently inert under a
    permissive policy).
  - `id` UNBOUND (no `component-signers` entry): under
    `:aiueos.policy/require-signed` false, `extra` applies via the bare-id
    lookup unchanged (today's behavior — no identity to bind against is
    ever established for callers who don't require one). Under
    `require-signed` true, `extra` does NOT apply (capability-security
    default: no ambient authority — an operator who wants cryptographic
    identity guarantees does not get them undermined by an id nobody
    bothered to bind).

  Either way, failing the check means `base` only (no elevated grant), the
  same non-hard-deny shape an unrecognized id already gets — not a new
  denial kind."
  [policy m signer]
  (let [active-surface (:aiueos.policy/surface policy)
        kernel-caps (:aiueos.policy/kernel-caps policy)
        base (if-let [offered (and active-surface (surface/offered-by-id active-surface))]
               (set/intersection kernel-caps offered)
               kernel-caps)
        id (:aiueos/component m)
        bound-signers (get (:aiueos.policy/component-signers policy) id)
        authorized? (cond
                      (some? bound-signers) (contains? bound-signers signer)
                      (:aiueos.policy/require-signed policy) false
                      :else true)
        extra (if authorized? (get (:aiueos.policy/grants policy) id #{}) #{})]
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
  result) and `policy` (an effective policy from `parse-policy`), presented
  by `signer` (see `granted-to`'s docstring — the resolved
  `:aiueos.broker/signer`, or `nil` for unsigned/no-auth-context callers).
  Returns a policy-decision map matching
  `aiueos.contract/validate-policy-decision`:
  `{:aiueos/decision :grant :aiueos/component id :aiueos/capabilities #{...}}`
  on success, or `{:aiueos/decision :deny :aiueos/component id
  :aiueos/violations [...]}` listing every violation (never just the first).

  The ADR-0001 DMA/IOMMU gate (`:dma-without-iommu`) fires when EITHER `m`
  self-declares `:aiueos/effects #{:dma}` OR `:aiueos/imports` contains any
  `dma-family-imports` id -- not effects alone. A manifest cannot skip the
  gate by simply omitting `:aiueos/effects #{:dma}` while still importing
  `:dma/map`/`:pci/config`/`:mmio/map`/`:irq/subscribe`."
  [m graph policy signer]
  (let [id (:aiueos/component m)
        granted (granted-to policy m signer)
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
                  (let [;; A kernel-primitive keyword (default-kernel-caps --
                        ;; :random/bytes, :log/write, the DMA-family quartet,
                        ;; ...) must NEVER resolve via a co-located peer's
                        ;; self-declared :aiueos/exports: `by-graph` trusts
                        ;; ANY other component's export claim with no
                        ;; authenticity check at all (security audit,
                        ;; 2026-07-13) -- a component merely declaring
                        ;; `:aiueos/exports #{:random/bytes}` would let a
                        ;; sibling's kernel-cap import resolve through this
                        ;; path, bypassing surface/kernel-caps restriction
                        ;; entirely for any multi-component system.aiueos.edn
                        ;; boot. Kernel primitives are the kernel's own to
                        ;; grant (via `granted-to`/by-grant below); an
                        ;; exporter can still provide any NON-reserved
                        ;; capability name it likes.
                        by-graph (and (not (contains? default-kernel-caps imp))
                                     (some #(not= % id) (graph/providers graph imp)))
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
        ;; net/fetch that would actually be granted without a non-empty
        ;; :aiueos.policy/net-allow is fail-closed. Unresolved :net/fetch
        ;; stays a pure :unresolved-capability (no double-count).
        net-would-grant? (or (contains? resolved :net/fetch)
                             (contains? granted :net/fetch))
        net-allow (as-string-set (or (:aiueos.policy/net-allow policy)
                                     (:aiueos/net-allow policy)))
        net-violations
        (if (and net-would-grant? (empty? net-allow))
          [(violation id :net-allow-empty
                      "granted :net/fetch requires a non-empty :aiueos/net-allow origin allowlist")]
          [])
        violations (vec (concat surface-violations import-violations effect-violations
                                dma-violations net-violations))]
    (if (seq violations)
      {:aiueos/decision :deny :aiueos/component id :aiueos/violations violations}
      (let [caps (cond-> resolved
                   (and requires-iommu? (contains? granted :iommu)) (conj :iommu))]
        {:aiueos/decision :grant :aiueos/component id :aiueos/capabilities caps}))))

(defn verify-system
  "Verify every component in `components` (a vector of manifest maps) against
  a shared capability graph built from all of them. Returns a vector of
  policy-decision maps, one per component, in input order.

  Pure policy check, no signature-authentication context -- every component
  is verified as unsigned (`signer` nil in `verify-component`/`granted-to`).
  A caller with real per-component signer identities (i.e. one that already
  ran `aiueos.broker/authenticate` on each manifest) should call
  `aiueos.broker/verify-system` instead, which threads the real resolved
  signer through."
  [components policy]
  (let [g (graph/build components)]
    (mapv #(verify-component % g policy nil) components)))
