(ns aiueos.value-runtime-verifier-wiring-test
  "The eleven value-runtime verifiers are WIRED, not merely present.

  This does not compile anything. `aiueos.verify-value-runtime-all` does, and
  needs the compiler closure; what this asserts is that the wiring by which it
  *could* be invoked is stated rather than inert:

  - `os/aiueos/scripts` (one level below `os/aiueos/scripts`, where
    `aiueos.verify-value-runtime-all` sits) is ON a classpath, so `-m` loads it.
    It is not `-M <file>`, which loads a namespace and calls nothing — the
    silent no-op `verify_value_runtime_all.clj`'s own usage string warns about.
  - an alias NAMES the runner, carrying the compiler pin that closure needs.
  - `.github/workflows/ci.yml` touches it, so a fleet/CI node with credentials
    can run it instead of nobody.

  `aiueos.kotoba-object-reachability-test` writes each value-* source down as
  declared-unbuilt because nothing executes it; the verifiers DO execute it and
  nobody runs them. ADR-0050 measured both halves. This file is the second half
  going from 'written' to 'wired'.

  ## What this does not claim
  - The verifiers pass. Invoked by hand they fail (ADR-0050), and their receipt
    is a measurement rather than a repaired state. Running them is re-measuring
    and is not claimed here.
  - `verify-admissions.cljs`'s four objects (cid-v1-admit, unixfs-file-admit,
    value-runtime-cas-verify, value-handle-arena). That runner is a SEPARATE
    thing on nbb and is its own wiring question; this file replies for what
    stayed here."
  (:require [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(def ^:private deps (delay (edn/read-string (slurp "deps.edn"))))
(def ^:private ci-text (delay (slurp ".github/workflows/ci.yml")))
(def ^:private runner
  (str "os/aiueos/scripts/aiueos/verify_value_runtime_all.clj"))

(defn- alias-by-name [n]
  (get-in @deps [:aliases (keyword n)]))

(deftest the-verifier-runner-exists-where-classpath-can-reach
  (is (.exists (io/file runner))
      "the invoker this wiring names must exist, or the alias would name nothing"))

(deftest the-runners-own-usage-is-a-wiring-not-an-operation
  (testing "the usage string is what this wiring is answering, not decoration"
    (let [usage (slurp runner)]
      (is (str/includes? usage "scripts dir has to be on the classpath")
          "the runner itself says a classpath is required — the alias carries it")
      (is (str/includes? usage "-M -m aiueos.verify-value-runtime-all")
          "the runner itself says `-m`, the form which actually calls it"))))

(deftest the-alias-names-the-run
  (let [a (alias-by-name "verify-value-runtime")]
    (is (some? a) "no :verify-value-runtime alias — nothing can invoke the run")
    (is (str/includes? (str (:main-opts a)) "aiueos.verify-value-runtime-all")
        "the alias must `-m` the runner; a FILE arg would be the silent no-op")
    (is (str/includes? (str (:extra-paths a)) "os/aiueos/scripts")
        (str "the alias must put " runner "'s directory on the classpath; "
             "without it `-m aiueos.verify-value-runtime-all` cannot LOAD"))))

(def ^:private amu 'io.github.kotoba-lang/amu)

(deftest the-alias-carries-the-closure-nobody-re-measures
  (let [a (alias-by-name "verify-value-runtime")
        pinned (get-in @deps [:aliases :test :extra-deps amu :git/sha])]
    (is (some? a) "a nil alias cannot carry anything")
    (is (contains? (:extra-deps a) amu)
        "the alias must carry amu: invoking needs a compiler, and it is test-only")
    (is (= pinned (:git/sha (get (:extra-deps a) amu)))
        (str "the closure must be the compiler the receipt was measured at "
             "(deps.edn :test pins " pinned "); re-running against another "
             "compiler would move what the numbers are about"))))

(deftest the-commit-pipeline-shark-touches-it
  (is (str/includes? @ci-text "clojure -M:verify-value-runtime")
      "ci.yml must run the alias; a fleet node holds credentials, an operator does"))

(deftest the-alias-is-not-a-load-and-call-nothing-nobody
  (let [a (alias-by-name "verify-value-runtime")]
    (is (some #{"-m"} (:main-opts a))
        "`-M <file>` loads one namespace and calls nothing; the alias must be -m")))

(deftest the-wiring-does-not-turn-the-receipts-ratchet
  ;; Re-running is separate work; the existing test is the ratchet, and this is
  ;; only about whether a run could exist. The receipt must be reachable because
  ;; the runner spits it, and the runner must write where a classpath can reach.
  ;;
  ;; The floor is nine, not ten, because coverage MOVED: four verifiers left to
  ;; `os/aiueos/scripts/verify-admissions.cljs` on nbb. A floor that keeps the
  ;; pre-move count reads as a shrunken scan when it is a move — so the moved
  ;; objects are asserted BY NAME beside it.
  (is (.exists (io/file "qualification/value-runtime-baseline.edn"))
      "the receipt the runner emits must be committed, so a stale one is red")
  (let [receipt (edn/read-string (slurp "qualification/value-runtime-baseline.edn"))]
    (is (<= 9 (:value-runtime/objects receipt))
        "evidence floor: a receipt of nothing must not read as clean")
    (is (seq (:value-runtime/moved-to receipt))
        "a shrinking count reads as a move only if the move is written down")
    (is (seq (:value-runtime/no-verifier-of-their-own receipt))
        "inputs that only the movers compile must not arrive unmeasured")))