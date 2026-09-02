(ns aiueos.native-rtl8125-closure-test
  (:require [clojure.java.io :as io]
            [clojure.java.shell :as shell]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def repo (System/getProperty "user.dir"))
(def aiueos (io/file repo "os" "aiueos"))
(def rtl-source (io/file aiueos "native" "rtl8125.kotoba"))
(def kernel-source (io/file aiueos "native" "kernel.kotoba"))
(def kernel-builder (io/file aiueos "scripts" "build-kotoba-native-kernel.sh"))
(def audit (io/file aiueos "scripts" "audit-k16-kotoba-native-closure.py"))

(deftest rtl8125-is-in-the-closed-native-module-graph
  (let [rtl (slurp rtl-source)
        kernel (slurp kernel-source)
        builder (slurp kernel-builder)]
    (testing "the provider is Kotoba source, not a wrapper around the C path"
      (is (str/includes? rtl "(ns native.rtl8125"))
      (is (not (str/includes? rtl "kernel/rtl8125.c")))
      (is (str/includes? rtl "(defn bar-for-bus"))
      (is (str/includes? rtl "(defn install-directory"))
      (is (str/includes? rtl "(defn rings-start"))
      (is (str/includes? rtl "(defn receive-arp"))
      (is (str/includes? rtl "(defn send-native-arp-receipt"))
      (is (str/includes? rtl "AIUEOS_NATIVE_ARP_OK"))
      (is (str/includes? rtl "(defn send-native-nic-diagnostic"))
      (is (str/includes? rtl "(defn diagnose-network-status"))
      (is (str/includes? rtl "AIUEOS_NATIVE_NIC_"))
      (is (str/includes? rtl "(store-server-mac frame)"))
      (is (str/includes? rtl "(frame-store-be16 frame 36 7777)"))
      (is (str/includes? rtl "(kernel-load-u16 mmio 512 offset)"))
      (is (str/includes? rtl "(kernel-store-u16 mmio 512 offset value)"))
      (is (not (str/includes? rtl
                              "(kernel-store-u8 mmio 512 (+ offset 1)"))))
    (testing "the K16 BDFs, UC/NX map and four-page DMA authority are linked"
      (is (str/includes? kernel "[native.rtl8125 :as rtl]"))
      (is (str/includes? kernel "(rtl/bar-for-bus 2)"))
      (is (str/includes? kernel "(rtl/bar-for-bus 3)"))
      (is (str/includes? kernel "rtl-dma-pages"))
      (is (str/includes? kernel "(qualify-rtl8125"))
      (is (str/includes? kernel "pre-cr3-nic-status"))
      (is (str/includes? kernel "physical-start scratch-start"))
      (is (str/includes? kernel "(= (load64-boot boot 8) 4)"))
      (is (str/includes? kernel "(= scratch-pages 14)"))
      (is (str/includes? kernel
                         "(qualify-rtl8125 bar-a bar-b rtl-dma-pages)")))
    (testing "the builder compiles the closed module graph with the sealed fuel"
      (is (str/includes? builder "--source-path \"$aiueos\" --unpinned"))
      (is (str/includes? builder "--fuel 1048576"))
      (is (str/includes? builder
                         "c2095e05d01ffc2dd2da412a98a3344fc2dc2b45")))))

(deftest closure-audit-keeps-physical-proof-unverified
  (let [{:keys [exit out err]}
        (shell/sh "python3" (str audit) :dir repo)
        report (str out err)]
    (is (zero? exit) report)
    (is (str/includes? report "one-shot-native-provider-qemu-negative-physical-unverified"))
    (is (str/includes? report "native/rtl8125.kotoba"))
    (is (str/includes? report "physical-rtl8125-arp-positive-receipt"))
    (is (str/includes? report "\"all_native_ready\": false"))))
