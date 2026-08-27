(ns aiueos.hw-probe-test
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def source (slurp (io/file "os/aiueos/hw-probe/main.c")))
(def build-script (slurp (io/file "os/aiueos/scripts/build-hw-probe.sh")))
(def qualification-builder
  (slurp (io/file "os/aiueos/scripts/make-physical-qualification-usb.py")))

(deftest probe-is-bounded-and-read-only
  (testing "the probe never writes a disk or exits boot services"
    (is (not (str/includes? source "exit_boot_services(")))
    (is (not (str/includes? source "->write_blocks(")))
    (is (not (re-find #"pci\.write\s*\(" source)))
    (is (not (str/includes? source "outb")))
    (is (str/includes? source "MAX_PCI_HANDLES 256")))
  (testing "required evidence is explicit"
    (doseq [marker ["AIUEOS_HW_PROBE_FIRMWARE" "AIUEOS_HW_PROBE_GOP"
                    "AIUEOS_HW_PROBE_MEMORY" "AIUEOS_HW_PROBE_ACPI"
                    "AIUEOS_HW_PROBE_PCI" "AIUEOS_HW_PROBE_BLOCK"
                    "AIUEOS_HW_PROBE_DONE" "AIUEOS_HW_PROBE_CHAINLOAD_OK"]]
      (is (str/includes? source marker)))))

(deftest build-is-deterministic-and-separate
  (is (str/includes? build-script "/timestamp:0"))
  (is (str/includes? build-script "SOURCE_DATE_EPOCH"))
  (is (not (str/includes? build-script "kernel/main.c")))
  (is (not (str/includes? build-script "kernel/pci.c"))))

(deftest qualification-image-preserves-the-read-only-boundary
  (is (str/includes? qualification-builder "uefi-read-only-hardware-probe"))
  (is (str/includes? qualification-builder "native-core-read-only"))
  (is (str/includes? qualification-builder "\"internal_disk_writes\": False"))
  (is (str/includes? qualification-builder "\"ssd_install\": False"))
  (is (str/includes? qualification-builder "AIUEOS_PHYSICAL_QUALIFICATION_USB_OK")))
