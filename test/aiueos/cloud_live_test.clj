(ns aiueos.cloud-live-test
  "The gate's decision rule, and the policy it ships with.

  Nothing here touches the network. A gate whose exit code can only be
  exercised by running it against the internet is a gate whose exit code is not
  checked -- and the exit code is the whole product: it is what a person or a
  script reads to decide whether something is wrong."
  (:require [grant.cloud :as cloud]
            [aiueos.cloud-live :as live]
            [aiueos.provider.cloud :as provider]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

;; -- three values, and the order they take precedence in -------------------

(deftest a-check-that-could-not-run-does-not-return-the-value-of-one-that-did
  (is (= 0 (live/exit-code [{:leg :a :outcome :admitted} {:leg :b :outcome :admitted}])))
  (is (= 3 (live/exit-code [{:leg :a :outcome :admitted} {:leg :b :outcome :unmeasured}]))
      "one leg with no answer is not a pass")
  (is (= 1 (live/exit-code [{:leg :a :outcome :admitted} {:leg :b :outcome :refused}])))
  (is (= 3 (live/exit-code []))
      "a gate that ran nothing has not passed"))

(deftest a-refusal-is-never-hidden-behind-a-leg-that-was-skipped
  (is (= 1 (live/exit-code [{:leg :a :outcome :unmeasured} {:leg :b :outcome :refused}]))
      "if both happen, the one that says something is wrong wins"))

;; -- what counts as which ------------------------------------------------

(deftest a-rejected-pin-is-a-refusal-even-though-it-arrives-as-a-fault
  (is (= :refused (live/outcome-of {:aiueos/decision :deny
                                    :aiueos.cloud/reason :peer-not-pinned
                                    :aiueos.provider.cloud/error :peer-not-pinned}))
      "a failed handshake is an I/O exception; reporting the single most
       important negative this repository has as could-not-answer would be a
       shrug")
  (is (= :refused (live/outcome-of {:aiueos/decision :deny
                                    :aiueos.cloud/reason :response-unmeasured
                                    :aiueos.provider.cloud/error :peer-not-pinned}))
      "and this is the shape the inference leg actually produces: the handshake
       failed, so the decision layer never saw a status and said
       :response-unmeasured, while the provider knew the peer was refused.
       Reading the reason first made the gate exit 3 for a bad pin -- measured
       2026-08-22, and the reason this assertion exists")
  (is (= :unmeasured (live/outcome-of {:aiueos/decision :deny
                                       :aiueos.provider.cloud/error :request-failed}))
      "a timeout really is could-not-answer")
  (is (= :unmeasured (live/outcome-of {:aiueos/decision :deny
                                       :aiueos.cloud/reason :response-unmeasured})))
  (is (= :admitted (live/outcome-of {:aiueos/decision :allow})))
  (is (= :refused (live/outcome-of {:aiueos/decision :deny
                                    :aiueos.cloud/reason :digest-mismatch}))))

(deftest a-negative-leg-passes-only-on-the-refusal-it-expected
  (let [refused {:aiueos/decision :deny :aiueos.cloud/reason :response-not-ok
                 :aiueos.cloud/status 404}]
    (is (= :admitted (live/negative-outcome-of refused :response-not-ok 404)))
    (is (= :refused (live/negative-outcome-of refused :response-not-ok 410))
        "a different status is a different fact")
    (is (= :refused (live/negative-outcome-of {:aiueos/decision :allow} :response-not-ok 404))
        "if the absent CID is served, the negative leg has to fail -- a negative
         that cannot fail is decoration")
    (is (= :unmeasured (live/negative-outcome-of
                        {:aiueos/decision :deny :aiueos.provider.cloud/error :request-failed}
                        :response-not-ok 404)))))

;; -- the receipt has to say which happened, in words ----------------------

(deftest a-skipped-leg-and-a-passed-leg-are-distinguishable-in-the-text
  (let [passed (live/leg-line {:leg :storage-read :outcome :admitted :measured {:status 200}})
        skipped (live/leg-line {:leg :inference :outcome :unmeasured
                                :why "no credential: AIUEOS_MURAKUMO_TOKEN is unset"})
        refused (live/leg-line {:leg :storage-read :outcome :refused
                                :reason :peer-not-pinned})
        faulted (live/leg-line {:leg :inference :outcome :refused
                                :reason :response-unmeasured
                                :fault :peer-not-pinned})]
    (is (str/includes? passed "ADMITTED"))
    (is (str/includes? skipped "UNMEASURED"))
    (is (str/includes? skipped "no credential"))
    (is (str/includes? refused "REFUSED"))
    (is (str/includes? refused ":peer-not-pinned"))
    (is (str/includes? faulted ":peer-not-pinned")
        "a refused pin leads with the fault; REFUSED :response-unmeasured would
         contradict its own first word")
    (is (not (str/starts-with? (str/trim (subs faulted 24)) ":response-unmeasured")))
    (is (not= passed skipped))))

;; -- the shipped policy ---------------------------------------------------

(deftest the-live-policy-is-loadable-and-says-what-it-must
  (let [live (live/read-policy)]
    (is (map? live) "resources/aiueos/cloud_live.edn is on the classpath")
    (is (= 1 (:aiueos.cloud-live/version live)))
    (is (true? (cloud/anchors-declared? live))
        "a pinning client with no pins is a client that cannot connect")
    (is (every? #(re-matches #"[0-9a-f]{64}" %) (cloud/trust-anchors live))
        "a malformed pin can never match a measured key, so it would be a set
         that partly cannot work while looking entirely valid")
    (is (false? (:aiueos.cloud/endpoint-from-origin? live))
        "the fallback exists and is off; turning it on is a decision in the file")
    (testing "every anchor has a note saying which host it was measured from"
      (doseq [pin (cloud/trust-anchors live)]
        (let [note (get (:aiueos.cloud-live/anchor-notes live) pin)]
          (is (string? (:host note)) (str pin " has no host note"))
          (is (= "2026-08-21" (:measured note))))))))

(deftest the-cids-the-gate-asks-for-are-cids
  (let [live (live/read-policy)]
    (doseq [k [:aiueos.cloud-live/read-cid :aiueos.cloud-live/absent-cid
               :aiueos.cloud-live/write-cid]]
      (is (some? (cloud/cid-info (get live k))) (str k " does not decode")))
    (is (= "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
           (:digest-hex (cloud/cid-info (:aiueos.cloud-live/read-cid live))))
        "the read CID is the empty byte string: the gateway returns 200 with
         zero bytes and admit-block still has to hash what arrived")
    (is (= :raw (:codec-name (cloud/cid-info (:aiueos.cloud-live/write-cid live))))
        "PUT /ipfs/:cid takes raw CIDv1 only")))

(deftest the-write-target-cid-matches-the-bytes-the-gate-would-send
  (let [live (live/read-policy)
        digest (:digest-hex (cloud/cid-info (:aiueos.cloud-live/write-cid live)))
        plan (cloud/plan-block-write live (:aiueos.cloud-live/write-cid live))]
    (is (cloud/allowed? plan))
    (is (cloud/allowed?
         (cloud/admit-write-payload
          plan
          (provider/sha256-hex
           (.getBytes ^String (:aiueos.cloud-live/write-text live) "UTF-8"))))
        "otherwise the store answers 422 and the operator gets to work out why")
    (is (= digest (provider/sha256-hex
                   (.getBytes ^String (:aiueos.cloud-live/write-text live) "UTF-8"))))))

(deftest the-inference-leg-is-configured-so-it-can-actually-answer
  ;; Three deployment values that each turned a working leg into a false
  ;; UNMEASURED or a false refusal when they were wrong, all measured
  ;; 2026-08-22. They are asserted here because the failures they cause are
  ;; indistinguishable, from the receipt, from the authority being broken.
  (let [live (live/read-policy)]
    (is (<= 256 (:aiueos.cloud-live/max-tokens live))
        "at max_tokens 8 this model spends the whole budget in reasoning_content
         and returns a 200 carrying content \"\" -- a completion-empty refusal
         caused by the gate's own request")
    (is (< 15000 (:aiueos.cloud-live/inference-timeout-ms live))
        "the provider default is 15s; the same request measured 23s and 56s on a
         busy fleet, and a timeout that trips on a healthy authority reports
         UNMEASURED, which reads exactly like an endpoint that is gone")
    (is (contains? (:aiueos.policy/net-allow live) "infer.murakumo.cloud")
        "the alias resolves onto a third host and the leg POSTs to it; without
         the entry there is no admitted endpoint to ask")
    (is (contains? (:aiueos.cloud-live/anchor-notes live)
                   "014bccd86fa34b2a9dbc410b5d27e60e46fb938cc9c9b77058c567cc4b997038")
        "and its key is pinned like the other two, with the date beside it")
    (is (some? (get-in live [:aiueos.cloud-live/credentials :inference :env]))
        "the leg attaches a credential when one is present; the variable is
         named so a receipt can say which one is unset")))

(deftest the-storage-plan-the-gate-would-make-is-allowed-by-the-shipped-policy
  (let [live (live/read-policy)]
    (is (cloud/allowed? (cloud/plan-block-read live (:aiueos.cloud-live/read-cid live))))
    (is (cloud/allowed? (cloud/plan-model-resolve live)))
    (is (cloud/allowed? (cloud/plan-liveness live)))
    (testing "and https is not optional in it"
      (is (nil? (:aiueos.cloud/allow-insecure-origins live))
          "the loopback exemption belongs to tests, not to the live policy"))))

(deftest a-leg-with-no-credential-reports-the-variable-and-not-a-value
  ;; A variable nothing sets, rather than the real one: if this test read the
  ;; real name and someone ran the suite on a machine that had the token, it
  ;; would open a socket and write a block. A test that behaves differently
  ;; depending on the operator's environment is not a test.
  (let [live (assoc (live/read-policy)
                    :aiueos.cloud-live/credentials
                    {:write {:env "AIUEOS_TEST_CREDENTIAL_NEVER_SET"
                             :header "authorization" :prefix "Bearer "}})
        receipt (live/run-write live)
        leg (first (:legs receipt))]
    (is (= 3 (:exit receipt)) "no credential is could-not-answer, never a pass")
    (is (= :unmeasured (:outcome leg)))
    (is (str/includes? (:why leg) "AIUEOS_TEST_CREDENTIAL_NEVER_SET")
        "the receipt names the variable to set, and never a value")
    (is (= :absent (:credential (:measured leg))))
    (is (nil? (System/getenv "AIUEOS_TEST_CREDENTIAL_NEVER_SET"))
        "and the premise of this test is checked rather than assumed")))
