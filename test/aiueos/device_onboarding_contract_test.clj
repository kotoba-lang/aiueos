(ns aiueos.device-onboarding-contract-test
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.test :refer [deftest is]]))

(def contract
  (edn/read-string
   (slurp (io/file "os/aiueos/contracts/device-onboarding-v1.edn"))))

(deftest account-node-network-and-storage-gates-stay-separate
  (is (= :kotoba-lang/browser (get-in contract [:ui :engine])))
  (is (= "https://auth.kotoba.cloud"
         (get-in contract [:authentication :authority])))
  (is (= "auth.kotoba.cloud"
         (get-in contract [:authentication :rp-id])))
  (is (= "/v1/passkey/login/options"
         (get-in contract [:authentication :endpoints :passkey-options])))
  (is (= [:factory :challenge-issued :account-authenticated
          :device-key-proved :claimed :account-synced]
         (get-in contract [:authentication :shared-flow])))
  (is (false? (get-in contract [:authentication :private-key-copy?])))
  (is (= :ed25519
         (get-in contract [:authentication :device-key-proof :signing])))
  (is (false? (get-in contract
                      [:authentication :device-key-proof
                       :private-key-leaves-device?])))
  (is (= :x25519-hkdf-sha256-aes-256-gcm-v1
         (get-in contract [:network-profile-sync :suite])))
  (is (false? (get-in contract
                      [:network-profile-sync :authority-sees-plaintext?])))
  (is (= :pending-wifi-driver
         (get-in contract [:network-profile-sync :native-k16-application])))
  (is (= :device-owned-cacao
         (get-in contract [:node-addition :murakumo :authentication])))
  (is (false? (get-in contract [:node-addition :murakumo :ready?])))
  (is (= :pending-native-adapter
         (get-in contract [:node-addition :kekkai :initial-state])))
  (is (false? (get-in contract [:storage :local :write-authorized?])))
  (is (= :folder-scoped-opt-in
         (get-in contract [:storage :replication :mode])))
  (is (false? (get-in contract
                      [:storage :replication :plaintext-cloud-copy?]))))
