(ns aiueos.native-pxe-test
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def loader (slurp (io/file "os/aiueos/uefi/main.c")))
(def probe (slurp (io/file "os/aiueos/dbc-probe/main.c")))
(def build (slurp (io/file "os/aiueos/scripts/build-uefi.sh")))
(def release-build
  (slurp (io/file "os/aiueos/scripts/build-physical-qualification-pxe.sh")))
(def smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-physical-pxe.sh")))
(def server (slurp (io/file "os/aiueos/tools/k16-pxe-server.py")))
(def contract
  (slurp (io/file "os/aiueos/contracts/physical-qualification-pxe-v1.edn")))
(def physical-network-contract
  (slurp (io/file "os/aiueos/contracts/physical-network-qualification-pxe-v1.edn")))
(def murakumo-contract
  (slurp (io/file "os/aiueos/contracts/murakumo-node-v1.edn")))
(def rtl8125 (slurp (io/file "os/aiueos/kernel/rtl8125.c")))
(def pci (slurp (io/file "os/aiueos/kernel/pci.c")))
(def kernel (slurp (io/file "os/aiueos/kernel/main.c")))
(def physical-network-build
  (slurp (io/file "os/aiueos/scripts/build-physical-network-pxe.sh")))
(def physical-network-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-rtl8125-qualification.sh")))
(def physical-relay-contract
  (slurp (io/file "os/aiueos/contracts/physical-relay-qualification-pxe-v1.edn")))
(def murakumo-relay-contract
  (slurp (io/file "os/aiueos/contracts/murakumo-relay-enrollment-v1.edn")))
(def physical-relay-build
  (slurp (io/file "os/aiueos/scripts/build-physical-relay-pxe.sh")))
(def relay-protocol
  (slurp (io/file "os/aiueos/kernel/relay_protocol.c")))
(def relay-protocol-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-relay-protocol.sh")))
(def physical-relay-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-physical-relay.sh")))
(def persistent-build
  (slurp (io/file "os/aiueos/scripts/build-physical-job-persistent-pxe.sh")))
(def persistent-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-physical-persistent.sh")))
(def qualification-runtime
  (slurp (io/file "os/aiueos/kernel/qualification.c")))

(deftest single-efi-carries-admitted-native-payload
  (testing "the PXE artifact embeds exactly the kernel and initramfs built now"
    (doseq [marker ["AIUEOS_EMBEDDED_RELEASE"
                    "aiueos_embedded_kernel_start"
                    "aiueos_embedded_initramfs_start"
                    "AIUEOS_NETBOOT_EMBEDDED_OK kernel+initramfs sha256-v1"]]
      (is (str/includes? loader marker)))
    (doseq [marker [".incbin" "AIUEOS_EMBEDDED_RELEASE"
                    "embedded-release.obj"]]
      (is (str/includes? build marker)))
    (is (str/includes? release-build "AIUEOS_NETBOOT_QUALIFICATION=1"))
    (is (str/includes? release-build "AIUEOS_SOURCE_DIRTY"))
    (is (str/includes? contract ":admission :compiled-sha256"))))

(deftest native-netboot-returns-to-the-control-efi
  (testing "BootNext is one-shot and BootOrder has no write surface"
    (doseq [marker ["prepare_netboot_qualification_return"
                    "boot_current_name" "boot_next_name"
                    "AIUEOS_NETBOOT_RETURN_ARMED"]]
      (is (str/includes? loader marker)))
    (is (not (str/includes? loader "boot_order_name")))
    (is (str/includes? contract ":boot-order-write? false"))
    (is (str/includes? smoke "preserving the UEFI variable store")))
  (testing "the normal-user server consumes only a completed one-shot transfer"
    (doseq [marker ["AIUEOS_PXE_NEXT_BOOT_ARMED"
                    "AIUEOS_PXE_NEXT_BOOT_CONSUMED"
                    "consume_next_boot(selected)"
                    "fallback={BOOT_PATH}"]]
      (is (str/includes? server marker)))))

(deftest qualification-result-returns-live-without-a-usb-write
  (doseq [marker ["AIUEOS_QUALIFICATION_RESULT state="
                  "source=uefi-nvram internal-ssd-writes=none retained=yes"
                  "report_qualification_result(system)"]]
    (is (str/includes? probe marker)))
  (is (str/includes? contract ":usb-runtime-writes? false"))
  (is (str/includes? contract ":internal-disk-writes? false"))
  (is (str/includes? contract ":physical-state :in-progress")))

(deftest qemu-and-physical-evidence-stay-distinct
  (doseq [marker ["AIUEOS_NATIVE_PXE_QEMU_OK"
                  "physical-k16=unverified"
                  "repeated native PXE builds differ"]]
    (is (str/includes? smoke marker)))
  (is (str/includes? contract ":does-not-prove [:physical-k16-boot")))

(deftest persistent-native-records-success-without-resetting
  (doseq [marker ["AIUEOS_PERSISTENT_BOOT=1"
                  "build-physical-job-pxe.sh"]]
    (is (str/includes? persistent-build marker)))
  (is (str/includes? loader
                     "AIUEOS_LOADER_WATCHDOG_DISABLED persistent-native"))
  (is (str/includes? qualification-runtime
                     "#ifdef AIUEOS_PERSISTENT_BOOT"))
  (doseq [marker ["AIUEOS_NATIVE_PERSISTENT_QEMU_OK"
                  "current-pxe-persistent"
                  "watchdog=disabled"]]
    (is (str/includes? persistent-smoke marker))))

(deftest k16-rtl8125-uefi-observation-stays-read-only
  (doseq [marker ["AIUEOS_RTL8125_HANDOFF bdf="
                  "access=mmio-read-only"
                  "report_rtl8125_handoff"
                  "rtl8125_mac_valid"]]
    (is (str/includes? probe marker)))
  (is (str/includes? contract ":device-writes? false"))
  (is (str/includes? contract ":phase :uefi-pxe-before-exit-boot-services")))

(deftest k16-rtl8125-native-qualification-is-separate-and-bounded
  (testing "the model, PCI wiring and physical-only build name the same gate"
    (doseq [marker ["aiueos_rtl8125_takeover" "RGE_DESC_OWN"
                    "RGE_RX_SOF" "RGE_RX_EOF"]]
      (is (str/includes? rtl8125 marker)))
    (is (str/includes? pci "aiueos_rtl8125_physical_qualification"))
    (is (str/includes? pci "0x0a4d0001U"))
    (is (str/includes? kernel "AIUEOS_PHYSICAL_NETWORK_QUALIFICATION"))
    (is (str/includes? kernel "aiueos_qualification_finalize(1,8125)"))
    (is (str/includes? physical-network-build
                       "AIUEOS_PHYSICAL_NETWORK_QUALIFICATION=1"))
    (is (str/includes? physical-network-smoke
                       "state=failure code=8201 source=uefi-nvram"))
    (is (str/includes? physical-network-smoke
                       "physical-k16=unverified")))
  (testing "the contracts parse and refuse to call model evidence physical"
    (let [physical (edn/read-string physical-network-contract)
          murakumo (edn/read-string murakumo-contract)]
      (is (= :unverified (get-in physical [:evidence :physical-state])))
      (is (= :amd-ivrs-unimplemented-test-only
             (get-in physical [:safety :dma-isolation])))
      (is (some #{:murakumo-heartbeat} (:does-not-prove physical)))
      (is (= :unverified
             (get-in murakumo [:gaps :transport :physical-link
                               :rtl8125-pxe-handoff :physical-state])))
      (is (= :relay-enrollment-implemented-native-unverified
             (get-in murakumo [:gaps :transport :murakumo-heartbeat :state]))))))

(deftest k16-relay-round-trip-is-request-bound-and-not-enrollment
  (testing "the physical-only profile binds a UDP ACK to this boot's nonce"
    (doseq [marker ["AIUEOS_NODE_HELLO_V1" "AIUEOS_NODE_ACK_V1"
                    "aiueos_relay_ack_payload_valid"]]
      (is (str/includes? relay-protocol marker)))
    (doseq [marker ["rtl8125_relay_ack_valid"
                    "aiueos_rtl8125_relay_qualification"]]
      (is (str/includes? pci marker)))
    (doseq [marker ["NODE_HELLO" "node_ack_payload"
                    "AIUEOS_NODE_RELAY_ACK" "scope=diagnostic-only"]]
      (is (str/includes? server marker)))
    (is (str/includes? kernel "aiueos_qualification_finalize(1,8130)"))
    (is (str/includes? physical-relay-build
                       "AIUEOS_PHYSICAL_RELAY_QUALIFICATION=1"))
    (is (str/includes? relay-protocol-smoke
                       "relay_protocol_model.c"))
    (is (str/includes? physical-relay-smoke
                       "build-physical-relay-pxe.sh")))
  (testing "the evidence contract refuses to call a diagnostic ACK fleet participation"
    (let [contract (edn/read-string physical-relay-contract)]
      (is (= :unverified (get-in contract [:evidence :physical-state])))
      (is (= :diagnostic-only (:scope contract)))
      (is (every? (set (:does-not-prove contract))
                  [:persistent-device-identity
                   :authenticated-murakumo-heartbeat
                   :job-claim :inference :result-return])))))

(deftest k16-relay-enrollment-keeps-readiness-behind-real-work
  (let [contract (edn/read-string murakumo-relay-contract)]
    (is (= :operator-service-token-on-mac-relay
           (get-in contract [:control-plane :authorization])))
    (is (false? (get-in contract [:reported-state :node/ready?])))
    (is (= [:node/model :node/capacity]
           (get-in contract [:reported-state :withheld-until-real-work])))
    (is (false? (get-in contract [:safety :service-token-in-efi?])))
    (is (false? (get-in contract [:safety :service-token-on-wire-to-k16?])))
    (is (= :unverified (get-in contract [:evidence :physical-k16])))
    (is (every? (set (:does-not-prove contract))
                [:device-owned-persistent-key :ready-capacity :job-claim
                 :inference :result-return :heartbeat-renewal
                 :reboot-recovery]))))
