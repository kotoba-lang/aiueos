#!/usr/bin/env nbb
;; Reproducibility recipe for the ADR-0014 V1 ELF-loader test-guest fixtures
;; (issue #110). Assembles the checked-in aarch64 sources + shared linker script
;; into the fixture ELFs and verifies each SHA-256.
;;
;; Runs inside the aarch64 Linux/KVM VM (needs GNU binutils `as`/`ld` targeting
;; aarch64 ELF -- the host is macOS/Mach-O, so this cannot build there):
;;   nbb scripts/build-hvt-guest.cljs
;;
;; The .elf files are checked in so tests need no toolchain; this script only
;; regenerates them and confirms they are byte-identical (reproducible build).
;; --build-id=none + -s (strip) keep the output byte-deterministic and
;; independent of the source path.

(ns build-hvt-guest
  (:require ["child_process" :as cp]
            ["crypto" :as crypto]
            ["fs" :as fs]
            [clojure.string :as str]))

(def ld-script "resources/hvt/guest-aarch64.ld")

;; the checked-in guests: source .S -> output .elf, with the recorded sha256.
(def guests
  [{:src "resources/hvt/guest-aarch64.S"
    :elf "resources/hvt/guest-aarch64.elf"
    :sha "e2456afed9c0b03b5be0eac4896eac008815471b0f1a99eabc1e3ce7fa794e44"}
   {:src "resources/hvt/guest-virtio-aarch64.S"
    :elf "resources/hvt/guest-virtio-aarch64.elf"
    :sha "4eb9664d67d8820fb25b146a4403f544edc7f90fc0cbea81405fb4b322aaa78c"}])

(defn sh [& argv]
  (let [r (cp/spawnSync (first argv) (clj->js (rest argv)) #js {:encoding "utf8"})]
    (when (not= 0 (.-status r))
      (println (str "[build-hvt-guest] command failed: " (str/join " " argv)))
      (println (.-stderr r))
      (js/process.exit 2))
    r))

(defn sha256 [path]
  (-> (crypto/createHash "sha256") (.update (fs/readFileSync path)) (.digest "hex")))

(defn build-one [{:keys [src elf sha]} i]
  (let [obj (str "/tmp/hvt-guest-" i ".o")]
    (println "[build-hvt-guest] assembling" src "->" elf)
    (sh "as" "-o" obj src)
    (sh "ld" "--build-id=none" "-s" "-z" "max-page-size=0x1000" "-T" ld-script "-o" elf obj)
    (let [actual (sha256 elf)]
      (println "  sha256:" actual)
      (if (= actual sha)
        (do (println "  PASS -- byte-identical") true)
        (do (println "  NOTE -- differs from recorded" sha "(update after review)") false)))))

(defn -main []
  (let [oks (doall (map-indexed (fn [i g] (build-one g i)) guests))]
    (if (every? true? oks)
      (do (println "[build-hvt-guest] PASS -- all fixtures reproducible & byte-identical.")
          (js/process.exit 0))
      (do (println "[build-hvt-guest] FAIL -- one or more fixtures differ.")
          (js/process.exit 1)))))

(-main)
