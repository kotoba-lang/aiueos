(ns aiueos.verify-value-runtime-cas-verify
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            [kotoba.compiler.core :as compiler])
  (:import [java.security MessageDigest]))

(defn- fail! [message data]
  (throw (ex-info message (assoc data :phase :aiueos-value-runtime-cas-verify))))

(defn- hex-bytes [s]
  (when (odd? (count s))
    (fail! "odd-length hexadecimal vector" {:hex s}))
  (byte-array
   (map #(unchecked-byte (Integer/parseInt (apply str %) 16))
        (partition 2 s))))

(defn- bytes= [a b]
  (MessageDigest/isEqual a b))

(defn- vector-input [{:keys [input-hex generated-bytes fill-byte]}]
  (if generated-bytes
    (byte-array generated-bytes (unchecked-byte fill-byte))
    (hex-bytes input-hex)))

(defn- model [vector]
  (let [input (vector-input vector)
        expected (hex-bytes (:expected-digest-hex vector))]
    (if (and (<= 1 (alength input) 12288) (= 32 (alength expected))
             (bytes= expected (.digest (MessageDigest/getInstance "SHA-256") input)))
      1 0)))

(defn- flattened [value]
  (tree-seq coll? seq value))

(defn -main [& [sha-source digest-source verify-source contract-path]]
  (when-not (and sha-source digest-source verify-source contract-path)
    (fail! "usage: <sha256.kotoba> <digest-equal.kotoba> <cas-verify.kotoba> <contract.edn>" {}))
  (let [contract (edn/read-string (slurp contract-path))
        result (compiler/compile-project
                {'aiueos.value-runtime-sha256 (slurp sha-source)
                 'aiueos.value-runtime-digest-equal (slurp digest-source)
                 'aiueos.value-runtime-cas-verify (slurp verify-source)}
                'aiueos.value-runtime-cas-verify :x86_64-aiueos-kernel-v1)
        values (flattened (:kir result))
        present-ops (set (filter symbol? values))
        native (:native contract)]
    (doseq [{:keys [name expected] :as vector} (:vectors contract)]
      (let [actual (model vector)]
        (when-not (= expected actual)
          (fail! "CAS digest vector mismatch"
                 {:vector name :expected expected :actual actual}))))
    (when-not (= (:export native) (get-in result [:object :export]))
      (fail! "CAS verifier native export mismatch"
             {:actual (get-in result [:object :export])}))
    (when-not (= (:imports native) (get-in result [:object :imports]))
      (fail! "CAS verifier imports foreign code"
             {:imports (get-in result [:object :imports])}))
    (when-not (every? present-ops (:required-operations native))
      (fail! "CAS verifier lacks bounded memory operations"
             {:required (:required-operations native)}))
    (println (pr-str {:format :aiueos.value-runtime-cas-verify/verification-v1
                      :vectors (count (:vectors contract))
                      :maximum-block-bytes 12288 :imports []
                      :foreign-code false :status :passed}))))
