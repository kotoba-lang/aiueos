(ns aiueos.component-abi-test
  (:require [clojure.test :refer [deftest is]]
            [aiueos.component-abi :as component-abi]))

(deftest component-imports-map-to-explicit-aiueos-authority
  (let [imports #{:aiueos.component/aiueos-clock-now}]
    (is (= #{:clock/monotonic}
           (component-abi/requested-capabilities! imports)))
    (is (component-abi/decision-grants-imports?
         {:aiueos/decision :grant :aiueos/capabilities #{:clock/monotonic}}
         imports))
    (is (not (component-abi/decision-grants-imports?
              {:aiueos/decision :deny :aiueos/capabilities #{:clock/monotonic}}
              imports)))))

(deftest unknown-component-import-fails-closed
  (is (thrown? #?(:clj clojure.lang.ExceptionInfo
                  :cljs cljs.core.ExceptionInfo)
               (component-abi/requested-capabilities!
                #{:aiueos.component/unknown}))))

(deftest component-lease-expires-and-is-revoked-by-epoch
  (let [import :aiueos.component/aiueos-clock-now
        ability {:target "clock://monotonic" :operation :clock/now
                 :max-bytes 1 :max-items 1 :deadline-ms 10 :audit-id "lease-test"}
        lease (component-abi/issue-lease
               {:decision {:aiueos/decision :grant :aiueos/capabilities #{:clock/monotonic}}
                :imports #{import} :abilities {import ability}
                :now 100 :epoch 7 :ttl-ms 10 :lease-id "lease-7"})]
    (is (component-abi/lease-authorizes? lease 7 105 import ability))
    (is (not (component-abi/lease-authorizes? lease 8 105 import ability)))
    (is (not (component-abi/lease-authorizes? lease 7 110 import ability)))))
