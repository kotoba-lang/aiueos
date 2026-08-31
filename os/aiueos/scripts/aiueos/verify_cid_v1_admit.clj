(ns aiueos.verify-cid-v1-admit
  "Runs `aiueos.cid-v1-admit` against the vectors its contract declares.

  RUNS it. The neighbouring `verify_value_runtime_cas_verify.clj` does not:
  with no way to execute a byte-walking object it re-implements SHA-256 in
  Java and compares that to the contract's own expected values, so its six
  vectors pass whatever the compiled object does. kotoba-kir 10fa46ce takes an
  optional memory image, which is what makes this file able to ask the object
  instead of asking a model of it."
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            [kotoba.compiler.core :as compiler]
            [kotoba.kir :as kir]))

(defn- fail! [message data]
  (throw (ex-info message (assoc data :phase :aiueos-cid-v1-admit))))

(defn- hex-bytes [s]
  (when (odd? (count s)) (fail! "odd-length hexadecimal vector" {:hex s}))
  (mapv #(Integer/parseInt (apply str %) 16) (partition 2 s)))

(defn- vector-block [{:keys [block-hex block-bytes fill-byte]}]
  (if block-bytes
    (vec (repeat block-bytes (or fill-byte 0)))
    (hex-bytes (or block-hex ""))))

(defn- write-at [image offset bytes]
  (reduce (fn [v [i b]] (assoc v (+ offset i) b)) image (map-indexed vector bytes)))

(defn- run-one
  "Executes the object once. Returns the reason code, or the trap's ex-data."
  [program {:keys [base image-bytes cid-offset scratch-offset block-offset]} fuel
   vector scratch-override]
  (let [cid (hex-bytes (:cid-hex vector))
        block (vector-block vector)
        image (-> (vec (repeat image-bytes 0))
                  (write-at cid-offset cid)
                  (write-at block-offset block))]
    (try
      (kir/execute program 'aiueos-cid-v1-admit
                   [(+ base cid-offset) (count cid)
                    (+ base block-offset) (count block)
                    (or scratch-override (+ base scratch-offset))]
                   {:memory {:base base :bytes (volatile! image)} :fuel fuel})
      (catch clojure.lang.ExceptionInfo e (ex-data e)))))

(defn -main [& [sha-source admit-source contract-path]]
  (when-not (and sha-source admit-source contract-path)
    (fail! "usage: <value-runtime-sha256.kotoba> <cid-v1-admit.kotoba> <contract.edn>" {}))
  (let [contract (edn/read-string (slurp contract-path))
        native (:native contract)
        {:keys [memory fuel floors]} (:verification contract)
        result (compiler/compile-project
                {'aiueos.value-runtime-sha256 (slurp sha-source)
                 'aiueos.cid-v1-admit (slurp admit-source)}
                'aiueos.cid-v1-admit :x86_64-aiueos-kernel-v1)
        program (:kir result)
        present-ops (set (filter symbol? (tree-seq coll? seq program)))]

    (when-not (= (:export native) (get-in result [:object :export]))
      (fail! "export symbol mismatch" {:actual (get-in result [:object :export])}))
    (when-not (= (:imports native) (get-in result [:object :imports]))
      (fail! "object imports foreign code" {:imports (get-in result [:object :imports])}))
    (when-not (every? present-ops (:required-operations native))
      (fail! "object lacks the bounded memory operations it claims"
             {:required (:required-operations native)
              :missing (remove present-ops (:required-operations native))}))

    ;; Vectors.
    (let [observed
          (doall
           (for [{:keys [name expected] :as v} (:vectors contract)]
             (let [actual (run-one program memory fuel v nil)]
               (when (map? actual)
                 ;; A trap where a reason code was expected is never a pass.
                 ;; `:kernel-memory-unavailable` in particular means the
                 ;; kotoba-kir on this classpath predates the memory image and
                 ;; this runner could not ask the object anything at all.
                 (fail! (if (= :kernel-memory-unavailable (:trap actual))
                          "REFUSING TO REPORT A PASS: this kotoba-kir cannot execute kernel memory operations, so no vector was actually run"
                          "vector trapped where a reason code was expected")
                        {:vector name :trap actual}))
               (when-not (= expected actual)
                 (fail! "vector mismatch" {:vector name :expected expected :actual actual}))
               actual)))

          traps
          (doall
           (for [{:keys [name scratch expect-trap expect-check] :as t} (:traps contract)]
             (let [actual (run-one program memory fuel t scratch)]
               (when-not (map? actual)
                 (fail! "trap vector returned a reason code" {:vector name :actual actual}))
               (when-not (and (= expect-trap (:trap actual)) (= expect-check (:check actual)))
                 (fail! "trap vector named the wrong fault"
                        {:vector name :expected [expect-trap expect-check]
                         :actual [(:trap actual) (:check actual)]}))
               name)))

          ;; Evidence floors. A run that executed nothing must not be able to
          ;; return what a run that executed everything returns.
          seen (set observed)
          reachable (set (remove (set (:unreachable-by-construction contract))
                                 (keys (:reasons contract))))
          unobserved (sort (remove seen reachable))
          impossible-but-seen (sort (filter seen (set (:unreachable-by-construction contract))))]

      (when (< (count observed) (:minimum-vectors floors))
        (fail! "fewer vectors ran than the contract's floor"
               {:ran (count observed) :floor (:minimum-vectors floors)}))
      (when (and (:every-reachable-reason-observed floors) (seq unobserved))
        (fail! "a declared reason was never produced by any vector"
               {:unobserved unobserved}))
      (when (seq impossible-but-seen)
        (fail! "a reason declared unreachable by construction was produced"
               {:reasons impossible-but-seen}))

      (println (str "VECTORS\t" (count observed)))
      (println (str "TRAPS\t" (count traps)))
      (println (str "REASONS-OBSERVED\t" (str/join "," (sort seen))))
      (println (pr-str {:format :aiueos.cid-v1-admit/verification-v1
                        :vectors (count observed) :traps (count traps)
                        :reasons-observed (vec (sort seen))
                        :unreachable-by-construction
                        (vec (sort (:unreachable-by-construction contract)))
                        :executed true :imports [] :status :passed})))))
