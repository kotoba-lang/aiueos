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

;; The kernel object ABI exports exactly one entry per object, so the
;; public-point object is compiled from the SAME reviewed source with the sign
;; entry renamed out of the export allow-list. Recorded as data rather than
;; hidden in a script, so `reproduce-kotoba-objects.cljs` can replay it and a
;; reviewer can see that there is no second copy of the P-256 arithmetic.
(def ^:private source-transforms
  {"ecdsa-p256-public.o" [["(defn aiueos-ecdsa-p256-sign"
                           "(defn ecdsa-p256-sign-internal"]]})


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

(defn- read-edn [p]
  (try (edn/read-string (.readFileSync fs p "utf8")) (catch :default _ nil)))

;; ------------------------------------------------------ verification ------

;; What, if anything, runs vectors against an object's SOURCE. Computed from
;; what is on disk rather than declared, so it cannot claim coverage that was
;; deleted.
;;
;; Both kinds run the source through the KIR interpreter. NEITHER executes the
;; emitted machine code, so neither is evidence that the committed `.o` is
;; correct -- only a QEMU smoke is, and that is recorded per measurement in
;; `qualification/jvm-free-object-parity.edn`, not as a property of the file.
;; Saying `:kir-vectors` where the truth is "its source has vectors" would be
;; the same class of mistake this whole manifest exists to stop.
(defn- verification-index
  "namespace -> {:verification ... :verification-note ...}"
  []
  (let [contracts-dir (path/join root "os" "aiueos" "contracts")
        runner (path/join root "os" "aiueos" "scripts" "verify-admissions.cljs")
        runner-text (if (.existsSync fs runner) (.readFileSync fs runner "utf8") "")
        tests-dir (path/join root "test" "aiueos")
        from-contracts
        (into {}
              (for [f (if (.existsSync fs contracts-dir) (.readdirSync fs contracts-dir) [])
                    :when (str/ends-with? f ".edn")
                    :let [c (read-edn (path/join contracts-dir f))
                          nsym (get-in c [:graph :root])
                          fmt (:format c)]
                    :when (and nsym fmt
                               (str/includes? runner-text
                                              (str "defmethod prepare " fmt)))]
                [(str nsym)
                 {:verification :kir-vectors
                  :verification-note
                  (str "os/aiueos/contracts/" f
                       " vectors run through the KIR interpreter by"
                       " os/aiueos/scripts/verify-admissions.cljs;"
                       " the emitted machine code is not executed by it")}]))
        from-tests
        (into {}
              (for [f (if (.existsSync fs tests-dir) (.readdirSync fs tests-dir) [])
                    :when (str/ends-with? f ".clj")
                    :let [t (.readFileSync fs (path/join tests-dir f) "utf8")
                          m (re-find #"\"os\" \"aiueos\" \"kotoba\" \"([a-z0-9-]+)\.kotoba\"" t)]
                    :when (and m (str/includes? t "kotoba.kir"))]
                [(str "aiueos." (second m))
                 {:verification :kir-vectors
                  :verification-note
                  (str "test/aiueos/" f
                       " lowers the source with kotoba.sema/kotoba.kir and runs"
                       " vectors through the interpreter (clojure -M:test);"
                       " the emitted machine code is not executed by it")}]))]
    (merge from-tests from-contracts)))

(defn- source-namespace [src-path]
  (second (re-find #"\(ns\s+([a-zA-Z][a-zA-Z0-9.*+!_?<>=-]*)"
                   (.readFileSync fs src-path "utf8"))))

(defn- emit-provenance!
  "Rebuild the manifest's STRUCTURE from what is on disk: which objects exist,
  which source each comes from, how that source is compiled, and what the
  digests are today.

  It does not invent a producer. An object's `:compiler` is carried forward
  only when the attestation it records still applies -- same object bytes, same
  source bytes. The moment either moves, the recorded revision is a claim about
  bytes that no longer exist, so it drops back to `:sha nil` and the gate
  refuses the object as `compiler-unrecorded` until
  `reproduce-kotoba-objects.cljs --attest` measures it again."
  []
  (let [dir (path/join root "os" "aiueos" "kotoba")
        out (path/join dir "provenance.edn")
        prior (:objects (read-edn out))
        verified (verification-index)
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
                   _ (when-not (.existsSync fs src-path)
                       (die! 2 (str "UNANSWERED could-not-answer reason=source-absent object=" o)))
                   src-sha (sha256-file src-path)
                   obj-sha (sha256-file (path/join dir o))
                   was (get prior o)
                   carry? (and was
                               (= obj-sha (:object-sha256 was))
                               (= src-sha (:source-sha256 was))
                               (get-in was [:compiler :sha]))
                   modules (module-closure dir o src-path)
                   v (or (get verified (source-namespace src-path))
                         {:verification :attested-unverified
                          :verification-note
                          (str "no vector suite covers this source: no contract"
                               " with a verify-admissions prepare method and no"
                               " test/aiueos parity test names it. Its shape is"
                               " checked by scripts/verify-kotoba-kernel-object.py"
                               " and the K16 pure-native gate; its behaviour is"
                               " not.")})]
               [o (cond-> (merge {:source src-rel
                                  :source-sha256 src-sha
                                  :object-sha256 obj-sha
                                  :target target
                                  :route (if (seq modules) :project :single-file)
                                  :compiler (if carry?
                                              (:compiler was)
                                              {:repo "kotoba-lang/amu" :sha nil
                                               :recipe :unrecorded})}
                                 v)
                    (seq modules) (assoc :modules modules)
                    (contains? source-transforms o)
                    (assoc :source-transform (get source-transforms o)))])))
          unrecorded (->> entries (filter #(nil? (get-in (val %) [:compiler :sha]))) (mapv key))]
      (.writeFileSync
       fs out
       (str ";; GENERATED by os/aiueos/scripts/build-k16-pure-native.cljs --emit-provenance\n"
            ";; Do not hand-edit. The receipts here say what the repository RECORDS\n"
            ";; about each committed object; `:compiler {:sha nil :recipe :unrecorded}`\n"
            ";; is an honest gap, not a placeholder to fill in with a guess. The\n"
            ";; K16 pure-native gate refuses those objects with reason=compiler-unrecorded.\n"
            ";;\n"
            ";; A `:compiler {:sha ...}` here IS a byte-level claim: it was written by\n"
            ";; `reproduce-kotoba-objects.cljs --attest`, which compiled the source with\n"
            ";; that amu revision and hashed the result. This generator only carries such\n"
            ";; a claim forward while both digests it was made against still hold.\n"
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
