#!/usr/bin/env nbb
;; The K16 pure-native profile's entry point.
;;
;;   nbb os/aiueos/scripts/build-k16-pure-native.cljs
;;       -- run build-uefi.sh with AIUEOS_K16_PURE_NATIVE=1 and print the
;;          legacy restricted-link receipt (`kotoba=n stubs=n foreign=n`).
;;
;;   nbb os/aiueos/scripts/build-k16-pure-native.cljs --compiler /path/to/amu
;;       -- build the bootable C-free Amu PE32+ loader and embedded Kotoba
;;          kernel, with the compiler revision pinned by the build scripts.
;;
;;   nbb os/aiueos/scripts/build-k16-pure-native.cljs --emit-provenance
;;       -- regenerate os/aiueos/kotoba/provenance.edn from the committed
;;          objects, their sibling sources, and the recipes that name a
;;          compiler revision. Writes nothing else.
;;
;; The `--compiler` route boots under QEMU and contains the pure boot/memory
;; substrate. The no-argument legacy route remains a negative measurement of
;; the old C/ASM link. Neither route promotes NIC, HTTPS or Qwen to pure-native;
;; those layers remain separately closed by the profile contract.
;;
;; Exit 0 = admitted. 2 = could not answer. 3 = refused (which is today's
;; expected outcome, and the refusal names why).
(ns build-k16-pure-native
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            ["node:child_process" :as child]
            ["node:crypto" :as crypto]
            ["node:fs" :as fs]
            ["node:path" :as path]))

(def ^:private args (vec *command-line-args*))

