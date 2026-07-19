(ns aiueos.policy-test
  (:require [aiueos.policy :as policy]
            [aiueos.graph :as graph]
            [clojure.test :refer [deftest is testing]]))

(deftest net-url-allowed-fails-closed-without-allowlist
  (is (false? (policy/net-url-allowed? policy/default-policy
                                       "https://example.com/x")))
  (is (false? (policy/net-url-allowed? {:aiueos.policy/net-allow #{}}
                                       "https://example.com/x"))))

(deftest net-url-allowed-matches-host-and-prefix
  (let [p {:aiueos.policy/net-allow #{"isekai.network" "http://127.0.0.1:9/"}}]
    (is (true? (policy/net-url-allowed? p "https://isekai.network/gftd/orbs")))
    (is (true? (policy/net-url-allowed? p "https://api.isekai.network/v1")))
    (is (true? (policy/net-url-allowed? p "http://127.0.0.1:9/path")))
    (is (false? (policy/net-url-allowed? p "https://evil.example/steal")))))

(def empty-graph (graph/build []))

(deftest net-url-allowed-fails-closed-without-allowlist
  (is (false? (policy/net-url-allowed? policy/default-policy
                                       "https://example.com/x")))
  (is (false? (policy/net-url-allowed? {:aiueos.policy/net-allow #{}}
                                       "https://example.com/x"))))

(deftest net-url-allowed-matches-host-and-prefix
  (let [p {:aiueos.policy/net-allow #{"isekai.network" "http://127.0.0.1:9/"}}]
    (is (true? (policy/net-url-allowed? p "https://isekai.network/gftd/orbs")))
    (is (true? (policy/net-url-allowed? p "https://api.isekai.network/v1")))
    (is (true? (policy/net-url-allowed? p "http://127.0.0.1:9/path")))
    (is (false? (policy/net-url-allowed? p "https://evil.example/steal")))))

(deftest net-fetch-import-denied-without-net-allow
  (testing "granting/importing :net/fetch with empty net-allow is a hard deny"
    (let [;; cloud surface offers net/fetch; put it in kernel-caps so the
          ;; import can resolve, then leave net-allow empty.
          pol (policy/parse-policy
               {:aiueos/kernel-caps #{:net/fetch}
                :aiueos/net-allow #{}})
          m {:aiueos/component :app/browserish :aiueos/kind :app :aiueos/trust :verified
             :aiueos/imports #{:net/fetch} :aiueos/exports #{}}
          decision (policy/verify-component m empty-graph pol nil)]
      (is (= :deny (:aiueos/decision decision)))
      (is (some #{:net-allow-empty}
                (map :aiueos/kind (:aiueos/violations decision))))))
  (testing "non-empty net-allow admits the same import when kernel offers it"
    (let [pol (policy/parse-policy
               {:aiueos/kernel-caps #{:net/fetch}
                :aiueos/net-allow #{"example.com"}})
          m {:aiueos/component :app/browserish :aiueos/kind :app :aiueos/trust :verified
             :aiueos/imports #{:net/fetch} :aiueos/exports #{}}
          decision (policy/verify-component m empty-graph pol nil)]
      (is (= :grant (:aiueos/decision decision)))
      (is (contains? (:aiueos/capabilities decision) :net/fetch)))))

(deftest grants-a-kernel-capability-import
  (let [m {:aiueos/component :service/log :aiueos/kind :service :aiueos/trust :verified
           :aiueos/imports #{:log/write} :aiueos/exports #{}}
        decision (policy/verify-component m empty-graph policy/default-policy nil)]
    (is (= :grant (:aiueos/decision decision)))
    (is (contains? (:aiueos/capabilities decision) :log/write))))

(deftest denies-an-unresolved-import
  (let [m {:aiueos/component :app/notes :aiueos/kind :app :aiueos/trust :verified
           :aiueos/imports #{:net/fetch} :aiueos/exports #{}}
        decision (policy/verify-component m empty-graph policy/default-policy nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:unresolved-capability]
           (mapv :aiueos/kind (:aiueos/violations decision))))))

(deftest resolves-an-import-via-a-provider-component
  (let [fs-service {:aiueos/component :service/fs :aiueos/kind :service :aiueos/trust :verified
                    :aiueos/exports #{:fs/read} :aiueos/imports #{}}
        notes-app {:aiueos/component :app/notes :aiueos/kind :app :aiueos/trust :verified
                   :aiueos/imports #{:fs/read} :aiueos/exports #{}}
        g (graph/build [fs-service notes-app])
        decision (policy/verify-component notes-app g policy/default-policy nil)]
    (is (= :grant (:aiueos/decision decision)))
    (is (contains? (:aiueos/capabilities decision) :fs/read))))

(deftest a-component-does-not-resolve-its-own-export
  (let [m {:aiueos/component :app/self :aiueos/kind :app :aiueos/trust :verified
           :aiueos/imports #{:custom/thing} :aiueos/exports #{:custom/thing}}
        g (graph/build [m])
        decision (policy/verify-component m g policy/default-policy nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:unresolved-capability] (mapv :aiueos/kind (:aiueos/violations decision))))))

(deftest ai-generated-lockdown-forbids-network-secrets-persistent-write
  (let [m {:aiueos/component :agent/researcher :aiueos/kind :agent :aiueos/trust :ai-generated
           :aiueos/effects #{:network :secrets}}
        decision (policy/verify-component m empty-graph policy/default-policy nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= 2 (count (:aiueos/violations decision))))
    (is (every? #(= :forbidden-effect %) (map :aiueos/kind (:aiueos/violations decision))))))

(deftest untrusted-forbids-secrets-but-not-network
  (let [m {:aiueos/component :app/plain :aiueos/kind :app :aiueos/trust :untrusted
           :aiueos/effects #{:network}}
        decision (policy/verify-component m empty-graph policy/default-policy nil)]
    (is (= :grant (:aiueos/decision decision)))))

(deftest missing-trust-defaults-to-untrusted
  (let [m {:aiueos/component :app/no-trust :aiueos/kind :app
           :aiueos/effects #{:secrets}}
        decision (policy/verify-component m empty-graph policy/default-policy nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:forbidden-effect] (mapv :aiueos/kind (:aiueos/violations decision))))))

(deftest dma-without-iommu-is-denied
  (let [m {:aiueos/component :driver/virtio-blk :aiueos/kind :driver :aiueos/trust :verified
           :aiueos/effects #{:dma}}
        decision (policy/verify-component m empty-graph policy/default-policy nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:dma-without-iommu] (mapv :aiueos/kind (:aiueos/violations decision))))))

(deftest dma-with-required-and-granted-iommu-succeeds
  (let [policy* (policy/parse-policy {:aiueos/grants {:driver/virtio-blk #{:iommu}}})
        m {:aiueos/component :driver/virtio-blk :aiueos/kind :driver :aiueos/trust :verified
           :aiueos/effects #{:dma} :aiueos/requires #{:iommu}}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :grant (:aiueos/decision decision)))
    (is (contains? (:aiueos/capabilities decision) :iommu))))

(deftest dma-requires-iommu-key-even-if-granted
  (let [policy* (policy/parse-policy {:aiueos/grants {:driver/virtio-blk #{:iommu}}})
        m {:aiueos/component :driver/virtio-blk :aiueos/kind :driver :aiueos/trust :verified
           :aiueos/effects #{:dma}}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:dma-without-iommu] (mapv :aiueos/kind (:aiueos/violations decision))))))

;; ───────── security fix (2607131500): the DMA/IOMMU gate now ALSO fires
;; off :aiueos/imports containing a DMA-family capability, not solely off
;; the self-declared, previously-unenforced :aiueos/effects #{:dma} field.
;; A manifest could otherwise import :dma/map (or :pci/config/:mmio/map/
;; :irq/subscribe) while simply omitting :aiueos/effects #{:dma}, silently
;; skipping the gate entirely ─────────

(deftest dma-family-import-without-declared-effects-still-triggers-the-gate
  (testing "ADVERSARIAL: :aiueos/imports #{:dma/map} with NO
  :aiueos/effects key at all (not even an empty set) -- before this fix,
  dma? was purely (contains? effects :dma), so this manifest would have
  sailed straight through with no IOMMU check whatsoever, exactly the gap
  ADR-0001/SECURITY.md's IOMMU rule was supposed to close"
    (let [m {:aiueos/component :driver/sneaky :aiueos/kind :driver :aiueos/trust :verified
             :aiueos/imports #{:dma/map}}
          decision (policy/verify-component m empty-graph policy/default-policy nil)]
      (is (= :deny (:aiueos/decision decision)))
      (is (= [:dma-without-iommu] (mapv :aiueos/kind (:aiueos/violations decision)))))))

(deftest dma-family-import-with-requires-and-granted-iommu-succeeds
  (let [policy* (policy/parse-policy {:aiueos/grants {:driver/sneaky #{:iommu}}})
        m {:aiueos/component :driver/sneaky :aiueos/kind :driver :aiueos/trust :verified
           :aiueos/imports #{:dma/map} :aiueos/requires #{:iommu}}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :grant (:aiueos/decision decision)))
    (is (contains? (:aiueos/capabilities decision) :iommu))
    (is (contains? (:aiueos/capabilities decision) :dma/map))))

(deftest each-device-access-quartet-import-alone-triggers-the-gate
  (testing "all four DMA-family capabilities (not just :dma/map) trigger
  the gate purely off being imported"
    (doseq [cap #{:pci/config :dma/map :irq/subscribe :mmio/map}]
      (let [m {:aiueos/component :driver/quartet :aiueos/kind :driver :aiueos/trust :verified
               :aiueos/imports #{cap}}
            decision (policy/verify-component m empty-graph policy/default-policy nil)]
        (is (= :deny (:aiueos/decision decision)) cap)
        (is (= [:dma-without-iommu] (mapv :aiueos/kind (:aiueos/violations decision))) cap)))))

(deftest a-non-dma-family-import-does-not-trigger-the-gate
  (testing "sanity: an ordinary kernel-cap import (not in dma-family-imports)
  is unaffected -- the gate is specific to the device-access quartet, not
  every import"
    (let [m {:aiueos/component :app/logger :aiueos/kind :app :aiueos/trust :verified
             :aiueos/imports #{:log/write}}
          decision (policy/verify-component m empty-graph policy/default-policy nil)]
      (is (= :grant (:aiueos/decision decision)))
      (is (not (contains? decision :aiueos/violations))))))

(deftest surface-gate-denies-a-component-pinned-elsewhere
  (let [policy* (policy/parse-policy {:aiueos/surface :browser})
        m {:aiueos/component :app/robot-only :aiueos/kind :app :aiueos/trust :verified
           :aiueos/surface #{:robot}}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:surface-mismatch] (mapv :aiueos/kind (:aiueos/violations decision))))))

(deftest surface-gate-allows-a-portable-component
  (let [policy* (policy/parse-policy {:aiueos/surface :browser})
        m {:aiueos/component :app/portable :aiueos/kind :app :aiueos/trust :verified}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :grant (:aiueos/decision decision)))))

(deftest surface-restricts-kernel-caps-to-the-offered-set
  (let [policy* (policy/parse-policy {:aiueos/surface :browser})
        ;; :random/bytes (not :pci/config -- a dma-family import, security
        ;; fix 2607131500 -- would ALSO trip :dma-without-iommu, conflating
        ;; this test's single concern) isn't in browser's offered set either.
        m {:aiueos/component :app/needs-random :aiueos/kind :app :aiueos/trust :verified
           :aiueos/imports #{:random/bytes}}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :deny (:aiueos/decision decision)))
    (is (= [:unresolved-capability] (mapv :aiueos/kind (:aiueos/violations decision))))))

(deftest a-co-located-components-export-cannot-spoof-a-kernel-primitive-name
  (testing "CONFIRMED BUG regression (independent review of PR #26): by-graph
            resolution trusted ANY co-located component's self-declared
            :aiueos/exports with no authenticity check at all -- a component
            merely claiming to export a KERNEL-PRIMITIVE keyword
            (:random/bytes, one of default-kernel-caps) let a sibling's
            import of that same name resolve via the graph path, bypassing
            a surface restriction that would otherwise deny it entirely.
            Kernel primitives must only ever resolve via granted-to
            (by-grant), never via an untrusted peer's export claim."
    (let [policy* (policy/parse-policy {:aiueos/surface :browser})
          spoofer {:aiueos/component :app/malicious :aiueos/kind :app :aiueos/trust :ai-generated
                   :aiueos/exports #{:random/bytes} :aiueos/imports #{}}
          victim {:aiueos/component :app/needs-random :aiueos/kind :app :aiueos/trust :verified
                  :aiueos/imports #{:random/bytes} :aiueos/exports #{}}
          g (graph/build [spoofer victim])
          decision (policy/verify-component victim g policy* nil)]
      (is (= :deny (:aiueos/decision decision))
          "browser's surface doesn't offer :random/bytes -- the spoofed export must NOT
           let this resolve anyway")
      (is (= [:unresolved-capability] (mapv :aiueos/kind (:aiueos/violations decision)))))))

(deftest a-co-located-components-export-still-resolves-a-non-kernel-capability
  (testing "the fix above must not be over-broad -- a genuine, non-reserved capability
            name (the resolves-an-import-via-a-provider-component case, just under a
            surface-restricted policy too) must still resolve via a peer's export"
    (let [policy* (policy/parse-policy {:aiueos/surface :browser})
          fs-service {:aiueos/component :service/fs :aiueos/kind :service :aiueos/trust :verified
                      :aiueos/exports #{:fs/read} :aiueos/imports #{}}
          notes-app {:aiueos/component :app/notes :aiueos/kind :app :aiueos/trust :verified
                     :aiueos/imports #{:fs/read} :aiueos/exports #{}}
          g (graph/build [fs-service notes-app])
          decision (policy/verify-component notes-app g policy* nil)]
      (is (= :grant (:aiueos/decision decision)))
      (is (contains? (:aiueos/capabilities decision) :fs/read)))))

(deftest explicit-forbid-overlay-replaces-the-default-for-that-trust
  (let [policy* (policy/parse-policy {:aiueos/forbid {:untrusted #{}}})
        m {:aiueos/component :app/wants-secrets :aiueos/kind :app :aiueos/trust :untrusted
           :aiueos/effects #{:secrets}}
        decision (policy/verify-component m empty-graph policy* nil)]
    (is (= :grant (:aiueos/decision decision)))))

(deftest verify-system-returns-one-decision-per-component-in-order
  (let [fs-service {:aiueos/component :service/fs :aiueos/kind :service :aiueos/trust :verified
                    :aiueos/exports #{:fs/read} :aiueos/imports #{}}
        notes-app {:aiueos/component :app/notes :aiueos/kind :app :aiueos/trust :verified
                   :aiueos/imports #{:fs/read} :aiueos/exports #{}}
        decisions (policy/verify-system [fs-service notes-app] policy/default-policy)]
    (is (= [:service/fs :app/notes] (mapv :aiueos/component decisions)))
    (is (every? #(= :grant (:aiueos/decision %)) decisions))))

(deftest four-axis-abac-grants-only-the-exact-declared-context
  (let [rule {:subject/signers #{:release-signer}
              :resource/trust #{:verified}
              :resource/effects #{:storage}
              :action/capabilities #{:log/write}
              :environment/surfaces #{:cloud}}
        policy* (policy/parse-policy
                 {:aiueos/surface :cloud
                  :aiueos/abac {:service/log rule}})
        manifest {:aiueos/component :service/log :aiueos/kind :service
                  :aiueos/trust :verified :aiueos/effects #{:storage}
                  :aiueos/imports #{:log/write}}
        decision (policy/verify-component manifest empty-graph policy* :release-signer)]
    (is (= :grant (:aiueos/decision decision)))
    (doseq [[label changed expected]
            [[:subject {:signer :attacker} :abac-subject]
             [:resource {:manifest (assoc manifest :aiueos/trust :untrusted)} :abac-resource]
             [:action {:manifest (assoc manifest :aiueos/imports #{:random/bytes})} :abac-action]
             [:environment {:policy (policy/parse-policy
                                     {:aiueos/surface :browser
                                      :aiueos/abac {:service/log rule}})}
              :abac-environment]]]
      (testing (name label)
        (let [result (policy/verify-component (or (:manifest changed) manifest)
                                              empty-graph
                                              (or (:policy changed) policy*)
                                              (get changed :signer :release-signer))]
          (is (= :deny (:aiueos/decision result)))
          (is (some #(= expected (:aiueos/kind %)) (:aiueos/violations result))))))))

;; ── ADR-0012: component-signer binding ──────────────────────────────────────
(deftest deployment-denies-implicit-classification-downgrade
  (let [manifest {:aiueos/component :service/export :aiueos/kind :service
                  :aiueos/trust :verified
                  :aiueos/classification :confidential
                  :aiueos/output-classification :public}
        rule {:subject :release-signer :purpose :release
              :now "2026-07-19T12:00:00Z"}
        policy* (policy/parse-policy
                 {:aiueos/information-flow {:service/export rule}})
        denied (policy/verify-component manifest empty-graph policy* :release-signer)
        grant {:id :release-redaction :subject :release-signer :purpose :release
               :from :confidential :to :public
               :expires-at "2026-07-20T00:00:00Z"}
        allowed (policy/verify-component
                 manifest empty-graph
                 (policy/parse-policy
                  {:aiueos/information-flow
                   {:service/export (assoc rule :declassification-grant grant)}})
                 :release-signer)]
    (is (= :deny (:aiueos/decision denied)))
    (is (= :information-flow
           (get-in denied [:aiueos/violations 0 :aiueos/kind])))
    (is (= :grant (:aiueos/decision allowed)))
    (is (true? (get-in allowed [:aiueos/detail :information-flow
                                :information-flow/allowed?])))))

(deftest deployment-requires-qualified-mutual-transport-profile
  (let [manifest {:aiueos/component :service/remote :aiueos/kind :service
                  :aiueos/trust :verified}
        profile {:protocol :tls-1.3 :mutual-auth? true
                 :peer-id "did:web:api.example" :expected-peer-id "did:web:api.example"
                 :certificate-fingerprint "sha256:current"
                 :trusted-fingerprints #{"sha256:current" "sha256:next"}
                 :revocation-checked? true :now "2026-07-19T12:00:00Z"
                 :certificate-expires-at "2026-08-01T00:00:00Z"
                 :require-rotation-overlap? true
                 :next-certificate-fingerprint "sha256:next"}
        allowed (policy/verify-component
                 manifest empty-graph
                 (policy/parse-policy {:aiueos/transport {:service/remote profile}}) nil)
        denied (policy/verify-component
                manifest empty-graph
                (policy/parse-policy
                 {:aiueos/transport {:service/remote (assoc profile :mutual-auth? false)}})
                nil)]
    (is (= :grant (:aiueos/decision allowed)))
    (is (true? (get-in allowed [:aiueos/detail :transport :transport/allowed?])))
    (is (= :deny (:aiueos/decision denied)))
    (is (= :transport-security
           (get-in denied [:aiueos/violations 0 :aiueos/kind])))))

;; :fs/admin (not one of default-kernel-caps) is used as the "elevated,
;; custom, privileged" capability throughout -- unlike :log/write et al.,
;; nothing grants it via `base`, so its presence/absence in `granted-to`'s
;; result cleanly isolates whether the `extra` (:aiueos.policy/grants) path
;; fired.

(deftest bound-id-authorized-signer-gets-the-elevated-grant
  (let [policy* (policy/parse-policy {:aiueos/grants {:driver/privileged #{:fs/admin}}
                                      :aiueos/component-signers {:driver/privileged #{:owner}}})
        m {:aiueos/component :driver/privileged}]
    (is (contains? (policy/granted-to policy* m :owner) :fs/admin))))

(deftest bound-id-wrong-signer-does-not-get-the-elevated-grant
  (testing "even though the wrong signer is otherwise validly registered/signed --
            component-signers enforcement doesn't depend on require-signed"
    (let [policy* (policy/parse-policy {:aiueos/grants {:driver/privileged #{:fs/admin}}
                                        :aiueos/component-signers {:driver/privileged #{:owner}}})
          m {:aiueos/component :driver/privileged}]
      (is (not (contains? (policy/granted-to policy* m :attacker) :fs/admin)))
      (is (not (contains? (policy/granted-to policy* m nil) :fs/admin))
          "presented unsigned against a bound id -- also no elevated grant"))))

(deftest unbound-id-permissive-policy-still-gets-the-elevated-grant
  (testing "require-signed false: unchanged, bare-id lookup regardless of signer"
    (let [policy* (policy/parse-policy {:aiueos/grants {:driver/open #{:fs/admin}}})
          m {:aiueos/component :driver/open}]
      (is (contains? (policy/granted-to policy* m nil) :fs/admin))
      (is (contains? (policy/granted-to policy* m :anyone) :fs/admin)))))

(deftest unbound-id-under-require-signed-does-not-get-the-elevated-grant
  (testing "ADR-0012's actual decision: require-signed true closes the ambient-authority
            gap for ids nobody bothered to bind, even for a signer that's otherwise
            validly registered/authenticated"
    (let [policy* (policy/parse-policy {:aiueos/grants {:driver/unbound-under-strict #{:fs/admin}}
                                        :aiueos/require-signed true})
          m {:aiueos/component :driver/unbound-under-strict}]
      (is (not (contains? (policy/granted-to policy* m :some-registered-signer) :fs/admin)))
      (is (contains? (policy/granted-to policy* m :some-registered-signer)
                      :log/write)
          "base (kernel-caps) is still granted -- this is 'no elevated grant', not a hard deny"))))

(deftest production-deployment-requires-real-hybrid-pqc-envelope
  (let [manifest {:aiueos/component :service/pqc}
        crypto-policy {:kotoba.security/crypto-policy-version 1
                       :mode :hybrid-required :hybrid-epoch-floor 1}
        envelope {:envelope/provider {:provider/id :kagi
                                      :provider/fips-validated false}
                  :envelope/kem? true :envelope/hybrid? true
                  :envelope/epoch 2
                  :envelope/algorithms [:x25519 :ml-kem-768]}
        decision (fn [e]
                   (policy/verify-component
                    manifest empty-graph
                    (policy/parse-policy
                     {:aiueos/crypto
                      {:service/pqc {:required? true :policy crypto-policy
                                     :envelope e}}})
                    nil))]
    (is (= :grant (:aiueos/decision (decision envelope))))
    (is (= :deny (:aiueos/decision
                  (decision (assoc envelope :envelope/algorithms [:x25519])))))
    (is (= :hybrid-pqc
           (-> (decision (assoc envelope :envelope/hybrid? false))
               :aiueos/violations first :aiueos/kind)))))
