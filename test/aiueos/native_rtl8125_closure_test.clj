(ns aiueos.native-rtl8125-closure-test
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.java.shell :as shell]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def repo (System/getProperty "user.dir"))
(def aiueos (io/file repo "os" "aiueos"))
(def rtl-source (io/file aiueos "native" "rtl8125.kotoba"))
(def kernel-source (io/file aiueos "native" "kernel.kotoba"))
(def kernel-builder (io/file aiueos "scripts" "build-kotoba-native-kernel.sh"))
(def audit (io/file aiueos "scripts" "audit-k16-kotoba-native-closure.py"))
(def physical-evidence
  (io/file aiueos "contracts" "physical-kotoba-native-k16-v1.edn"))
(def tcp-candidate
  (io/file aiueos "contracts" "pure-kotoba-tcp-k16-v1.edn"))

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
      (is (str/includes? rtl "(defn build-tcp-segment"))
      (is (str/includes? rtl "(zero-prefix frame 60 0)"))
      (is (str/includes? rtl "(defn tcp-syn-ack-valid"))
      (is (str/includes? rtl "(defn receive-tcp-syn-ack"))
      (is (str/includes? rtl "AIUEOS_NATIVE_TCP_OK"))
      (is (str/includes? rtl "(frame-store-be16 frame 36 8443)"))
      (is (str/includes? rtl "(frame-store-be16 frame 34 49155)"))
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
      (is (str/includes? kernel "(+ 96 pre-cr3-nic-status)"))
      (is (str/includes? kernel
                         "(install-page-fault-idt\n                                         reusable-page rx-limit)"))
      (is (not (str/includes? kernel "(< handler 1097728)")))
      (is (str/includes? kernel "physical-start scratch-start"))
      (is (str/includes? kernel "(< e 549755813888)"))
      (is (not (str/includes? kernel "(< e 1073741824)")))
      (is (str/includes? kernel "(= (load64-boot boot 8) 4)"))
      (is (str/includes? kernel "(= scratch-pages 14)"))
      (is (str/includes? kernel
                         "(qualify-rtl8125 bar-a bar-b rtl-dma-pages)")))
    (testing "the builder compiles the closed module graph with the sealed fuel"
      (is (str/includes? builder "--source-path \"$aiueos\" --unpinned"))
      (is (str/includes? builder "--fuel 1048576"))
      (is (str/includes? builder
                         "13d2f5dfe1adeaa99b7e9e6c04fcf8cb8fc15a4b"))
      (is (str/includes? (slurp (io/file aiueos "scripts"
                                         "build-kotoba-native-boot.sh"))
                         "AIUEOS_NATIVE_K16_PREFLIGHT")))))

(deftest closure-audit-records-physical-nic-proof-without-upgrading-https
  (let [{:keys [exit out err]}
        (shell/sh "python3" (str audit) :dir repo)
        report (str out err)]
    (is (zero? exit) report)
    (is (str/includes? report "physical-arp-and-udp-receipt-passed"))
    (is (str/includes? report "native/rtl8125.kotoba"))
    (is (str/includes? report "physical-pure-kotoba-tcp-positive-receipt"))
    (is (str/includes? report "not-implemented-in-pure-closure"))
    (is (str/includes? report "\"all_native_ready\": false"))))

(deftest physical-k16-native-nic-evidence-binds-screen-wire-and-artifact
  (let [evidence (edn/read-string (slurp physical-evidence))]
    (is (= "97e899fef2bc20412fe5e0919b260db0193a02cd9627bf97b0f24961d7b951df"
           (get-in evidence [:artifact :efi-sha256])))
    (is (= 0 (get-in evidence [:screen :provider-status])))
    (is (= "AIUEOS_NATIVE_ARP_OK" (get-in evidence [:wire :message])))
    (is (= :passed (get-in evidence [:result :physical-rtl8125-provider])))
    (is (some #{:physical-k16-https} (:does-not-prove evidence)))
    (is (some #{:physical-k16-qwen} (:does-not-prove evidence)))))

(deftest tcp-candidate-keeps-tls-and-murakumo-unverified
  (let [candidate (edn/read-string (slurp tcp-candidate))]
    (is (= :kotoba (get-in candidate [:implementation :language])))
    (is (= 8443 (get-in candidate [:target :gateway-port])))
    (is (= "60" (get-in candidate [:diagnostic :screen-success-hex])))
    (is (= "AIUEOS_NATIVE_TCP_OK"
           (get-in candidate [:diagnostic :wire-success])))
    (is (= [{:hex "40" :meaning :tcp-gate-entered}
            {:hex "43" :meaning :syn-descriptor-completed}
            {:hex "44" :meaning :peer-frame-received}]
           (get-in candidate [:diagnostic :wire-stages])))
    (is (= 8000000
           (get-in candidate [:diagnostic :bounded-poll
                              :iterations-per-window])))
    (is (= "4151e6d5cde47f7ae6534185eb1e3bdc102a429c"
           (get-in candidate [:artifact :aiueos-implementation-commit])))
    (is (= "9755a8c5545aeb4b3f3e1a3329f363b8a09880dbf8bf0b08a82b4994fa1f8a6d"
           (get-in candidate [:artifact :efi :sha256])))
    (is (= :passed (get-in candidate [:evidence :qemu :state])))
    (is (= :unverified (get-in candidate [:evidence :physical-k16])))
    (is (some #{:tls13-handshake} (:does-not-prove candidate)))
    (is (some #{:murakumo-node-registration} (:does-not-prove candidate)))
    (is (some #{:decode-throughput} (:does-not-prove candidate)))))
