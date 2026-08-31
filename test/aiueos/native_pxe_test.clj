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
(def physical-direct-https-contract
  (slurp (io/file
          "os/aiueos/contracts/physical-direct-https-qualification-pxe-v1.edn")))
(def physical-persistent-worker-contract
  (slurp (io/file
          "os/aiueos/contracts/physical-persistent-worker-k16-v1.edn")))
(def physical-direct-https-build
  (slurp (io/file "os/aiueos/scripts/build-physical-direct-https-pxe.sh")))
(def physical-direct-https-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-physical-direct-https.sh")))
(def physical-relay-contract
  (slurp (io/file "os/aiueos/contracts/physical-relay-qualification-pxe-v1.edn")))
(def murakumo-relay-contract
  (slurp (io/file "os/aiueos/contracts/murakumo-relay-enrollment-v1.edn")))
(def micro-inference-contract
  (slurp (io/file "os/aiueos/contracts/micro-inference-qualification-v1.edn")))
(def qwen38-benchmark-contract
  (slurp (io/file "os/aiueos/contracts/qwen38-27b-k16-benchmark-v1.edn")))
(def inference-status
  (slurp (io/file "os/aiueos/kernel/inference_status.c")))
(def inference-status-header
  (slurp (io/file "os/aiueos/kernel/inference_status.h")))
(def inference-status-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-inference-status.sh")))
(def qwen38-fetch
  (slurp (io/file "os/aiueos/scripts/fetch-qwen38-27b-model.sh")))
(def qwen38-handoff-contract
  (slurp (io/file "os/aiueos/contracts/qwen38-model-handoff-v1.edn")))
(def qwen38-handoff
  (slurp (io/file "os/aiueos/kernel/model_handoff.c")))
(def qwen38-paging
  (slurp (io/file "os/aiueos/kernel/paging.c")))
(def qwen38-bundle
  (slurp (io/file "os/aiueos/scripts/prepare-qwen38-model-bundle.sh")))
(def qwen38-ranged-bundle
  (slurp (io/file "os/aiueos/scripts/fetch-qwen38-model-bundle.sh")))
(def qwen38-ranged-bundle-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qwen38-ranged-bundle.sh")))
(def qwen38-handoff-build
  (slurp (io/file "os/aiueos/scripts/build-qwen38-model-handoff-pxe.sh")))
(def qwen38-handoff-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-model-handoff.sh")))
(def qwen38-node-oneshot-build
  (slurp (io/file
          "os/aiueos/scripts/build-qwen38-murakumo-node-oneshot-pxe.sh")))
(def qwen38-node-persistent-build
  (slurp (io/file
          "os/aiueos/scripts/build-qwen38-murakumo-node-pxe.sh")))
(def device-worker-protocol
  (slurp (io/file "os/aiueos/kernel/device_worker_protocol.c")))
(def device-result
  (slurp (io/file "os/aiueos/kernel/device_result.c")))
(def device-worker-protocol-smoke
  (slurp (io/file
          "os/aiueos/scripts/smoke-device-worker-protocol.sh")))
(def model-slots-contract
  (slurp (io/file "os/aiueos/contracts/model-nvme-slots-v1.edn")))
(def model-slots-core (slurp (io/file "os/aiueos/uefi/model_slots.c")))
(def model-slots-smoke
  (slurp (io/file "os/aiueos/scripts/smoke-qemu-model-slots.sh")))
(def model-slots-build
  (slurp (io/file "os/aiueos/scripts/build-qwen38-model-slots-usb.sh")))
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
(def qualification-entry
  (slurp (io/file "os/aiueos/kernel/qualification_entry.S")))

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

(deftest qwen-node-one-shot-has-a-bounded-model-sized-watchdog
  (testing "the generic release keeps 90 seconds unless a physical profile opts in"
    (is (str/includes? build "qualification_watchdog_seconds"))
    (is (str/includes? build "must be 1..3600"))
    (is (str/includes? build "persistent boot disables the loader watchdog"))
    (is (str/includes? release-build
                       "AIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS:-90")))
  (testing "the exact Qwen node can return a result after its 10.9 GB admission"
    (doseq [marker ["AIUEOS_QWEN38_MODEL_HANDOFF=1"
                    "AIUEOS_MURAKUMO_DEVICE_RESULT=1"
                    "AIUEOS_PERSISTENT_BOOT=0"
                    "AIUEOS_QUALIFICATION_LOADER_WATCHDOG_SECONDS:-1800"]]
      (is (str/includes? qwen38-node-oneshot-build marker)))))

(deftest qwen-node-persistent-worker-owns-heartbeat-poll-and-result
  (doseq [marker ["AIUEOS_QWEN38_MODEL_HANDOFF=1"
                  "AIUEOS_MURAKUMO_DEVICE_RESULT=1"
                  "AIUEOS_PERSISTENT_BOOT=1"]]
    (is (str/includes? qwen38-node-persistent-build marker)))
  (doseq [marker ["AIUEOS_MURAKUMO_PERSISTENT_BOOT_OK"
                  "AIUEOS_MURAKUMO_HEARTBEAT_OK"
                  "AIUEOS_MURAKUMO_JOB_POLL_OK"
                  "AIUEOS_MURAKUMO_JOB_CLAIMED"
                  "AIUEOS_MURAKUMO_JOB_RESULT_OK"
                  "AIUEOS_MURAKUMO_CONTROL_RECEIVED"
                  "AIUEOS_MURAKUMO_CONTROL_ACK_OK"
                  "aiueos_rtl8125_device_worker_poll"
                  "aiueos_rtl8125_device_worker_control_ack"
                  "aiueos_rtl8125_device_worker_result"]]
    (is (or (str/includes? kernel marker) (str/includes? pci marker))))
  (doseq [marker ["\\\"accepted\\\":true"
                  "\\\"operation\\\":\\\"poll\\\""
                  "\\\"control\\\":{\\\"action\\\":\\\"reboot-pxe\\\""
                  "\\\"command-id\\\":\\\""
                  "\\\"job-id\\\":\\\""
                  "\\\"bos\\\":"
                  "\\\"job\\\":null"]]
    (is (str/includes? device-worker-protocol marker)))
  (is (str/includes? device-worker-protocol-smoke
                     "device_worker_protocol_model.c")))

(deftest network-reboot-acks-before-uefi-runtime-reset
  (testing "the signed worker protocol has a result-free control ACK"
    (doseq [marker ["AIUEOS_DEVICE_WORKER_CONTROL_ACK"
                    "\"control-ack\""
                    "worker->token || worker->second_token || worker->inference_cycles"]]
      (is (str/includes? device-result marker))))
  (is (str/includes? release-build
                     "\"ack\": \"signed-device-p256-before-reset\""))
  (testing "the node resets only after the acknowledgement returns HTTP 2xx"
    (let [ack (.indexOf kernel "AIUEOS_MURAKUMO_CONTROL_ACK_OK")
          reset (.indexOf kernel "(void)aiueos_qualification_reboot();")]
      (is (<= 0 ack))
      (is (< ack reset)))
    (is (str/includes? pci "rtl8125_direct_device_request(request_length)")))
  (testing "Runtime Services are entered under the retained firmware CR3"
    (doseq [marker ["aiueos_qualification_reboot_firmware"
                    "runtime->reset_system(0, EFI_SUCCESS, 0, 0)"]]
      (is (str/includes? qualification-runtime marker)))
    (doseq [marker [".global aiueos_qualification_reboot"
                    "call aiueos_qualification_reboot_firmware"
                    "mov %rax, %cr3"]]
      (is (str/includes? qualification-entry marker))))

  (testing "worker-only scratch does not relax the low W^X aperture"
    (is (str/includes? device-result "AIUEOS_DEVICE_HIGH_BSS"))
    (is (str/includes? pci
                       "rtl_direct_http_request[1024]\n  __attribute__((section(\".high_bss\")))"))
    (is (str/includes? pci
                       "kotoba_app_objects[KOTOBA_APP_CAPACITY][12288]\n  __attribute__((section(\".high_bss\")))"))
    (is (str/includes? pci
                       "rtl8125_direct_tls_flight[RTL_DIRECT_TLS_FLIGHT_MAX]\n  __attribute__((section(\".high_bss\")))"))))

(deftest physical-k16-network-reboot-evidence-is-bounded
  (let [evidence (edn/read-string physical-persistent-worker-contract)]
    (is (= "e066c8753550b05f5dff67cbebe10fdc50db147c"
           (get-in evidence [:artifact :source-commit])))
    (is (= "3c6f2ffb14fd721f" (get-in evidence [:first-boot :boot-id])))
    (is (= "512c9d8804f58d31" (get-in evidence [:second-boot :boot-id])))
    (is (true? (get-in evidence [:second-boot :boot-id-changed?])))
    (is (= :passed (get-in evidence [:result :signed-control-ack])))
    (is (= :passed (get-in evidence [:result :network-triggered-reboot])))
    (is (= :passed
           (get-in evidence [:result :post-reboot-murakumo-reconnect])))
    (is (= :passed (get-in evidence [:result :targeted-job-claim])))
    (is (= :passed (get-in evidence [:result :physical-qwen-first-token])))
    (is (= 3 (get-in evidence [:targeted-inference :runs])))
    (is (= 47139022281
           (get-in evidence
                   [:targeted-inference :median-time-to-first-token-ns])))
    (is (= :not-captured-in-opaque-netlog
           (get-in evidence [:result :server-ready-field])))
    (is (every? (set (:does-not-prove evidence))
                [:server-ready-true :multi-token-decode-throughput
                 :gpu-offload :standalone-k16-internet
                 :device-owned-cacao :passkey-account-binding
                 :post-quantum-authentication :biscuit-authorization
                 :ssd-installation]))))

(deftest device-worker-refreshes-boot-id-across-warm-pxe-reboots
  (testing "the once-per-boot qualification request replaces retained BSS state"
    (is (str/includes? device-result
                       "!device_boot_id_refresh()"))
    (is (str/includes? device-result
                       "A warm PXE reboot may reuse the same physical pages")))
  (testing "poll and result requests keep one id throughout the current boot"
    (is (str/includes? device-result
                       "!random_scalar(nonce_k) || !device_boot_id_ensure()"))))

(deftest direct-node-post-closes-and-retries-with-a-bounded-flight
  (testing "a server FIN terminates the current receive pump"
    (doseq [marker ["if (frame[47] & NET_TCP_FIN)"
                    "return 0;"
                    "RTL_DIRECT_RX_BUDGET 50000000U"]]
      (is (str/includes? pci marker))))
  (testing "a complete HTTP response is acknowledged and actively closed"
    (doseq [marker ["#define NET_TCP_RST 0x04"
                    "NET_TCP_RST | NET_TCP_ACK"
                    "those stale frames hid the third connection's SYN-ACK"]]
      (is (str/includes? pci marker))))
  (testing "a new connection scans past a bounded stale server flight"
    (doseq [marker ["RTL_DIRECT_SYN_SCAN_FRAMES 64U"
                    "more than eight such frames after two successful polls"
                    "RTL_DIRECT_SYN_SCAN_FRAMES, &received"]]
      (is (str/includes? pci marker))))
  (testing "each connection restarts the owned rings after a bounded FIFO drain"
    (doseq [marker ["aiueos_rtl8125_restart(&rtl8125_qualification_device)"
                    "an empty, bounded receive engine"
                    "RTL_DIRECT_STAGE_ERROR(15)"]]
      (is (str/includes? pci marker)))
    (doseq [marker ["aiueos_rtl8125_restart"
                    "restart=bounded-fifo-flush"]]
      (is (or (str/includes? rtl8125 marker)
              (str/includes? (slurp (io/file "os/aiueos/tests/rtl8125_handoff_model.c"))
                             marker)))))
  (testing "a signed node POST gets fresh TLS material on up to three attempts"
    (doseq [marker ["RTL_DIRECT_TLS_ATTEMPTS 3U"
                    "RTL_DIRECT_LOCAL_PORT + (lane % 12000U)"
                    "RTL_DIRECT_ISN + (lane << 16) + lane"
                    "aiueos_cpu_random_bytes(rtl_direct_client_random"
                    "rtl8125_direct_https_attempts = attempt + 1"]]
      (is (str/includes? pci marker))))
  (testing "Finished plus a maximum 1024-byte HTTP request cannot overflow"
    (is (str/includes? pci "RTL_DIRECT_TLS_FLIGHT_MAX 1152U"))
    (is (str/includes? pci
                       "RTL_DIRECT_TLS_FLIGHT_MAX >= 58U + 1024U + 22U"))
    (is (not (str/includes? pci "client_hello[256], flight[512]"))))
  (testing "the retained NVRAM result separates every local TLS-flight gate"
    (doseq [marker ["request_length > sizeof(rtl8125_direct_tls_flight) - 80U"
                    "RTL_DIRECT_STAGE_ERROR(9)"
                    "RTL_DIRECT_STAGE_ERROR(12)"
                    "RTL_DIRECT_STAGE_ERROR(13)"
                    "RTL_DIRECT_STAGE_ERROR(14)"]]
      (is (str/includes? pci marker))))
  (testing "the one-descriptor K16 path advertises one bounded receive slot"
    (is (str/includes? pci "RTL_DIRECT_RX_BUDGET 50000000U"))
    (is (str/includes? pci "RTL_DIRECT_RX_WINDOW 1024U"))
    (is (str/includes? pci "net_tx_window = RTL_DIRECT_RX_WINDOW"))
    (is (not (str/includes? pci "((stage) + 1U)"))))
  (testing "a failed persistent request resets RX but keeps boot-scoped DNS"
    (is (str/includes? pci
                       "aiueos_rtl8125_rx_rearm(&rtl8125_qualification_device);"))
    (doseq [marker ["retain the DNS answer already verified"
                    "The direct-link bridge is the fixed 10.77.0.1 endpoint"]]
      (is (str/includes? pci marker)))
    (is (not (str/includes?
              pci
              "aiueos_rtl8125_rx_rearm(&rtl8125_qualification_device);\n  rtl8125_direct_dns_a = 0;\n  net_tx_window = NET_TCP_WINDOW;"))))
  (testing "each bounded DNS receive gets a fresh reply after a stale TLS frame"
    (doseq [marker ["Pair every"
                    "bounded receive attempt with a fresh DNS query"
                    "for (unsigned attempt = 0; attempt < 8; attempt++)"
                    "if (!rtl8125_direct_tx(bytes)) return 0;"
                    "if (!rtl8125_direct_rx(&received)) continue;"]]
      (is (str/includes? pci marker))))
  (testing "runtime ARP refresh remains bound to the admitted Mac peer"
    (doseq [marker ["aiueos_rtl8125_direct_arp_request"
                    "aiueos_rtl8125_direct_arp_reply"
                    "RTL_DIRECT_IP, RTL_DIRECT_GATEWAY"
                    "if (!sent) return 0;"]]
      (is (str/includes? pci marker)))
    (doseq [marker ["load_be32(frame + 28) != peer_ip"
                    "load_be32(frame + 38) != local_ip"
                    "arp=peer-bound-reply"]]
      (is (or (str/includes? rtl8125 marker)
              (str/includes? (slurp (io/file "os/aiueos/tests/rtl8125_handoff_model.c"))
                             marker)))))
  (testing "a physical pump refusal names the exact receive gate"
    (doseq [marker ["30U + rtl8125_direct_tls_pump_error"
                    "rtl8125_direct_tls_pump_error = 4"
                    "rtl8125_direct_tls_pump_error = 7"]]
      (is (str/includes? pci marker)))))

(deftest physical-worker-reports-bounded-response-diagnostics
  (testing "a reboot identifies transport, parse and result response outcomes"
    (doseq [marker ["AIUEOS_WORKER_RX "
                    "rtl8125_direct_worker_rx_report(sequence, 'R')"
                    "rtl8125_direct_worker_rx_report(sequence, 'P')"
                    "rtl8125_direct_worker_rx_report(sequence, 'O')"
                    "ok ? 'o' : 'F'"]]
      (is (str/includes? pci marker))))
  (testing "the UDP report is bounded and excludes the response body"
    (doseq [marker ["uint32_t fields[4]"
                    "app_length < 12U ? app_length : 12U"
                    "0x8000U | (sequence & 0x7fffU)"]]
      (is (str/includes? pci marker)))
    (is (str/includes? pci
                       "response JSON, signatures and device-private material are"))))

(deftest persistent-node-reconnects-instead-of-halting-on-one-missed-renewal
  (doseq [marker ["NODE RECONNECTING"
                  "AIUEOS_PHYSICAL_LIVENESS_RETRY"
                  "AIUEOS_PHYSICAL_LIVENESS_RECOVERED"]]
    (is (str/includes? kernel marker)))
  (is (not (str/includes?
            kernel
            "aiueos_framebuffer_qualification_screen(\"AIUEOS K16\", \"NODE LINK STALE\", \"SSD READ ONLY\", 0);\n      for(;;)__asm__ volatile(\"cli; hlt\");"))
      "one renewal miss must not permanently halt an otherwise qualified node"))

(deftest persistent-node-reconnects-after-an-ambiguous-initial-post
  (testing "a retained device DID lets persistent boot enter the worker loop"
    (doseq [marker ["int direct_https_ok = 0"
                    "AIUEOS_MURAKUMO_INITIAL_POST_AMBIGUOUS"
                    "action=worker-reconnect"
                    "if(!aiueos_rtl8125_direct_device_did()[0])"
                    "int persistent_qualification_recorded = direct_https_ok"]]
      (is (str/includes? kernel marker))))
  (testing "the qualification is recorded only after Murakumo accepts a worker request"
    (doseq [marker ["if (!persistent_qualification_recorded)"
                    "AIUEOS_MURAKUMO_INITIAL_POST_RECOVERED"
                    "aiueos_qualification_finalize(1,8162)"]]
      (is (str/includes? kernel marker))))
  (testing "one-shot builds still fail closed and confirmed responses keep the HTTP gate"
    (is (str/includes? kernel
                       "if(direct_https_ok && !aiueos_rtl8125_direct_http_ready())"))
    (is (str/includes? kernel
                       "#if defined(AIUEOS_MURAKUMO_DEVICE_RESULT) && defined(AIUEOS_PERSISTENT_BOOT)"))
    (is (str/includes? kernel
                       "for(;;)__asm__ volatile(\"cli; hlt\");"))))

(deftest native-inference-measurement-and-targeting-stay-evidence-bounded
  (let [contract (edn/read-string micro-inference-contract)]
    (is (= :serialized-tsc-cycle-delta
           (get-in contract [:measurement :native-compute])))
    (is (= :unqualified-until-tsc-frequency-is-measured
           (get-in contract [:measurement :native-wall-time])))
    (is (= :target-did (get-in contract [:murakumo :target-field])))
    (is (some #{:network-direct-to-murakumo} (:does-not-prove contract))))
  (doseq [marker ["cycles=" "inference-cycles" "relay-round-trip-ns"
                  "target-did" "murakumo_queue_path"]]
    (is (str/includes? server marker)))
  (is (str/includes? pci "rdtscp; lfence"))
  (is (not (str/includes?
            pci
            "!rtl8125_qualification_device.ready||rtl8125_relay_error||rtl8125_job_error"))
      "one failed job must not permanently suppress future heartbeat handling"))

(deftest qwen38-k16-benchmark-records-physical-murakumo-generation
  (let [contract (edn/read-string qwen38-benchmark-contract)]
    (is (= "Qwen/Qwen3.8-27B" (get-in contract [:model :id])))
    (is (= "Qwen3.8-27B-UD-IQ3_XXS.gguf"
           (get-in contract [:artifact :file])))
    (is (= 10934860704 (get-in contract [:artifact :bytes])))
    (is (= "c0b7c3038681ed2e3040456c1dd45f9858b6c2290bed172c70388a94874f3eee"
           (get-in contract [:artifact :sha256])))
    (is (= "512c9d8804f58d31" (get-in contract [:observed :boot-id])))
    (is (= 135792741870 (get-in contract [:observed :model-load :ns])))
    (is (= 3 (get-in contract [:observed :summary :runs])))
    (is (= 3 (get-in contract [:observed :summary :reference-matches])))
    (is (= 47139022281 (get-in contract [:observed :summary :median-ns])))
    (is (= :not-available
           (get-in contract [:observed :summary :decode-tokens-per-second])))
    (is (= #{2005}
           (set (map :token (get-in contract [:observed :murakumo-jobs])))))
    (is (= 31 (get-in contract [:observed
                                :communication-through-sequence-76
                                :failed-exchanges-followed-by-recovery])))
    (is (zero? (get-in contract [:observed
                                 :communication-through-sequence-76
                                 :unrecovered-at-snapshot])))
    (is (some #{:zero-for-missing-timing}
              (get-in contract [:measurement :forbidden-substitutions])))
    (is (every? (set (:does-not-prove contract))
                [:multi-token-generation :decode-tokens-per-second
                 :threaded-or-simd-throughput :gpu-offload
                 :network-round-trip-latency
                 :production-grade-link-reliability
                 :standalone-k16-internet :ssd-installation])))
  (doseq [marker ["AIUEOS_INFERENCE_UNMEASURED"
                  "aiueos_inference_milli_tokens_per_second"
                  "compute_cycles"]]
    (is (or (str/includes? inference-status marker)
            (str/includes? inference-status-header marker))))
  (is (str/includes? inference-status-smoke
                     "inference_status_screen_model.c"))
  (doseq [marker ["4ca720788d1e01f1bff70c033e0d0028fd02e502"
                  "10934860704"
                  "c0b7c3038681ed2e3040456c1dd45f9858b6c2290bed172c70388a94874f3eee"
                  "artifact plus 2 GiB headroom"]]
    (is (str/includes? qwen38-fetch marker)))
  (is (str/includes? kernel "aiueos_framebuffer_inference_screen")))

(deftest qwen38-pure-aiueos-handoff-is-exact-immutable-and-not-generation
  (let [contract (edn/read-string qwen38-handoff-contract)]
    (is (= [4000000000 4000000000 2934860704]
           (mapv :bytes (get-in contract [:bundle :parts]))))
    (is (= [:supervisor :read-only :nx]
           (get-in contract [:kernel :mapping :permissions])))
    (is (= :passed (get-in contract [:evidence :physical-k16])))
    (is (= :absent (get-in contract [:evidence :real-generation])))
    (is (false? (get-in contract [:safety :internal-disk-writes?])))
    (is (every? (set (:does-not-prove contract))
                [:qwen38-generation :physical-k16-throughput])))
  (doseq [marker ["HUGE_PAGE_SIZE" "AIUEOS_MODEL_RESERVED_APIC_START"
                  "version == 3U"]]
    (is (str/includes? qwen38-handoff marker)))
  (doseq [marker ["PTE_PRESENT | PTE_HUGE | PTE_NX"
                  "deliberately not writable"]]
    (is (str/includes? qwen38-paging marker)))
  (doseq [marker ["10934860704"
                  "c0b7c3038681ed2e3040456c1dd45f9858b6c2290bed172c70388a94874f3eee"
                  "Q38P0.BIN" "model-bundle-v1.json"]]
    (is (str/includes? qwen38-bundle marker)))
  (is (str/includes? qwen38-handoff-build
                     "AIUEOS_QWEN38_MODEL_HANDOFF=1"))
  (doseq [marker ["AIUEOS_QWEN38_MODEL_HANDOFF_QEMU_OK"
                  "AIUEOS_QWEN38_MODEL_HANDOFF_QEMU_REFUSAL_OK"
                  "generation=not-yet-present"]]
    (is (str/includes? qwen38-handoff-smoke marker))))

(deftest pure-aiueos-model-cache-writes-only-guarded-inactive-nvme-slot
  (let [contract (edn/read-string model-slots-contract)]
    (is (= :nvme-namespace-required (get-in contract [:target :device-path])))
    (is (= [:inactive-data :flush :full-readback-sha256
            :inactive-header :flush-and-readback
            :alternate-selector :flush-and-readback]
           (get-in contract [:update :write-order])))
    (is (= :last-known-good (get-in contract [:update :interrupted])))
    (is (false? (get-in contract [:safety :writes-without-anchor?])))
    (is (false? (get-in contract [:safety :writes-windows-partition?])))
    (is (= :unverified (get-in contract [:evidence :physical-k16-nvme-write]))))
  (doseq [marker ["anchor_magic" "AIUEOS_MODEL_SLOT_RECORD_SELECTOR"
                  "aiueos_model_slot_commit" "aiueos_model_slot_verify_active"]]
    (is (str/includes? model-slots-core marker)))
  (doseq [marker ["AIUEOS_MODEL_SLOTS_QEMU_OK" "generations=1,2"
                  "corrupt-update=last-known-good"]]
    (is (str/includes? model-slots-smoke marker)))
  (doseq [marker ["AIUEOS_MODEL_NVME_SLOTS=1"
                  "AIUEOS_MODEL_NVME_TARGET_OPTIONAL=1"
                  "AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION=1"]]
    (is (str/includes? model-slots-build marker)))
  (doseq [marker ["--range" "--max-filesize" "Q38P0.BIN"
                  "transport=https-range"]]
    (is (str/includes? qwen38-ranged-bundle marker)))
  (is (str/includes? qwen38-ranged-bundle-smoke
                     "AIUEOS_QWEN38_RANGED_BUNDLE_OK"))
  (is (str/includes? loader "AIUEOS_MODEL_SLOT_DEFERRED")))

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
    (doseq [marker ["aiueos_rtl8125_takeover" "aiueos_rtl8125_restart"
                    "RGE_DESC_OWN"
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

(deftest k16-direct-https-image-carries-no-secret-or-mac-application-relay
  (testing "RTL8125 owns DNS, TCP, TLS and the public Murakumo GET"
    (doseq [marker ["rtl8125_direct_dns"
                    "rtl8125_direct_tcp_send"
                    "aiueos_tls13_configure"
                    "GET /infer/queue HTTP/1.1"
                    "AIUEOS_PHYSICAL_DIRECT_HTTPS_OK"]]
      (is (or (str/includes? pci marker)
              (str/includes? kernel marker))))
    (is (str/includes? physical-direct-https-build
                       "AIUEOS_PHYSICAL_DIRECT_HTTPS_QUALIFICATION=1"))
    (is (str/includes? physical-direct-https-smoke
                       "build-physical-direct-https-pxe.sh")))
  (testing "the contract keeps source/build separate from a physical reboot"
    (let [contract (edn/read-string physical-direct-https-contract)]
      (is (= :transport-only (get-in contract [:transport :trust])))
      (is (= :unverified (get-in contract [:evidence :physical-state])))
      (is (= :none (get-in contract [:request :secrets])))
      (is (false? (get-in contract [:request :mac-application-termination?])))
      (is (true? (get-in contract [:request :mac-opaque-l4-forwarder?])))
      (is (false? (get-in contract [:safety :account-token-in-image?])))
      (is (false? (get-in contract [:safety :wifi-secret-in-image?])))
      (is (false? (get-in contract [:safety :cacao-in-image?])))
      (is (every? (set (:does-not-prove contract))
                  [:trusted-https :device-owned-cacao :account-claim
                   :wifi-application :authenticated-murakumo-heartbeat
                   :inference :reboot-recovery])))))

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
