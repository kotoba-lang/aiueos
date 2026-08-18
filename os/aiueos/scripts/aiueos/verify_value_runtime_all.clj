(ns aiueos.verify-value-runtime-all
  "Run every value-runtime verifier and write down what happened.

  The eleven `verify_value_*.clj` next to this file each compile one Kotoba
  object and check it against its contract. Measured 2026-08-18: **nothing in
  this repository invoked any of them** — no task, no script, no test, no doc
  mentions them. They are not a check that passes; they are a check nobody
  runs, which reports the same green as one that ran (ADR-2608136000 question
  2, and ADR-0050 here).

  This is the invoker. It produces `qualification/value-runtime-baseline.edn`,
  a receipt naming the compiler it measured against, so the numbers cannot be
  quoted without their date and closure.

  Two objects have no verifier of their own — `value-runtime-sha256` and
  `value-runtime-digest-equal` — because they are *inputs* to the ones that do,
  and are compiled as part of them. They are covered, not missing.

  Run (the scripts dir has to be on the classpath, and it has to be `-m`:
  `-M <file>` loads this namespace and calls nothing, which is the same silent
  no-op this receipt exists to expose):

    clojure -Sdeps '{:paths [\"os/aiueos/scripts\"]
                     :deps {io.github.kotoba-lang/compiler
                            {:git/url \"https://github.com/kotoba-lang/amu.git\"
                             :git/sha \"<the sha in deps.edn>\"}}}' \\
      -M -m aiueos.verify-value-runtime-all"
  (:require [clojure.edn :as edn]
            [clojure.pprint]
            [clojure.java.io :as io]
            [clojure.string :as str]))

(def receipt-path "qualification/value-runtime-baseline.edn")

(defn- k [n] (str "os/aiueos/kotoba/" n ".kotoba"))
(defn- c [n] (str "os/aiueos/contracts/" n "-v1.edn"))

(def arena (k "value-handle-arena"))
(def sha256 (k "value-runtime-sha256"))
(def digest (k "value-runtime-digest-equal"))
(def cas (k "value-runtime-cas-verify"))
(def transport (k "value-runtime-provider-transport"))
(def dispatch (k "value-runtime-dispatch"))
(def table (k "value-runtime-capability-table"))

(def cases
  "Object -> the arguments its verifier's own usage string asks for. Getting
  these wrong reports a usage error as though it were a failing object, which
  is why they are written down rather than guessed per run."
  [["value-handle-arena" [arena (c "value-handle-arena")]]
   ["value-handle-plan" [(k "value-handle-plan") (c "value-handle-plan")]]
   ["value-runtime-capability-table" [table (c "value-runtime-capability-table")]]
   ["value-runtime-cas-verify" [sha256 digest cas (c "value-runtime-cas-verify")]]
   ["value-runtime-dispatch" [arena sha256 digest cas transport dispatch
                              (c "value-runtime-dispatch")]]
   ["value-runtime-domain" [(k "value-runtime-domain") (c "value-runtime-domain")]]
   ["value-runtime-entry" [arena sha256 digest cas transport dispatch
                           (k "value-runtime-entry") (c "value-runtime-entry")]]
   ["value-runtime-provider-policy" [table (k "value-runtime-provider-policy")
                                     (c "value-runtime-provider-policy")]]
   ["value-runtime-provider-transport" [arena sha256 digest cas transport
                                        (c "value-runtime-provider-transport")]]
   ["value-runtime-syscall-plan" [(k "value-runtime-syscall-plan")
                                  (c "value-runtime-syscall-plan")]]])

(defn- compiler-sha
  "The compiler this measurement is about. A receipt without it is a number
  with no closure, which is the shape of a claim that cannot be rechecked."
  []
  (get-in (edn/read-string (slurp "deps.edn"))
          [:aliases :test :extra-deps 'io.github.kotoba-lang/compiler :git/sha]))

(def causes
  "Failure classes, matched on the verifier's own message. Derived, not
  asserted: a row's cause comes from what the compiler said, so a new failure
  mode shows up as `:unclassified` rather than being folded into an existing
  bucket.

  Measured 2026-08-18, the ten failures are three classes, not one bug:

  - `:native-slice-typed-values` — the object uses a typed value the
    `x86_64-aiueos-kernel-v1` slice does not admit. The same wall
    `kotoba-kir`'s `only-native-word-typed-features?` describes.
  - `:native-slice-lowering` — admitted as a type, no lowering for the
    operation.
  - `:per-object-export-symbol` — the contract expects a symbol named after the
    object; the pinned compiler emits `kotoba_aiueos_probe` for every kernel
    object. Changing the contracts to match would give 57 linked objects one
    symbol, so this class is upstream work, not a local edit."
  [[#"no admitted type signature" :native-slice-typed-values]
   [#"no admitted lowering" :native-slice-lowering]
   [#"export mismatch|export is not exact" :per-object-export-symbol]])

(def upstream
  "Where the work is, for each form the slice refuses. A rejected form with no
  entry here is work nobody has asked for — the receipt would name a wall and
  point at no one, which is how a measurement becomes a complaint."
  {"kernel-compare-exchange-u32" "https://github.com/kotoba-lang/amu/issues/625"
   "kernel-value-runtime-capability-table" "https://github.com/kotoba-lang/amu/issues/625"
   "(kernel-publish-current-domain domain)" "https://github.com/kotoba-lang/amu/issues/625"})

(defn classify [message]
  (or (some (fn [[re cause]] (when (re-find re (str message)) cause)) causes)
      :unclassified))

(defn run-one [object args]
  (let [script (str "os/aiueos/scripts/aiueos/verify_"
                    (str/replace object "-" "_") ".clj")]
    (try
      (load-file script)
      (apply (resolve (symbol (str "aiueos.verify-" object) "-main")) args)
      {:object object :verdict :ok :args (vec args)}
      (catch Throwable t
        (let [m (or (ex-message t) (str t))
              d (ex-data t)]
          (cond-> {:object object :verdict :fail :args (vec args)
                   :cause (classify m) :message m}
            ;; The form the compiler named, when it named one. `:span` is
            ;; dropped: a byte offset into a file this receipt does not pin is
            ;; noise that would churn the diff on every unrelated edit.
            (:form d) (assoc :rejected-form (str (:form d)))
            (:kotoba.error/code d) (assoc :error-code (:kotoba.error/code d))
            (:phase d) (assoc :compiler-phase (:phase d))))))))

(defn -main [& _]
  (let [results (vec (for [[object args] cases]
                       (let [r (run-one object args)]
                         (println (format "%-34s %s" object (name (:verdict r))))
                         r)))
        failed (filterv #(= :fail (:verdict %)) results)
        receipt {:value-runtime/measured-at "2026-08-18"
                 :value-runtime/compiler-sha (compiler-sha)
                 :value-runtime/objects (count results)
                 :value-runtime/failing (count failed)
                 :value-runtime/failing-by-cause (frequencies (map :cause failed))
                 ;; What the six typed-value failures are actually asking the
                 ;; native slice for. One upstream ask or six is the difference
                 ;; between a bounded change and a programme.
                 :value-runtime/rejected-forms
                 (frequencies (keep :rejected-form failed))
                 :value-runtime/upstream upstream
                 :value-runtime/results results
                 :value-runtime/no-verifier-of-their-own
                 {"value-runtime-sha256" "an input to cas-verify, dispatch, entry and provider-transport; compiled as part of them"
                  "value-runtime-digest-equal" "same — an input, not an uncovered object"}
                 :value-runtime/verifier-without-source
                 {"verify_value_runtime_kernel_image.clj"
                  "no value-runtime-kernel-image.kotoba exists; recorded rather than deleted, because which of the two is missing is not a thing this run can know"}}]
    (io/make-parents receipt-path)
    (spit receipt-path (with-out-str (clojure.pprint/pprint receipt)))
    (println (format "%d objects, %d failing -> %s"
                     (count results) (count failed) receipt-path))))
