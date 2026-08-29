(ns aiueos.device-onboarding-contract-test
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.test :refer [deftest is]]))

(def contract
  (edn/read-string
   (slurp (io/file "os/aiueos/contracts/device-onboarding-v1.edn"))))

(deftest account-node-network-and-storage-gates-stay-separate
  (is (= :kotoba-lang/browser (get-in contract [:ui :engine])))
  (is (= [:factory :challenge-issued :account-authenticated
          :device-key-proved :claimed :account-synced]
         (get-in contract [:authentication :shared-flow])))
  (is (false? (get-in contract [:authentication :private-key-copy?])))
  (is (false? (get-in contract [:node-addition :murakumo :ready?])))
  (is (= :pending-native-adapter
         (get-in contract [:node-addition :kekkai :initial-state])))
  (is (false? (get-in contract [:storage :local :write-authorized?])))
  (is (= :folder-scoped-opt-in
         (get-in contract [:storage :replication :mode])))
  (is (false? (get-in contract
                      [:storage :replication :plaintext-cloud-copy?]))))
