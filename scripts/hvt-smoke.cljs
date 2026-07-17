#!/usr/bin/env nbb
;; ADR-0014 V0 smoke gate for `aiueos.hvt` (issue #110).
;;
;; Runs the JVM/FFM self-owned-VMM boot spike (`clojure -M:hvt`, which needs a
;; real /dev/kvm) and asserts its EDN run receipt: the guest must have emitted
;; the expected serial string via MMIO traps AND requested PSCI SYSTEM_OFF.
;; This is the nbb verification harness ADR-0014 calls for -- the KVM syscalls
;; themselves are JVM-FFM (nbb has no FFM), so this harness orchestrates the
;; spike and checks the evidence, rather than making the ioctls itself.
;;
;; Run inside the Linux/KVM VM (needs kvm-group access):
;;   nbb scripts/hvt-smoke.cljs
;; or, if nbb isn't present, the same assertion is trivially reproducible by
;; eye from `clojure -M:hvt`'s printed receipt.

(ns hvt-smoke
  (:require ["child_process" :as cp]
            [clojure.edn :as edn]
            [clojure.string :as str]))

(def expected-serial "HI\n")

(defn run-spike []
  (let [res (cp/spawnSync "clojure" #js ["-M:hvt"]
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

(defn -main []
  (println "[hvt-smoke] running aiueos.hvt KVM boot spike (clojure -M:hvt) ...")
  (let [{:keys [status stdout stderr]} (run-spike)
        line (last-edn-line stdout)
        receipt (when line (try (edn/read-string line) (catch :default _ nil)))]
    (when (seq (str/trim stderr))
      (println "[hvt-smoke] stderr:") (println stderr))
    (if-not receipt
      (do (println "[hvt-smoke] FAIL: no EDN receipt on stdout (KVM unavailable?).")
          (println stdout)
          (js/process.exit 2))
      (let [{:keys [serial serial-ok? shutdown? steps exits]} receipt
            ok? (and (= serial expected-serial) serial-ok? shutdown? (zero? status))]
        (println (str "[hvt-smoke] receipt: serial=" (pr-str serial)
                      " serial-ok?=" serial-ok?
                      " shutdown?=" shutdown?
                      " steps=" steps
                      " mmio-exits=" (count (filter #(= :mmio (:reason %)) exits))))
        (if ok?
          (do (println "[hvt-smoke] PASS -- self-owned VMM booted a guest, serial via MMIO traps, MMIO poweroff clean halt.")
              (js/process.exit 0))
          (do (println "[hvt-smoke] FAIL -- receipt did not meet the V0 gate.")
              (js/process.exit 1)))))))

(-main)
