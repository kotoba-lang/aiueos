(ns aiueos.value-runtime-baseline-test
  "The value-runtime receipt is honest, and its failure count only goes down.

  This does not compile anything — `aiueos.verify-value-runtime-all` does, and
  it needs the compiler closure. What this asserts is that the written-down
  measurement still describes the repository it claims to: the same compiler,
  every object accounted for, and a failure count that has not grown.

  A receipt is only worth its closure. If someone advances the compiler pin and
  does not re-measure, these numbers stop being about this repository — so the
  sha is checked, and a stale receipt is red rather than quietly quoted."
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def ^:private receipt-path "qualification/value-runtime-baseline.edn")

(def ^:private failing-ceiling
  "Measured 2026-08-18: all ten. **This number may only be lowered.** Raising
  it is how a ratchet becomes a record of decline."
  10)

(def ^:private receipt (delay (edn/read-string (slurp receipt-path))))

(defn- value-sources []
  (->> (.listFiles (io/file "os" "aiueos" "kotoba"))
       (map #(.getName %))
       (filter #(and (str/starts-with? % "value-") (str/ends-with? % ".kotoba")))
       (map #(str/replace % #"\.kotoba$" ""))
       sort))

(deftest the-receipt-measured-something
  (testing "an evidence floor: a receipt of nothing must not read as clean"
    (is (<= 10 (:value-runtime/objects @receipt)))
    (is (= (:value-runtime/objects @receipt) (count (:value-runtime/results @receipt))))))

(deftest the-receipt-is-about-the-compiler-this-repo-pins
  (let [pinned (get-in (edn/read-string (slurp "deps.edn"))
                       [:aliases :test :extra-deps
                        'io.github.kotoba-lang/compiler :git/sha])]
    (is (= pinned (:value-runtime/compiler-sha @receipt))
        (str "the receipt was measured against "
             (:value-runtime/compiler-sha @receipt)
             " and deps.edn now pins " pinned
             " — re-run aiueos.verify-value-runtime-all"))))

(deftest every-value-object-is-accounted-for
  (let [measured (set (map :object (:value-runtime/results @receipt)))
        inputs (set (keys (:value-runtime/no-verifier-of-their-own @receipt)))]
    (doseq [object (value-sources)]
      (is (or (contains? measured object) (contains? inputs object))
          (str object " is neither measured nor named as an input to something"
               " that is — a new object must not arrive unmeasured")))))

(deftest the-failure-count-has-not-grown
  (is (<= (:value-runtime/failing @receipt) failing-ceiling)
      "the ratchet only turns one way")
  (is (= (:value-runtime/failing @receipt)
         (count (filter #(= :fail (:verdict %)) (:value-runtime/results @receipt))))
      "the headline count is derived from the rows, not asserted beside them"))

(deftest a-pass-carries-no-failure-message
  (doseq [{:keys [object verdict message]} (:value-runtime/results @receipt)]
    (is (contains? #{:ok :fail} verdict) (str object ": unknown verdict " verdict))
    (when (= :ok verdict)
      (is (nil? message) (str object " passed and still carries a failure message")))
    (when (= :fail verdict)
      (is (seq message) (str object " failed with no reason recorded")))))
