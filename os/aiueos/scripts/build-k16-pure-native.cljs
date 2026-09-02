#!/usr/bin/env nbb
;; The K16 pure-native profile's entry point.
;;
;;   nbb os/aiueos/scripts/build-k16-pure-native.cljs
;;       -- run build-uefi.sh with AIUEOS_K16_PURE_NATIVE=1 and print the
;;          receipt summary (`kotoba=n stubs=n foreign=n`).
;;
;;   nbb os/aiueos/scripts/build-k16-pure-native.cljs --emit-provenance
;;       -- regenerate os/aiueos/kotoba/provenance.edn from the committed
;;          objects, their sibling sources, and the recipes that name a
;;          compiler revision. Writes nothing else.
;;
;; THE PROFILE DOES NOT BOOT. It cannot: BOOTX64.EFI is `uefi/main.c`, there is
;; no Kotoba/Amu loader, and the profile refuses to emit a C one. Its purpose is
;; to make the Kotoba/foreign boundary machine-checked, so the streams that are
;; porting the loader, the NIC, TLS and the Qwen path have a number to move.
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


;; A source that declares `(:require ...)` is a MODULE OF A PROJECT, and the
;; object built from it depends on bytes this manifest would otherwise not
;; name. Recording only the entry file would leave `:source-sha256` unchanged
;; while the object changed -- a receipt that reports "unmodified" for an input
;; it never looked at.
;;
;; Resolution is amu's own rule (`.` -> `/`, `-` -> `_`, relative to
;; `os/aiueos/kotoba`), with the historical flat spelling as a fallback,
;; because every module written before amu#742 sits flat under a hyphenated
;; name. A module that resolves to neither is a hard stop: a partial closure
;; recorded as if it were whole is the failure this whole file exists to avoid.
(defn- required-namespaces [text]
  (->> (re-seq #"\[([a-zA-Z][a-zA-Z0-9.*+!_?<>=-]*)\s+:as\s" text)
       (map second)
       distinct
       sort
       vec))

(defn- module-file [dir namespace]
  (let [munged (-> namespace (str/replace "." "/") (str/replace "-" "_"))
        flat (str/replace namespace #"^aiueos\." "")]
    (first (filter #(.existsSync fs %)
                   [(path/join dir (str munged ".kotoba"))
                    (path/join dir (str flat ".kotoba"))]))))

(defn- module-closure
  "Every module the entry pulls in, transitively, as
  namespace -> {:source :source-sha256}. Empty for a single-file object."
  [dir object entry-path]
  (loop [pending (required-namespaces (.readFileSync fs entry-path "utf8"))
         seen (sorted-map)]
    (if-let [ns-name (first pending)]
      (if (contains? seen ns-name)
        (recur (rest pending) seen)
        (let [file (module-file dir ns-name)]
          (when-not file
            (die! 2 (str "UNANSWERED could-not-answer reason=module-source-absent object="
                         object " module=" ns-name)))
          (let [text (.readFileSync fs file "utf8")]
            (recur (into (vec (rest pending)) (required-namespaces text))
                   (assoc seen ns-name
                          {:source (str "os/aiueos/kotoba/"
                                        (path/relative dir file))
                           :source-sha256 (sha256-file file)})))))
      seen)))

(defn- kernel-recipe-pin
  "The compiler revision `reproduce-kotoba-kernel-object.sh` pins, and the set
  of objects it actually names. Read out of the script rather than restated
  here: a second copy of a pin is a pin that can disagree with itself."
  []
  (let [p (path/join root kernel-recipe)
        text (.readFileSync fs p "utf8")
        sha (second (re-find #"(?m)^expected=([0-9a-f]{40})" text))
        ;; `kotoba/<stem>.kotoba` and, since the ns->path move,
        ;; `kotoba/aiueos/<munged stem>.kotoba`. Reading only the flat spelling
        ;; would silently demote every moved object to `:unrecorded` -- the
        ;; failure this file exists to avoid, arriving as a quieter receipt
        ;; rather than as an error.
        named (set (map (fn [[_ stem]] (str/replace stem "_" "-"))
                        (re-seq #"kotoba/(?:aiueos/)?([a-z0-9_-]+)\.kotoba" text)))]
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
                   ;; Flat `<stem>.kotoba` first, then the ns->path spelling
                   ;; `aiueos/<munged stem>.kotoba` an object's source moves to
                   ;; when it becomes an importable module. Same rule as
                   ;; `module-file` above, and it has to be the same rule: a
                   ;; source this cannot find is reported as `source-absent`
                   ;; and stops the run, which is right for a source that is
                   ;; gone and wrong for one that merely moved.
                   src-name (or (get source-overrides o)
                                (first (filter #(.existsSync fs (path/join dir %))
                                               [(str stem ".kotoba")
                                                (str "aiueos/" (str/replace stem "-" "_")
                                                     ".kotoba")]))
                                (str stem ".kotoba"))
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
                    (seq (module-closure dir o src-path))
                    (assoc :modules (module-closure dir o src-path)))])))
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
  :else (run-profile!))
