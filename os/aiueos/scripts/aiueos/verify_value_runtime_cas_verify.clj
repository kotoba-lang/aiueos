(ns aiueos.verify-value-runtime-cas-verify
  "Runs `aiueos.value-runtime-cas-verify` against the vectors its contract
  declares.

  RUNS it. Until 2026-08-31 this file computed each vector's answer with
  `java.security.MessageDigest` and compared THAT against the contract's own
  `:expected` values. The compiled object was loaded, checked for its export
  symbol and its imports, and never called. Deleting its body and leaving a
  correctly-named export would not have failed a single vector -- the shape
  root ADR-2608136000 question 6 names, in the file whose whole subject is
  integrity.

  kotoba-kir's optional memory image is what makes asking the object possible
  (aiueos ADR-0128)."
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            [kotoba.compiler.core :as compiler]
            [kotoba.kir :as kir]))

(defn- fail! [message data]
  (throw (ex-info message (assoc data :phase :aiueos-value-runtime-cas-verify))))

(defn- hex-bytes [s]
  (when (odd? (count s)) (fail! "odd-length hexadecimal vector" {:hex s}))
  (mapv #(Integer/parseInt (apply str %) 16) (partition 2 s)))

(defn- vector-input [{:keys [input-hex generated-bytes fill-byte]}]
  (if generated-bytes
    (vec (repeat generated-bytes (or fill-byte 0)))
    (hex-bytes (or input-hex ""))))

(defn- write-at [image offset bytes]
  (reduce (fn [v [i b]] (assoc v (+ offset i) b)) image (map-indexed vector bytes)))

(defn- run-one [program {:keys [base image-bytes expected-offset output-offset
                                workspace-offset block-offset]}
                fuel vector]
  (let [input (vector-input vector)
        expected (hex-bytes (:expected-digest-hex vector))
        image (-> (vec (repeat image-bytes 0))
                  (write-at expected-offset expected)
                  (write-at block-offset input))]
    (try
      (kir/execute program 'aiueos-value-runtime-cas-verify
                   [(+ base block-offset) (count input)
                    (+ base expected-offset) (+ base output-offset)
                    (+ base workspace-offset)]
                   {:memory {:base base :bytes (volatile! image)} :fuel fuel})
      (catch clojure.lang.ExceptionInfo e (ex-data e)))))

(defn -main [& [sha-source digest-source verify-source contract-path]]
  (when-not (and sha-source digest-source verify-source contract-path)
    (fail! "usage: <sha256.kotoba> <digest-equal.kotoba> <cas-verify.kotoba> <contract.edn>" {}))
  (let [contract (edn/read-string (slurp contract-path))
        native (:native contract)
        {:keys [memory fuel floors]} (:verification contract)
        result (compiler/compile-project
                {'aiueos.value-runtime-sha256 (slurp sha-source)
                 'aiueos.value-runtime-digest-equal (slurp digest-source)
                 'aiueos.value-runtime-cas-verify (slurp verify-source)}
                'aiueos.value-runtime-cas-verify :x86_64-aiueos-kernel-v1)
        program (:kir result)
        present-ops (set (filter symbol? (tree-seq coll? seq program)))]

    (when-not (= (:export native) (get-in result [:object :export]))
      (fail! "CAS verifier native export mismatch"
             {:actual (get-in result [:object :export])}))
    (when-not (= (:imports native) (get-in result [:object :imports]))
      (fail! "CAS verifier imports foreign code"
             {:imports (get-in result [:object :imports])}))
    (when-not (every? present-ops (:required-operations native))
      (fail! "CAS verifier lacks bounded memory operations"
             {:required (:required-operations native)
              :missing (remove present-ops (:required-operations native))}))
    (when-not memory
      (fail! "REFUSING TO REPORT A PASS: the contract declares no memory layout, so no vector can be run"
             {}))

    (let [observed
          (doall
           (for [{:keys [name expected] :as v} (:vectors contract)]
             (let [actual (run-one program memory fuel v)]
               (when (map? actual)
                 (fail! (if (= :kernel-memory-unavailable (:trap actual))
                          "REFUSING TO REPORT A PASS: this kotoba-kir cannot execute kernel memory operations, so no vector was actually run"
                          "vector trapped where a result was expected")
                        {:vector name :trap actual}))
               (when-not (= expected actual)
                 (fail! "CAS digest vector mismatch"
                        {:vector name :expected expected :actual actual}))
               actual)))
          seen (set observed)]

      ;; Evidence floors. A run that executed nothing must not return what a
      ;; run that executed everything returns, and a suite that only ever saw
      ;; one verdict has not shown the object can produce the other.
      (when (< (count observed) (:minimum-vectors floors))
        (fail! "fewer vectors ran than the contract's floor"
               {:ran (count observed) :floor (:minimum-vectors floors)}))
      (when-not (= #{0 1} seen)
        (fail! "the object never produced both verdicts" {:observed (vec (sort seen))}))

      (println (str "VECTORS\t" (count observed)))
      (println (str "VERDICTS-OBSERVED\t" (str/join "," (sort seen))))
      (println (pr-str {:format :aiueos.value-runtime-cas-verify/verification-v1
                        :vectors (count observed)
                        :verdicts-observed (vec (sort seen))
                        :maximum-block-bytes 12288
                        :executed true :imports [] :foreign-code false
                        :status :passed})))))
