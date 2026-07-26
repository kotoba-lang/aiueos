(ns aiueos.execution-decision-test
  (:require [clojure.test :refer [deftest is]]
            [aiueos.execution-decision :as decision]
            [aiueos.graph :as graph]
            [aiueos.policy :as policy]
            [kotoba.abi.contract :as abi]))

(def cid "bafy-portable-decision")

(def plan
  {:format :kotoba.plan/v1
   :plan-cid cid :code-closure-cid cid :artifact-cid cid
   :compiler-contract cid :input-cid cid
   :requested-effects #{} :requested-resources #{} :budget {:fuel 1000}})

(defn input [manifest]
  {:plan plan :decision-cid cid :policy-cid cid :db-basis cid
   :issued-at "2026-07-25T00:00:00Z" :expires-at "2026-07-25T00:01:00Z"
   :manifest manifest :graph (graph/build []) :policy policy/default-policy})

(deftest portable-decision-is-derived-from-agent-admission
  (let [permit (decision/decide-plan!
                (input {:aiueos/component :agent/clean :aiueos/kind :agent
                        :aiueos/imports #{:log/write}}))
        deny (decision/decide-plan!
              (input {:aiueos/component :agent/unsafe :aiueos/kind :agent
                      :aiueos/effects #{:network}}))]
    (is (abi/valid-policy-decision? permit))
    (is (= :permit (:result permit)))
    (is (= :deny (:result deny)))
    (is (seq (:reasons deny)))))

(deftest portable-decision-does-not-accept-caller-selected-results
  (is (= :invalid-input
         (try (decision/decide-plan! (assoc (input {:aiueos/component :agent/clean
                                                     :aiueos/kind :agent})
                                             :result :permit))
              nil
              (catch #?(:clj clojure.lang.ExceptionInfo
                        :cljs cljs.core.ExceptionInfo)
                  e
                (:reason (ex-data e)))))))

(deftest approvals-are-bound-to-the-exact-permitted-world
  (let [permit (decision/decide-plan!
                (input {:aiueos/component :agent/clean :aiueos/kind :agent
                        :aiueos/imports #{:log/write}}))
        approval-plan (assoc plan :requested-resources #{:production/write})
        approval {:format :kotoba.approval/v1
                  :approval-cid cid :plan-cid cid :policy-cid cid :db-basis cid
                  :resources #{:production/write} :input-cid cid :approver-cid cid
                  :issued-at "2026-07-25T00:00:00Z"
                  :expires-at "2026-07-25T00:01:00Z"}]
    (is (= approval
           (decision/authorize-approval!
            approval-plan permit approval "2026-07-25T00:00:30Z")))
    (doseq [mutated [(assoc approval :plan-cid "bafy-other-plan")
                     (assoc approval :policy-cid "bafy-other-policy")
                     (assoc approval :db-basis "bafy-other-basis")
                     (assoc approval :input-cid "bafy-other-input")
                     (assoc approval :resources #{:different/write})]]
      (is (thrown? #?(:clj clojure.lang.ExceptionInfo
                      :cljs cljs.core.ExceptionInfo)
                   (decision/authorize-approval!
                    approval-plan permit mutated "2026-07-25T00:00:30Z"))))
    (is (thrown? #?(:clj clojure.lang.ExceptionInfo
                    :cljs cljs.core.ExceptionInfo)
                 (decision/authorize-approval!
                  approval-plan permit approval "2026-07-25T00:01:00Z")))))
