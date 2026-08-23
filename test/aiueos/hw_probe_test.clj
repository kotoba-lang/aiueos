(ns aiueos.hw-probe-test
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def source (slurp (io/file "os/aiueos/hw-probe/main.c")))
(def build-script (slurp (io/file "os/aiueos/scripts/build-hw-probe.sh")))

(deftest probe-is-bounded-and-read-only
  (testing "the standalone probe never takes over the machine or imports disk I/O"
    (is (not (str/includes? source "exit_boot_services(")))
    (is (not (str/includes? source "block_io")))
    (is (not (re-find #"pci\.write\s*\(" source)))
    (is (not (str/includes? source "outb")))
    (is (str/includes? source "MAX_PCI_HANDLES 64")))
  (testing "required evidence is explicit"
    (doseq [marker ["AIUEOS_HW_PROBE_FIRMWARE" "AIUEOS_HW_PROBE_GOP"
                    "AIUEOS_HW_PROBE_MEMORY" "AIUEOS_HW_PROBE_ACPI"
                    "AIUEOS_HW_PROBE_PCI" "AIUEOS_HW_PROBE_DONE"]]
      (is (str/includes? source marker)))))

(deftest build-is-deterministic-and-separate
  (is (str/includes? build-script "/timestamp:0"))
  (is (str/includes? build-script "SOURCE_DATE_EPOCH"))
  (is (not (str/includes? build-script "kernel/main.c")))
  (is (not (str/includes? build-script "kernel/pci.c"))))
