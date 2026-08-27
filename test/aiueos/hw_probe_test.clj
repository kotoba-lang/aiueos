(ns aiueos.hw-probe-test
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def source (slurp (io/file "os/aiueos/hw-probe/main.c")))
(def build-script (slurp (io/file "os/aiueos/scripts/build-hw-probe.sh")))
(def qualification-builder
  (slurp (io/file "os/aiueos/scripts/make-physical-qualification-usb.py")))
(def qualification-runtime
  (slurp (io/file "os/aiueos/kernel/qualification.c")))
(def uefi-loader-source (slurp (io/file "os/aiueos/uefi/main.c")))
(def result-checker
  (slurp (io/file "os/aiueos/scripts/check-physical-qualification-result.sh")))
(def macos-watcher
  (slurp (io/file "os/aiueos/scripts/install-macos-result-watcher.sh")))
(def qualification-contract
  (slurp (io/file "os/aiueos/contracts/physical-qualification-usb-v4.edn")))
(def qualification-v5-contract
  (slurp (io/file "os/aiueos/contracts/physical-qualification-usb-v5.edn")))
(def qualification-v6-contract
  (slurp (io/file "os/aiueos/contracts/physical-qualification-usb-v6.edn")))

(deftest probe-is-bounded-and-keeps-internal-disks-read-only
  (testing "the probe never reaches raw block writes or exits boot services"
    (is (not (str/includes? source "exit_boot_services(")))
    (is (not (str/includes? source "->write_blocks(")))
    (is (not (re-find #"pci\.write\s*\(" source)))
    (is (not (re-find #"mem\.write\s*\(" source)))
    (is (not (str/includes? source "outb")))
    (is (str/includes? source "MAX_PCI_HANDLES 256")))
  (testing "required evidence is explicit"
    (doseq [marker ["AIUEOS_HW_PROBE_FIRMWARE" "AIUEOS_HW_PROBE_GOP"
                    "AIUEOS_HW_PROBE_MEMORY" "AIUEOS_HW_PROBE_ACPI"
                    "AIUEOS_HW_PROBE_PCI" "AIUEOS_HW_PROBE_BLOCK"
                    "AIUEOS_HW_PROBE_DONE" "AIUEOS_HW_PROBE_CHAINLOAD_OK"
                    "AIUEOS_HW_PROBE_LOG_SAVED"
                    "AIUEOS_PHYSICAL_QUALIFICATION_RESULT_SAVED"]]
      (is (str/includes? source marker)))))

(deftest result-return-is-bounded-to-the-same-usb-data-partition
  (is (str/includes? qualification-contract ":scope :same-usb-result-partition-guid")
      "contract language must name the GUID-and-parent write boundary")
  (is (str/includes? source "result_partition_guid"))
  (is (str/includes? source "1cd9b207"))
  (is (str/includes? source "AIUEOS_K16_RESULT_VOLUME_V4"))
  (is (str/includes? source "bytes_equal(candidate_path,loaded_path,loaded_prefix)"))
  (is (str/includes? source "\\\\PROBE.LOG"))
  (is (str/includes? source "\\\\RESULT.LOG"))
  (is (str/includes? source "boot_next_name"))
  (is (str/includes? source "qualification_variable_cleared=yes"))
  (is (str/includes? qualification-runtime "firmware NVRAM"))
  (is (str/includes? qualification-runtime "sizeof(record)"))
  (is (not (str/includes? qualification-runtime "block_io")))
  (is (str/includes? qualification-contract ":internal-disk-write-code-reached? false"))
  (is (str/includes? qualification-contract ":uefi-variable-bytes 16")))

(deftest xhci-dbc-inventory-is-bounded-and-read-only
  (is (str/includes? source "MAX_XHCI_EXT_CAPS 50"))
  (is (str/includes? source "read_xhci_mmio32"))
  (is (str/includes? source "(header&0xffU)==10U"))
  (is (str/includes? source "offset+0x24"))
  (is (str/includes? source "AIUEOS_HW_PROBE_XHCI_DBC_SUMMARY"))
  (is (str/includes? qualification-contract ":live-stream-state :gated-on-physical-present-result-and-port-cable-validation")))

(deftest build-is-deterministic-and-separate
  (is (str/includes? build-script "/timestamp:0"))
  (is (str/includes? build-script "SOURCE_DATE_EPOCH"))
  (is (not (str/includes? build-script "kernel/main.c")))
  (is (not (str/includes? build-script "kernel/pci.c"))))

(deftest gop-discovery-falls-back-beyond-console-out
  (is (str/includes? source "*source=\"protocol-scan\""))
  (is (str/includes? source "locate_handle(EFI_BY_PROTOCOL,&gop_guid"))
  (is (str/includes? uefi-loader-source "AIUEOS_GOP_DISCOVERY_OK source=protocol-scan"))
  (is (str/includes? uefi-loader-source
                     "locate_handle(EFI_BY_PROTOCOL,&graphics_output_guid"))
  (is (str/includes? build-script "AIUEOS_GOP_FORCE_PROTOCOL_SCAN")))

(deftest physical-loader-failures-return-a-machine-readable-result
  (is (str/includes? uefi-loader-source "persist_loader_failure"))
  (is (str/includes? uefi-loader-source
                     "fail(110,\"AIUEOS_LOADER_FAIL segment-allocation\")"))
  (is (str/includes? uefi-loader-source
                     "AIUEOS_LOADER_FAILURE_RESULT_PERSISTED"))
  (is (str/includes? uefi-loader-source
                     "#ifndef AIUEOS_PHYSICAL_QUALIFICATION"))
  (is (str/includes? source
                     "AIUEOS_HW_PROBE_CHAINLOAD_RESULT_COLLECTED"))
  (is (str/includes? qualification-v5-contract "110 :segment-allocation"))
  (is (str/includes? qualification-v5-contract
                     ":collected-without-reboot true"))
  (let [start-returned (.indexOf source
                                 "AIUEOS_HW_PROBE_CHAINLOAD_FAIL stage=start-returned")
        collect-after-return (.indexOf source
                                       "collect_terminal_result(image,system)"
                                       start-returned)]
    (is (<= 0 start-returned))
    (is (< start-returned collect-after-return))))

(deftest incomplete-results-retain-the-physical-failure-reason
  (is (str/includes? result-checker "reason=gop-absent"))
  (let [state-branch (.indexOf result-checker "case \"$state\"")
        success-probe-gate (.indexOf result-checker
                                     "AIUEOS_HW_PROBE_GOP capability=present")]
    (is (<= 0 state-branch))
    (is (<= 0 success-probe-gate))
    (is (< state-branch success-probe-gate))))

(deftest physical-loader-hangs-leave-a-durable-progress-code
  (is (str/includes? uefi-loader-source "persist_loader_record(0,code)"))
  (is (str/includes? uefi-loader-source "AIUEOS_LOADER_PROGRESS loaded-image-protocol"))
  (is (str/includes? uefi-loader-source "AIUEOS_LOADER_PROGRESS exit-boot-services"))
  (is (str/includes? uefi-loader-source "AIUEOS_LOADER_WATCHDOG_ARMED"))
  (is (str/includes? qualification-v6-contract "209 :exit-boot-services-or-native-core"))
  (is (str/includes? qualification-v6-contract ":forced-progress-code 290"))
  (let [progress-before-map (.indexOf uefi-loader-source
                                      "progress(209,\"AIUEOS_LOADER_PROGRESS exit-boot-services\")")
        final-map (.indexOf uefi-loader-source
                            "bs->get_memory_map(&memory_map_size" progress-before-map)
        exit (.indexOf uefi-loader-source "bs->exit_boot_services" final-map)]
    (is (<= 0 progress-before-map))
    (is (< progress-before-map final-map))
    (is (< final-map exit))))

(deftest qualification-image-preserves-the-internal-disk-read-only-boundary
  (is (str/includes? qualification-builder "uefi-probe-and-arm-return"))
  (is (str/includes? qualification-builder "uefi-result-collector"))
  (is (str/includes? qualification-builder "\"internal_disk_writes\": False"))
  (is (str/includes? qualification-builder "\"qualification_usb_log_writes\": True"))
  (is (str/includes? qualification-builder "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"))
  (is (str/includes? qualification-builder "AIUEOS RSLT"))
  (is (str/includes? qualification-builder "mac-user-mount"))
  (is (str/includes? qualification-builder "\"ssd_install\": False"))
  (is (str/includes? qualification-builder "release.dirent(\"..\", 0x10, 0)"))
  (is (str/includes? qualification-builder "AIUEOS_PHYSICAL_QUALIFICATION_USB_OK")))

(deftest macos-result-retrieval-needs-no-administrator
  (is (str/includes? result-checker "/Volumes/AIUEOS\\ RSLT"))
  (is (str/includes? result-checker "AIUEOS_K16_RESULT_V4"))
  (is (str/includes? macos-watcher "$HOME/Library/LaunchAgents"))
  (is (str/includes? macos-watcher "WatchPaths"))
  (is (str/includes? macos-watcher "AIUEOS_WATCHER_INSTALL_DRY_RUN"))
  (is (not (str/includes? macos-watcher "sudo"))))