(defn- flag? [f] (some #(= f %) args))

(defn- opt [f]
  (let [i (.indexOf args f)] (when-not (neg? i) (nth args (inc i) nil))))

(def ^:private root
  (path/resolve (or (opt "--root") (.cwd js/process))))

(def ^:private target "x86_64-aiueos-kernel-v1")

(def ^:private compiler
  (or (opt "--compiler")
      (.-AIUEOS_KOTOBA_COMPILER (.-env js/process))))

(defn- die! [code msg]
  (.error js/console msg)
  (.exit js/process code))

(defn- sha256-file [p]
  (-> (.createHash crypto "sha256") (.update (.readFileSync fs p)) (.digest "hex")))

;; ------------------------------------------------------- provenance ------

;; `ecdsa-p256-public.o` has no sibling source and is NOT sourceless:
;; `ecdsa-p256-sign.kotoba` defines both `aiueos-ecdsa-p256-sign` and
;; `aiueos-ecdsa-p256-public`, and `reproduce-ecdsa-sign-object.clj` writes
;; both objects from that one file (ADR-0129).
(def ^:private source-overrides
  {"ecdsa-p256-public.o" "ecdsa-p256-sign.kotoba"})

(def ^:private ecdsa-recipe "os/aiueos/scripts/reproduce-ecdsa-sign-object.clj")
(def ^:private kernel-recipe "os/aiueos/scripts/reproduce-kotoba-kernel-object.sh")

(defn- kernel-recipe-pin
  "The compiler revision `reproduce-kotoba-kernel-object.sh` pins, and the set
  of objects it actually names. Read out of the script rather than restated
  here: a second copy of a pin is a pin that can disagree with itself."
  []
  (let [p (path/join root kernel-recipe)
        text (.readFileSync fs p "utf8")
        sha (second (re-find #"(?m)^expected=([0-9a-f]{40})" text))
        named (set (map second (re-seq #"kotoba/([a-z0-9-]+)\.kotoba" text)))]
    (when-not sha
      (die! 2 (str "UNANSWERED could-not-answer reason=recipe-pin-unreadable path=" kernel-recipe)))
    {:sha sha :named named}))

(defn- emit-provenance! []
  (let [dir (path/join root "os" "aiueos" "kotoba")
        {:keys [sha named]} (kernel-recipe-pin)
        objects (->> (.readdirSync fs dir)
                     (filter #(str/ends-with? % ".o"))
                     sort
                     vec)]
    (when (empty? objects)
      (die! 2 "UNANSWERED could-not-answer reason=no-objects"))
    (let [entries
          (into
           (sorted-map)
           (for [o objects]
             (let [stem (subs o 0 (- (count o) 2))
                   src-name (get source-overrides o (str stem ".kotoba"))
                   src-path (path/join dir src-name)
                   src-rel (str "os/aiueos/kotoba/" src-name)
                   recipe (cond
                            (contains? #{"ecdsa-p256-sign.o" "ecdsa-p256-public.o"} o) ecdsa-recipe
                            (contains? named stem) kernel-recipe
                            :else nil)]
               (when-not (.existsSync fs src-path)
                 (die! 2 (str "UNANSWERED could-not-answer reason=source-absent object=" o)))
               [o (cond-> {:source src-rel
                           :source-sha256 (sha256-file src-path)
                           :object-sha256 (sha256-file (path/join dir o))
                           :target target
                           ;; A recipe that patches the pinned toolchain
                           ;; in-process is still a recorded producer, but it
                           ;; is not the same claim as an unpatched one.
                           :compiler (if recipe
                                       (cond-> {:repo "kotoba-lang/amu" :sha sha :recipe recipe}
                                         (= recipe ecdsa-recipe) (assoc :patched true))
                                       {:repo "kotoba-lang/amu" :sha nil :recipe :unrecorded})}
                    true identity)])))
          unrecorded (->> entries (filter #(nil? (get-in (val %) [:compiler :sha]))) (mapv key))
          out (path/join dir "provenance.edn")]
      (.writeFileSync
       fs out
       (str ";; GENERATED by os/aiueos/scripts/build-k16-pure-native.cljs --emit-provenance\n"
            ";; Do not hand-edit. The receipts here say what the repository RECORDS\n"
            ";; about each committed object; `:compiler {:sha nil :recipe :unrecorded}`\n"
            ";; is an honest gap, not a placeholder to fill in with a guess. The\n"
            ";; K16 pure-native gate refuses those objects with reason=compiler-unrecorded.\n"
            ";;\n"
            ";; This file does NOT claim the objects reproduce byte-for-byte from these\n"
            ";; sources at these revisions -- that is measured separately\n"
            ";; (ADR-0129, qualification/object-producer-measurement.edn).\n"
            (pr-str {:format :aiueos.kotoba-object-provenance/v1
                     :target target
                     :objects entries
                     :recorded (- (count entries) (count unrecorded))
                     :unrecorded unrecorded})
            "\n")
       "utf8")
      (println (str "AIUEOS_PROVENANCE_WRITTEN objects=" (count entries)
                    " recorded=" (- (count entries) (count unrecorded))
                    " unrecorded=" (count unrecorded)))
      (.exit js/process 0))))

;; ------------------------------------------------------------ profile ----

(defn- run-bootable-native! [compiler]
  (let [compiler (path/resolve compiler)
        script (path/join root "os" "aiueos" "scripts"
                          "build-kotoba-native-boot.sh")
        out-root (or (.-AIUEOS_OUT (.-env js/process))
                     (path/join root "build" "aiueos"))
        native-out (path/join out-root "k16-pure-native" "native")
        boot-out (path/join out-root "k16-pure-native" "boot")
        env (js/Object.assign #js {} (.-env js/process)
                              #js {"AIUEOS_NATIVE_OUT" native-out
                                   "AIUEOS_NATIVE_BOOT_OUT" boot-out})]
    (when-not (.existsSync fs script)
      (die! 2 (str "UNANSWERED could-not-answer reason=build-script-absent path=" script)))
    (when-not (.existsSync fs compiler)
      (die! 2 (str "UNANSWERED could-not-answer reason=compiler-absent path=" compiler)))
    (let [r (.spawnSync child "sh" #js [script compiler]
                        #js {:cwd root :env env :stdio "inherit"})
          status (or (.-status r) 2)
          kernel (path/join native-out "KERNEL.ELF")
          efi (path/join boot-out "esp" "EFI" "BOOT" "BOOTX64.EFI")
          kernel-receipt (path/join native-out "receipt.json")
          boot-receipt (path/join boot-out "receipt.json")
          receipt (path/join out-root "aiueos-k16-pure-native-boot-receipt.edn")]
      (when-not (zero? status)
        (.exit js/process status))
      (doseq [p [kernel efi kernel-receipt boot-receipt]]
        (when-not (.existsSync fs p)
          (die! 2 (str "UNANSWERED could-not-answer reason=artifact-absent path=" p))))
      (.mkdirSync fs out-root #js {:recursive true})
      (.writeFileSync
       fs receipt
       (str (pr-str {:format :aiueos.k16-pure-native-boot-receipt/v1
                     :target target
                     :compiler (-> (js/JSON.parse
                                    (.readFileSync fs kernel-receipt "utf8"))
                                   (aget "compiler_commit"))
                     :kernel {:path kernel :sha256 (sha256-file kernel)}
                     :loader {:path efi :sha256 (sha256-file efi)}
                     :foreign 0
                     :scope :boot-memory-substrate
                     :all-native-ready? false}) "\n")
       "utf8")
      (println (str "K16_PURE_NATIVE_BOOT_OK foreign=0 kernel=" kernel
                    " loader=" efi " receipt=" receipt))
      (.exit js/process 0))))

(defn- run-profile! []
  (let [script (path/join root "os" "aiueos" "scripts" "build-uefi.sh")]
    (when-not (.existsSync fs script)
      (die! 2 (str "UNANSWERED could-not-answer reason=build-script-absent path=" script)))
    (let [env (js/Object.assign #js {} (.-env js/process)
                                #js {"AIUEOS_K16_PURE_NATIVE" "1"})
          r (.spawnSync child "sh" #js [script]
                        #js {:cwd root :env env :stdio "inherit"})
          status (or (.-status r) 2)
          out (or (.-AIUEOS_OUT (.-env js/process)) (path/join root "build" "aiueos"))
          receipt (path/join out "aiueos-k16-pure-native-receipt.edn")]
      (if-not (.existsSync fs receipt)
        (die! 2 (str "UNANSWERED could-not-answer reason=receipt-absent path=" receipt))
        (let [m (edn/read-string (.readFileSync fs receipt "utf8"))]
          (println (str "K16_PURE_NATIVE scanned=" (:scanned m)
                        " kotoba=" (:kotoba m) " stubs=" (:stubs m)
                        " foreign=" (:foreign m)))
          (doseq [[reason n] (sort-by key (:reasons m))]
            (println (str "  reason=" reason " n=" n)))
          (println (str "  receipt=" receipt))
          (.exit js/process status))))))

(cond
  (flag? "--emit-provenance") (emit-provenance!)
  compiler (run-bootable-native! compiler)
  :else (run-profile!))
