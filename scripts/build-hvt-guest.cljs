#!/usr/bin/env nbb
;; Reproducibility recipe for the ADR-0014 V1 ELF-loader test fixture
;; `resources/hvt/guest-aarch64.elf` (issue #110). Assembles the checked-in
;; aarch64 source + linker script into the fixture ELF and verifies the SHA-256.
;;
;; Runs inside the aarch64 Linux/KVM VM (needs GNU binutils `as`/`ld` targeting
;; aarch64 ELF -- the host is macOS/Mach-O, so this cannot build there):
;;   nbb scripts/build-hvt-guest.cljs
;;
;; The .elf is checked in so tests need no toolchain; this script only
;; regenerates it and confirms it is byte-identical (reproducible build).

(ns build-hvt-guest
  (:require ["child_process" :as cp]
            ["crypto" :as crypto]
            ["fs" :as fs]
            [clojure.string :as str]))

(def src "resources/hvt/guest-aarch64.S")
(def ld "resources/hvt/guest-aarch64.ld")
(def elf "resources/hvt/guest-aarch64.elf")
(def expected-sha
  "e2456afed9c0b03b5be0eac4896eac008815471b0f1a99eabc1e3ce7fa794e44")

(defn sh [& argv]
  (let [r (cp/spawnSync (first argv) (clj->js (rest argv))
                        #js {:encoding "utf8"})]
    (when (not= 0 (.-status r))
      (println (str "[build-hvt-guest] command failed: " (str/join " " argv)))
      (println (.-stderr r))
      (js/process.exit 2))
    r))

(defn sha256 [path]
  (-> (crypto/createHash "sha256")
      (.update (fs/readFileSync path))
      (.digest "hex")))

(defn -main []
  (println "[build-hvt-guest] assembling" src "->" elf)
  (sh "as" "-o" "/tmp/guest-hvt.o" src)
  ;; --build-id=none + -s (strip) keep the output byte-deterministic and
  ;; independent of the source path: the default build-id note varies run to
  ;; run, and the assembler embeds the source filename as an STT_FILE symbol --
  ;; stripping both removes every non-essential, non-reproducible byte, leaving
  ;; just the ELF header, program header, and the 40-byte .text the tender loads.
  (sh "ld" "--build-id=none" "-s" "-z" "max-page-size=0x1000" "-T" ld "-o" elf "/tmp/guest-hvt.o")
  (let [actual (sha256 elf)]
    (println "[build-hvt-guest] sha256:" actual)
    (if (= actual expected-sha)
      (do (println "[build-hvt-guest] PASS -- fixture is reproducible & byte-identical.")
          (js/process.exit 0))
      (do (println "[build-hvt-guest] NOTE -- sha differs from the recorded one.")
          (println "  expected:" expected-sha)
          (println "  If binutils changed, update expected-sha after review.")
          (js/process.exit 1)))))

(-main)
