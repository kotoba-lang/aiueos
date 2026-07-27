(ns aiueos.tcb
  "Machine-verifiable trusted-computing-base inventory."
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io])
  (:import [java.security MessageDigest]))

(def inventory-path "qualification/tcb-inventory.edn")

(defn- hex [^bytes value]
  (apply str (map #(format "%02x" (bit-and (int %) 0xff)) value)))

(defn sha256-file [path]
  (with-open [input (io/input-stream path)]
    (let [digest (MessageDigest/getInstance "SHA-256")
          buffer (byte-array 16384)]
      (loop []
        (let [read (.read input buffer)]
          (when (pos? read)
            (.update digest buffer 0 read)
            (recur))))
      (hex (.digest digest)))))

(defn read-inventory []
  (edn/read-string (slurp inventory-path)))

(defn validate
  "Return structured drift/errors. An inventory update must be an intentional
  review action; changing trusted code without updating its digest fails."
  ([] (validate (read-inventory)))
  ([inventory]
   (let [files (:tcb/files inventory)
         paths (mapv :path files)
         errors
         (into []
               (concat
                (when-not (= 1 (:tcb/version inventory))
                  [{:kind :unsupported-version
                    :actual (:tcb/version inventory)}])
                (when-not (= (count paths) (count (set paths)))
                  [{:kind :duplicate-path}])
                (mapcat
                 (fn [{:keys [path role sha256]}]
                   (let [file (io/file path)]
                     (cond
                       (not (.exists file))
                       [{:kind :missing-file :path path}]
                       (not (keyword? role))
                       [{:kind :missing-role :path path}]
                       (not= sha256 (sha256-file file))
                       [{:kind :digest-drift :path path
                         :expected sha256 :actual (sha256-file file)}]
                       :else [])))
                 files)))]
     {:valid? (empty? errors)
      :files (count files)
      :external (count (:tcb/external inventory))
      :errors errors})))

(defn -main [& _]
  (let [result (validate)]
    (prn result)
    (when-not (:valid? result)
      (System/exit 1))))
