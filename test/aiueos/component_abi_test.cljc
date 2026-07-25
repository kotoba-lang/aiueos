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
  (is (thrown? clojure.lang.ExceptionInfo
               (component-abi/requested-capabilities!
                #{:aiueos.component/unknown}))))
