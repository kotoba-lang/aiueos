(ns aiueos.tcb-test
  (:require [aiueos.tcb :as tcb]
            [clojure.test :refer [deftest is]]))

(def required-tcb-paths
  #{"src/aiueos/contract.cljc"
    "src/aiueos/manifest.cljc"
    "src/aiueos/signing.cljc"
    "src/aiueos/key_lifecycle.clj"
    "src/aiueos/policy.cljc"
    "src/aiueos/broker.cljc"
    "src/aiueos/execute.cljc"
    "src/aiueos/entropy.clj"
    "src/aiueos/watchdog.clj"
    "src/aiueos/launcher.cljc"
    "src/aiueos/network_topic.clj"
    "src/aiueos/deployment_profile.cljc"
    "src/aiueos/pid1.cljc"
    "src/aiueos/sealed_audit.clj"
    "src/aiueos/sealed_state.clj"
    "src/aiueos/hvt.cljc"
    "src/aiueos/vfio.cljc"})

(deftest checked-in-tcb-inventory-has-no-drift
  (is (= {:valid? true :files 24 :external 4 :errors []}
         (tcb/validate))))

(deftest authority-and-escape-boundaries-cannot-disappear-silently
  (let [inventory (tcb/read-inventory)
        paths (set (map :path (:tcb/files inventory)))]
    (is (every? paths required-tcb-paths))
    (is (every? (comp keyword? :role) (:tcb/files inventory)))
    (is (some #(= :content-not-release-pinned (:assurance-gap %))
              (:tcb/external inventory)))))

(deftest digest-drift-is-fail-closed
  (let [inventory (tcb/read-inventory)
        changed (assoc-in inventory [:tcb/files 0 :sha256]
                          (apply str (repeat 64 "0")))
        result (tcb/validate changed)]
    (is (false? (:valid? result)))
    (is (= :digest-drift (-> result :errors first :kind)))))
