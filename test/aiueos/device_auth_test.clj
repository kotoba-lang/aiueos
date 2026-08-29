(ns aiueos.device-auth-test
  (:require [aiueos.device-auth :as auth]
            [clojure.test :refer [deftest is testing]]))

(def base
  (auth/factory {:device-did "did:aiueos:k16:one"
                 :model "gmktec-k16"
                 :rp-id "auth.kotoba.cloud"
                 :origin "https://auth.kotoba.cloud"
                 :authority "https://auth.kotoba.cloud"}))

(defn issued []
  (auth/issue-challenge base {:challenge "once" :now-ms 1000 :ttl-ms 300000}))

(def passkey-proof
  {:auth/method :passkey
   :auth/challenge "once"
   :auth/rp-id "auth.kotoba.cloud"
   :auth/origin "https://auth.kotoba.cloud"
   :auth/user-present? true
   :auth/user-verified? true
   :authority/verified? true
   :account/principal-id "urn:kotoba:principal:one"
   :account/did "did:key:account"
   :passkey/credential-id "credential-1"
   :passkey/sign-count 8
   :passkey/sign-count-ok? true})

(deftest passkey-and-device-key-complete-one-claim
  (let [account (auth/authenticate-account (issued) passkey-proof 1100)
        claimed (auth/prove-device account
                                   {:device-did "did:aiueos:k16:one"
                                    :public-key "ed25519:device"
                                    :proof-valid? true}
                                   1200)
        plan (auth/node-plan claimed)]
    (is (auth/claimed? claimed))
    (is (= :kotoba-lang/browser (get-in plan [:ui :engine])))
    (is (= :ready-to-append (get-in plan [:account :sync])))
    (is (= "urn:kotoba:principal:one" (get-in plan [:account :principal-id])))
    (is (false? (get-in plan [:account :private-key-copied?])))
    (is (false? (get-in plan [:murakumo :ready?]))
        "claiming an account is not workload readiness")
    (is (= :pending-native-adapter (get-in plan [:kekkai :node-state])))
    (is (= :opt-in-not-started (get-in plan [:storage :replication])))))

(deftest phone-scan-still-requires-a-passkey-on-the-phone
  (let [proof (assoc passkey-proof
                     :auth/method :phone-scan
                     :phone/device-did "did:key:phone"
                     :phone/approved? true
                     :phone/passkey-verified? false)
        denied (auth/authenticate-account (issued) proof 1100)
        admitted (auth/authenticate-account
                  (issued) (assoc proof :phone/passkey-verified? true) 1100)]
    (is (= :phone/passkey-required (:aiueos.device-auth/reason denied)))
    (is (= :account-authenticated (:aiueos.device-auth/state admitted)))))

(deftest challenge-rp-origin-and-device-proof-fail-closed
  (testing "the challenge is single use and time bounded"
    (is (= :challenge/expired
           (:aiueos.device-auth/reason
            (auth/authenticate-account (issued) passkey-proof 301000))))
    (is (= :challenge/mismatch
           (:aiueos.device-auth/reason
            (auth/authenticate-account
             (issued) (assoc passkey-proof :auth/challenge "replay") 1100)))))
  (testing "the passkey ceremony is bound to RP and origin"
    (is (= :passkey/origin-mismatch
           (:aiueos.device-auth/reason
            (auth/authenticate-account
             (issued) (assoc passkey-proof :auth/origin "https://evil.example") 1100))))
    (is (= :passkey/clone-signal
           (:aiueos.device-auth/reason
            (auth/authenticate-account
             (issued) (assoc passkey-proof :passkey/sign-count-ok? false) 1100)))))
  (testing "an authenticated person cannot add a different unproved machine"
    (let [account (auth/authenticate-account (issued) passkey-proof 1100)
          denied (auth/prove-device account
                                    {:device-did "did:aiueos:k16:two"
                                     :public-key "ed25519:other"
                                     :proof-valid? true}
                                    1200)]
      (is (= :device/did-mismatch (:aiueos.device-auth/reason denied))))))
