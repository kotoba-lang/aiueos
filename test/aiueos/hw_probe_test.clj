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
(def qualification-contract
  (slurp (io/file "os/aiueos/contracts/physical-qualification-usb-v2.edn")))

(deftest probe-is-bounded-and-keeps-internal-disks-read-only
  (testing "the probe never reaches raw block writes or exits boot services"
    (is (not (str/includes? source "exit_boot_services(")))
    (is (not (str/includes? source "->write_blocks(")))
    (is (not (re-find #"pci\.write\s*\(" source)))
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

(deftest result-return-is-bounded-to-uefi-and-the-loaded-usb-filesystem
  (is (str/includes? qualification-contract ":scope :same-loaded-image-filesystem")
      "contract language must name the self-only write boundary")
  (is (str/includes? source "\\\\EFI\\\\AIUEOS\\\\PROBE.LOG"))
  (is (str/includes? source "\\\\EFI\\\\AIUEOS\\\\RESULT.LOG"))
  (is (str/includes? source "boot_next_name"))
  (is (str/includes? source "qualification_variable_cleared=yes"))
  (is (str/includes? qualification-runtime "firmware NVRAM"))
  (is (str/includes? qualification-runtime "sizeof(record)"))
  (is (not (str/includes? qualification-runtime "block_io")))
  (is (str/includes? qualification-contract ":internal-disk-write-code-reached? false"))
  (is (str/includes? qualification-contract ":uefi-variable-bytes 16")))

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

(deftest incomplete-results-retain-the-physical-failure-reason
  (is (str/includes? result-checker "reason=gop-absent"))
  (let [state-branch (.indexOf result-checker "case \"$state\"")
        success-probe-gate (.indexOf result-checker
                                     "AIUEOS_HW_PROBE_GOP capability=present")]
    (is (<= 0 state-branch))
    (is (<= 0 success-probe-gate))
    (is (< state-branch success-probe-gate))))

(deftest qualification-image-preserves-the-internal-disk-read-only-boundary
  (is (str/includes? qualification-builder "uefi-probe-and-arm-return"))
  (is (str/includes? qualification-builder "uefi-result-collector"))
  (is (str/includes? qualification-builder "\"internal_disk_writes\": False"))
  (is (str/includes? qualification-builder "\"qualification_usb_log_writes\": True"))
  (is (str/includes? qualification-builder "\"ssd_install\": False"))
  (is (str/includes? qualification-builder "release.dirent(\"..\", 0x10, 0)"))
  (is (str/includes? qualification-builder "AIUEOS RSLT"))
  (is (str/includes? qualification-builder "AIUEOS_PHYSICAL_QUALIFICATION_USB_OK")))
