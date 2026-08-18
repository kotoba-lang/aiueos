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

(deftest every-failure-is-classified
  (testing "a new failure mode must not be folded into an existing bucket"
    (doseq [{:keys [object cause verdict]} (:value-runtime/results @receipt)]
      (when (= :fail verdict)
        (is (some? cause) (str object " failed with no cause recorded"))
        (is (not= :unclassified cause)
            (str object " failed in a way this receipt has no class for: "
                 "add the class rather than widening an existing pattern"))))))

(deftest the-causes-account-for-every-failure
  (let [by-cause (:value-runtime/failing-by-cause @receipt)]
    (is (seq by-cause))
    (is (= (:value-runtime/failing @receipt) (reduce + (vals by-cause)))
        "a cause histogram that does not sum to the failure count is describing
         a different measurement than the one beside it")))

(deftest a-native-slice-rejection-names-the-form-it-rejected
  (testing "'the slice does not admit this' with no form named is a report
            nobody can act on, and would hide six asks behind one sentence"
    (doseq [{:keys [object cause rejected-form verdict]} (:value-runtime/results @receipt)]
      (when (and (= :fail verdict)
                 (contains? #{:native-slice-typed-values :native-slice-lowering} cause))
        (is (seq rejected-form)
            (str object " was refused by the native slice with no form recorded"))))))

(deftest the-rejected-forms-are-counted-from-the-rows
  (let [forms (:value-runtime/rejected-forms @receipt)
        from-rows (frequencies (keep :rejected-form (:value-runtime/results @receipt)))]
    (is (= from-rows forms)
        "the histogram is derived, not maintained beside the rows")
    (is (<= (reduce + (vals forms)) (:value-runtime/failing @receipt))
        "more named forms than failures would mean a row was counted twice")))

(deftest every-refused-form-points-at-where-the-work-is
  (let [upstream (:value-runtime/upstream @receipt)]
    (doseq [form (keys (:value-runtime/rejected-forms @receipt))]
      (is (seq (get upstream form))
          (str "the slice refuses " form " and nothing says who was asked — "
               "a measurement that names a wall and points at no one is a "
               "complaint")))))

(deftest every-failure-class-points-at-where-the-work-is
  (let [by-cause (:value-runtime/upstream-by-cause @receipt)]
    (doseq [cause (keys (:value-runtime/failing-by-cause @receipt))]
      (is (seq (get by-cause cause))
          (str cause " has failures and nothing says who was asked — the "
               "export class carries no rejected form, so the per-form floor "
               "alone would let it sit unreferenced")))))
