(ns aiueos.verify-unixfs-file-admit
  "Runs `aiueos.unixfs-file-admit` against the vectors its contract declares.

  The two admitted nodes are not invented here. `unixfs.file/build` in
  `tech-ipfs-specs-unixfs` produced them and that implementation is pinned
  against kubo 0.41 CIDs, so the bytes this object admits are bytes a real
  IPFS gateway would serve. Every refusal vector is one of those two nodes
  with one named byte changed."
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            [kotoba.compiler.core :as compiler]
            [kotoba.kir :as kir]))

(defn- fail! [message data]
  (throw (ex-info message (assoc data :phase :aiueos-unixfs-file-admit))))

(defn- hex-bytes [s]
  (when (odd? (count s)) (fail! "odd-length hexadecimal vector" {:hex s}))
  (mapv #(Integer/parseInt (apply str %) 16) (partition 2 s)))

(defn- run-one [program {:keys [base image-bytes node-offset]} fuel
                {:keys [node-hex node-length expected-links expected-filesize]}]
  (let [node (hex-bytes node-hex)
        image (reduce (fn [v [i b]] (assoc v (+ node-offset i) b))
                      (vec (repeat image-bytes 0)) (map-indexed vector node))]
    (try
      (kir/execute program 'aiueos-unixfs-file-admit
                   [(+ base node-offset) (or node-length (count node))
                    expected-links expected-filesize]
                   {:memory {:base base :bytes (volatile! image)} :fuel fuel})
      (catch clojure.lang.ExceptionInfo e (ex-data e)))))

(defn -main [& [source contract-path]]
  (when-not (and source contract-path)
    (fail! "usage: <unixfs-file-admit.kotoba> <contract.edn>" {}))
  (let [contract (edn/read-string (slurp contract-path))
        native (:native contract)
        {:keys [memory fuel floors]} (:verification contract)
        result (compiler/compile-project
                {'aiueos.unixfs-file-admit (slurp source)}
                'aiueos.unixfs-file-admit :x86_64-aiueos-kernel-v1)
        program (:kir result)
        present-ops (set (filter symbol? (tree-seq coll? seq program)))]

    (when-not (= (:export native) (get-in result [:object :export]))
      (fail! "export symbol mismatch" {:actual (get-in result [:object :export])}))
    (when-not (= (:imports native) (get-in result [:object :imports]))
      (fail! "object imports foreign code" {:imports (get-in result [:object :imports])}))
    (when-not (every? present-ops (:required-operations native))
      (fail! "object lacks the bounded memory operations it claims"
             {:missing (remove present-ops (:required-operations native))}))

    (let [observed
          (doall
           (for [{:keys [name expected] :as v} (:vectors contract)]
             (let [actual (run-one program memory fuel v)]
               (when (map? actual)
                 ;; A node this object parses must never reach a memory fault:
                 ;; every load it performs is guarded by a length it checked
                 ;; first, and a fault would mean the kernel dies on bytes an
                 ;; attacker chose rather than refusing them.
                 (fail! (if (= :kernel-memory-unavailable (:trap actual))
                          "REFUSING TO REPORT A PASS: this kotoba-kir cannot execute kernel memory operations, so no vector was actually run"
                          "vector trapped where a reason code was expected")
                        {:vector name :trap actual}))
               (when-not (= expected actual)
                 (fail! "vector mismatch" {:vector name :expected expected :actual actual}))
               actual)))
          seen (set observed)
          reachable (set (keys (:reasons contract)))
          unobserved (sort (remove seen reachable))]

      (when (< (count observed) (:minimum-vectors floors))
        (fail! "fewer vectors ran than the contract's floor"
               {:ran (count observed) :floor (:minimum-vectors floors)}))
      (when (and (:every-reachable-reason-observed floors) (seq unobserved))
        (fail! "a declared reason was never produced by any vector"
               {:unobserved unobserved}))

      (println (str "VECTORS\t" (count observed)))
      (println (str "REASONS-OBSERVED\t" (str/join "," (sort seen))))
      (println (pr-str {:format :aiueos.unixfs-file-admit/verification-v1
                        :vectors (count observed)
                        :reasons-observed (vec (sort seen))
                        :executed true :imports [] :status :passed})))))
