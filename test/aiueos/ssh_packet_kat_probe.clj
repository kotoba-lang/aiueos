(ns aiueos.ssh-packet-kat-probe
  (:require [clojure.test :refer [deftest is]]
            [kotoba.compiler.core :as compiler]
            [kotoba.kir :as ir]
            [clojure.java.io :as io]))
(deftest probe-kat
  (let [src (slurp (io/file "os" "aiueos" "kotoba" "ssh-packet.kotoba"))
        kir (:kir (compiler/compile-source src :wasm32-kotoba-v1 {}))
        image (volatile! (vec (repeat 512 0)))
        base 4096
        r (ir/execute kir 'aiueos-ssh-packet-kat [base]
                      {:fuel 1048576 :memory {:base base :bytes image}})]
    (println "KAT-RESULT" r)
    (println "FRAME20-HEAD" (take 10 (drop 32 @image)))
    (println "ADMIT20-HEAD" (take 10 (drop 96 @image)))
    (println "REFUSE-HEAD" (take 8 (drop 224 @image)))
    (is (zero? r) "baked KAT returns 0")))