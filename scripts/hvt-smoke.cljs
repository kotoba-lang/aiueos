#!/usr/bin/env nbb
;; ADR-0014 V0+V1 smoke gate for `aiueos.hvt` (issue #110).
;;
;; Runs the JVM/FFM self-owned-VMM boot spike (`clojure -M:hvt …`, which needs a
;; real /dev/kvm) and asserts its EDN run receipt: the guest must emit the
;; expected serial string via MMIO traps AND halt. Two cases:
;;   - default: the raw-word guest (`clojure -M:hvt`)               -- V0
;;   - elf:     direct-load the aarch64 ELF fixture and boot it     -- V1 loader
;; The KVM syscalls are JVM-FFM (nbb has no FFM), so this harness orchestrates
;; the spike and checks the evidence rather than making the ioctls itself.
;;
;; Run inside the Linux/KVM VM (needs kvm-group access):
;;   nbb scripts/hvt-smoke.cljs

(ns hvt-smoke
  (:require ["child_process" :as cp]
            [clojure.edn :as edn]
            [clojure.string :as str]))

(def expected-serial "HI\n")
(def elf-fixture "resources/hvt/guest-aarch64.elf")

(defn run-spike [argv]
  (let [res (cp/spawnSync "clojure" (clj->js (into ["-M:hvt"] argv))
                          #js {:encoding "utf8" :cwd (js/process.cwd)})]
    {:status (.-status res)
     :stdout (or (.-stdout res) "")
     :stderr (or (.-stderr res) "")}))

(defn last-edn-line
  "The receipt is the last non-blank stdout line (clojure deps resolution may
  print earlier noise on first run)."
  [stdout]
  (->> (str/split-lines stdout)
       (map str/trim)
       (remove str/blank?)
       (filter #(str/starts-with? % "{"))
       last))

(defn check-case
  "Run one spike case; return true iff it met the gate."
  [label argv]
  (println (str "[hvt-smoke] " label ": clojure -M:hvt " (str/join " " argv) " ..."))
  (let [{:keys [status stdout stderr]} (run-spike argv)
        line (last-edn-line stdout)
        receipt (when line (try (edn/read-string line) (catch :default _ nil)))]
    (when (seq (str/trim stderr))
      (println "[hvt-smoke]   stderr:")
      (doseq [l (str/split-lines (str/trim stderr))] (println "   " l)))
    (if-not receipt
      (do (println "[hvt-smoke]   FAIL: no EDN receipt on stdout (KVM unavailable?).")
          (println stdout)
          false)
      (let [{:keys [serial serial-ok? shutdown? steps exits]} receipt
            ok? (and (= serial expected-serial) serial-ok? shutdown? (zero? status))]
        (println (str "[hvt-smoke]   receipt: serial=" (pr-str serial)
                      " serial-ok?=" serial-ok?
                      " shutdown?=" shutdown?
                      " steps=" steps
                      " mmio-exits=" (count (filter #(= :mmio (:reason %)) exits))))
        (println (str "[hvt-smoke]   " (if ok? "PASS" "FAIL") " -- " label))
        ok?))))

(defn -main []
  (let [v0 (check-case "V0 raw-word guest (MMIO poweroff halt)" [])
        v1 (check-case "V1 ELF direct-load (guest-aarch64.elf)" ["elf" elf-fixture])]
    (if (and v0 v1)
      (do (println "[hvt-smoke] PASS -- self-owned VMM boots both a raw guest and a direct-loaded ELF, serial via MMIO traps, clean halt.")
          (js/process.exit 0))
      (do (println "[hvt-smoke] FAIL -- one or more cases did not meet the gate.")
          (js/process.exit 1)))))

(-main)
