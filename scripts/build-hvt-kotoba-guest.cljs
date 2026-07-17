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
(def target "aarch64-aiueos-kernel-v1")

;; the checked-in kotoba-first guests: .kotoba source -> .elf, with recorded sha.
(def guests
  [{:src "resources/hvt/guest-serial.kotoba"
    :elf "resources/hvt/guest-serial.elf"
    :sha "fb9d25b1614b5ba27edd02308085073bb196e76515ba7cb6734c140c46951512"}
   {:src "resources/hvt/guest-virtio-probe.kotoba"
    :elf "resources/hvt/guest-virtio-probe.elf"
    :sha "81e26fdae0d607de4a02546451c08da83bf5c7f3173f37830117c6085d43b549"}])

(defn sha256 [path]
  (-> (crypto/createHash "sha256") (.update (fs/readFileSync path)) (.digest "hex")))

(defn build-one [{:keys [src elf sha]}]
  (println "[build-hvt-kotoba-guest] compiling" src "->" elf (str "(--target " target ")"))
  (let [r (cp/spawnSync compiler-bin
                        #js ["compile" src "--target" target "--artifact" "image" "--output" elf]
                        #js {:encoding "utf8"})]
    (when (not= 0 (.-status r))
      (println "  compile failed:") (println (.-stdout r)) (println (.-stderr r))
      (js/process.exit 2))
    (let [actual (sha256 elf)]
      (println "  sha256:" actual)
      (if (= actual sha)
        (do (println "  PASS -- byte-identical.") true)
        (do (println "  NOTE -- differs from recorded" sha "(compiler changed; update after review).")
            false)))))

(defn -main []
  (let [oks (doall (map build-one guests))]
    (if (every? true? oks)
      (do (println "[build-hvt-kotoba-guest] PASS -- all kotoba guests reproducible.")
          (js/process.exit 0))
      (do (println "[build-hvt-kotoba-guest] FAIL -- one or more differ.")
          (js/process.exit 1)))))

(-main)
