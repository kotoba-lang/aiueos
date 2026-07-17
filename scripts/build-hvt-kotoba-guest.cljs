#!/usr/bin/env nbb
;; Reproducibility recipe for the ADR-0014 V1 **kotoba-first** guest fixture
;; `resources/hvt/guest-serial.elf` (issue #110): a `.kotoba` program compiled by
;; `kotoba-lang/compiler` to a bare-metal AArch64 ELF via its
;; `aarch64-aiueos-kernel-v1` target -- the guest is written in the Kotoba
;; language, not asm/C.
;;
;; Runs where the compiler west sibling is checked out (../compiler) with a JDK.
;; The .elf is checked in so tests need no toolchain; this only regenerates it
;; and confirms it is byte-identical.
;;
;;   nbb scripts/build-hvt-kotoba-guest.cljs

(ns build-hvt-kotoba-guest
  (:require ["child_process" :as cp]
            ["crypto" :as crypto]
            ["fs" :as fs]
            [clojure.string :as str]))

(def compiler-bin "../compiler/bin/kotoba-compiler")
(def src "resources/hvt/guest-serial.kotoba")
(def elf "resources/hvt/guest-serial.elf")
(def target "aarch64-aiueos-kernel-v1")
(def expected-sha
  "fb9d25b1614b5ba27edd02308085073bb196e76515ba7cb6734c140c46951512")

(defn sha256 [path]
  (-> (crypto/createHash "sha256") (.update (fs/readFileSync path)) (.digest "hex")))

(defn -main []
  (println "[build-hvt-kotoba-guest] compiling" src "->" elf (str "(--target " target ")"))
  (let [r (cp/spawnSync compiler-bin
                        #js ["compile" src "--target" target "--artifact" "image" "--output" elf]
                        #js {:encoding "utf8"})]
    (when (not= 0 (.-status r))
      (println "[build-hvt-kotoba-guest] compile failed:")
      (println (.-stdout r)) (println (.-stderr r))
      (js/process.exit 2))
    (let [actual (sha256 elf)]
      (println "  sha256:" actual)
      (if (= actual expected-sha)
        (do (println "[build-hvt-kotoba-guest] PASS -- byte-identical.")
            (js/process.exit 0))
        (do (println "[build-hvt-kotoba-guest] NOTE -- differs from recorded"
                     expected-sha "(the compiler may have changed; update after review).")
            (js/process.exit 1))))))

(-main)
