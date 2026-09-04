(ns aiueos.ssh-packet-bisect
  (:require [clojure.test :refer [deftest is]]
            [kotoba.compiler.core :as compiler]
            [clojure.java.io :as io]))

(def src (slurp (io/file "os" "aiueos" "kotoba" "ssh-packet.kotoba")))

(defn try-compile [label text]
  (try
    (do (compiler/compile-source text :wasm32-kotoba-v1 {})
        (println label "OK"))
    (catch Exception e
      (println label "FAIL" (.getMessage e)))))

(defn slice [from to]
  (let [i1 (.indexOf ^String src from)
        i2 (if to (.indexOf ^String src to) (count src))]
    (subs src i1 i2)))

(def head (subs src 0 (.indexOf ^String src "(defn kat-store-iota")))
(def k1 (slice "(defn kat-store-iota" ";; Byte equality"))
(def k2 (slice ";; Byte equality" ";; The transport_test"))
(def k3 (slice ";; The transport_test" ";; The admitted payload"))
(def k4 (slice ";; The admitted payload" ";; Phase 1"))
(def p1 (slice "(defn kat-phase-1" ";; Phase 2"))
(def p2 (slice "(defn kat-phase-2" ";; Phase 3"))
(def p3 (slice "(defn kat-phase-3" ";; Phase 4"))
(def p4 (slice "(defn kat-phase-4" "(defn aiueos-ssh-packet-kat"))
(def ke (slice "(defn aiueos-ssh-packet-kat" nil))

(deftest bisect
  (try-compile "head-only" head)
  (try-compile "head+k1" (str head k1))
  (try-compile "head+k1..k4" (str head k1 k2 k3 k4))
  (try-compile "head+k+p1" (str head k1 k2 k3 k4 p1))
  (try-compile "head+k+p1p2" (str head k1 k2 k3 k4 p1 p2))
  (try-compile "head+k+p1p2p3" (str head k1 k2 k3 k4 p1 p2 p3))
  (try-compile "head+k+p1..p4" (str head k1 k2 k3 k4 p1 p2 p3 p4))
  (try-compile "head+k+p1..p4+ke" (str head k1 k2 k3 k4 p1 p2 p3 p4 ke))
  (is true))
))))))))