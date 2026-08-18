(ns aiueos.tcb-test
  (:require [aiueos.tcb :as tcb]
            [clojure.edn :as edn]
            [clojure.java.io :as io]
            [clojure.test :refer [deftest is]]))

(def required-tcb-paths
  #{"src/aiueos/contract.cljc"
    "src/aiueos/manifest.cljc"
    "src/aiueos/signing.cljc"
    "src/aiueos/key_lifecycle.clj"
    "src/aiueos/policy.cljc"
    "src/aiueos/broker.cljc"
    "src/aiueos/execute.cljc"
    "src/aiueos/entropy.clj"
    "src/aiueos/watchdog.clj"
    "src/aiueos/launcher.cljc"
    "src/aiueos/network_topic.clj"
    "src/aiueos/deployment_profile.cljc"
    "src/aiueos/pid1.cljc"
    "src/aiueos/sealed_audit.clj"
    "src/aiueos/sealed_state.clj"
    "src/aiueos/hvt.cljc"
    "src/aiueos/vfio.cljc"
    "src/aiueos/sbom.clj"
    ;; The checker is in the inventory it checks. Without this, weakening a
    ;; check leaves no digest trace anywhere -- the one file whose content
    ;; decides whether drift is reported was the one file free to drift.
    "src/aiueos/tcb.clj"})

(deftest checked-in-tcb-inventory-has-no-drift
  ;; 28 -> 34: fleet onboarding and update admission joined the TCB
  ;; (enroll, update, publisher, ota, clock, provider/device). The count is
  ;; asserted rather than derived on purpose -- deriving it would make the
  ;; inventory grow silently, and a TCB that can grow unnoticed is the record
  ;; ADR-0016 called "not evidence".
  ;; `:classpath :not-in-scope` because this JVM was started with the `:test`
  ;; alias, whose extra jars are not what the inventory is about (ADR-0062).
  ;; The counts are asserted rather than derived on purpose; 34 -> 39 and
  ;; 5 -> 6 are the cloud/anchors/boot-admission additions of ADR-0042 through
  ;; ADR-0049, which nobody noticed at the time because this assertion was
  ;; already red for the classpath reason and one red hides another.
  (is (= {:valid? true :classpath-scope :not-in-scope
          :files 39 :external 6 :classpath 9 :properties 6 :errors []}
         (tcb/validate (tcb/read-inventory)
                       (clojure.edn/read-string (slurp "deps.edn"))
                       (clojure.edn/read-string (slurp "security-adoption.edn"))
                       {:classpath :not-in-scope :review :not-in-scope}))))

(deftest authority-and-escape-boundaries-cannot-disappear-silently
  (let [inventory (tcb/read-inventory)
        paths (set (map :path (:tcb/files inventory)))]
    (is (every? paths required-tcb-paths))
    (is (every? (comp keyword? :role) (:tcb/files inventory)))))

(deftest every-external-dependency-is-content-addressed-or-declares-its-gap
  (is (every? (fn [{:keys [sha256 git-sha assurance-gap]}]
                (or (string? sha256) (string? git-sha) (keyword? assurance-gap)))
              (:tcb/external (tcb/read-inventory)))))

(deftest digest-drift-is-fail-closed
  (let [inventory (tcb/read-inventory)
        changed (assoc-in inventory [:tcb/files 0 :sha256]
                          (apply str (repeat 64 "0")))
        result (tcb/validate changed)]
    (is (false? (:valid? result)))
    (is (= :digest-drift (-> result :errors first :kind)))))

;; --- external (dependency) half -------------------------------------------
;;
;; The synthetic inventories below carry no `:tcb/files`, so only the external
;; checks can produce errors. They keep the real `:tcb/classpath`, because the
;; classpath the test JVM is running on is real either way.

(def git-dep-inventory
  {:tcb/version 3
   :tcb/files []
   :tcb/classpath (:tcb/classpath (tcb/read-inventory))
   :tcb/external [{:coordinate "io.github.example/lib" :source :git
                   :git-sha "aaaa000000000000000000000000000000000000"
                   :role :example}]})

(def git-deps-edn
  {:deps {'io.github.example/lib
          {:git/url "https://example.invalid/lib.git"
           :git/sha "aaaa000000000000000000000000000000000000"}}})

(deftest synthetic-git-dependency-baseline-is-clean
  (is (true? (:valid? (tcb/validate git-dep-inventory git-deps-edn nil
                                    {:classpath :not-in-scope :review :not-in-scope})))))

(deftest runtime-dependency-missing-from-the-inventory-is-fail-closed
  (let [deps (assoc-in git-deps-edn [:deps 'io.github.example/unlisted]
                       {:mvn/version "1.0.0"})
        result (tcb/validate git-dep-inventory deps nil {:classpath :not-in-scope :review :not-in-scope})]
    (is (false? (:valid? result)))
    (is (= [{:kind :external-undeclared
             :coordinate "io.github.example/unlisted"}]
           (:errors result)))))

(deftest git-pin-drift-between-inventory-and-deps-is-fail-closed
  (let [deps (assoc-in git-deps-edn [:deps 'io.github.example/lib :git/sha]
                       "bbbb000000000000000000000000000000000000")
        result (tcb/validate git-dep-inventory deps nil {:classpath :not-in-scope :review :not-in-scope})]
    (is (false? (:valid? result)))
    (is (= :external-git-sha-drift (-> result :errors first :kind)))))

(deftest an-unpinned-external-dependency-cannot-pass-silently
  (let [inventory (update-in git-dep-inventory [:tcb/external 0] dissoc :git-sha)
        result (tcb/validate inventory git-deps-edn nil {:classpath :not-in-scope :review :not-in-scope})]
    (is (false? (:valid? result)))
    (is (= :external-unpinned (-> result :errors first :kind)))))

(deftest a-platform-entry-without-a-declared-gap-cannot-pass-silently
  (let [inventory (assoc git-dep-inventory
                         :tcb/external [{:coordinate "java.base" :source :platform
                                         :role :crypto-ffi-process-runtime}])
        result (tcb/validate inventory {:deps {}} nil {:classpath :not-in-scope :review :not-in-scope})]
    (is (false? (:valid? result)))
    (is (= :external-unpinned (-> result :errors first :kind)))))

;; --- platform floor --------------------------------------------------------
;;
;; The floor and the CI runner disagree today. These read the versions the
;; workflow actually provisions rather than hardcoding 21, so raising the
;; runner does not turn them into assertions about a number that moved.

(def provisioned-versions (tcb/provisioned-java-versions))

(defn- platform-inventory [entry]
  (assoc git-dep-inventory :tcb/external [entry]))

(defn- platform-errors [entry]
  (:errors (tcb/validate (platform-inventory entry) {:deps {}} nil
                         {:classpath :not-in-scope :review :not-in-scope})))

(deftest the-ci-workflow-java-version-is-readable
  (is (seq provisioned-versions)
      "the floor check is inert if no setup-java version can be read")
  (is (every? int? provisioned-versions)))

(deftest a-floor-the-runner-does-not-meet-must-be-recorded
  (let [floor (inc (apply max provisioned-versions))
        errors (platform-errors {:coordinate "java.base" :source :platform
                                 :role :crypto-ffi-process-runtime
                                 :minimum-version floor
                                 :assurance-gap :platform-runtime-not-content-addressed})]
    (is (= :platform-floor-contradiction-unrecorded (:kind (first errors))))
    (is (= (vec provisioned-versions) (:provisioned (first errors))))))

(deftest a-recorded-contradiction-that-no-longer-exists-is-fail-closed
  (let [floor (apply min provisioned-versions)
        errors (platform-errors {:coordinate "java.base" :source :platform
                                 :role :crypto-ffi-process-runtime
                                 :minimum-version floor
                                 :assurance-gap :platform-runtime-not-content-addressed
                                 :floor-unmet-by-ci {:provisioned [1]}})]
    (is (= :platform-floor-contradiction-stale (:kind (first errors)))
        "resolving the disagreement either way must delete the record, or the
         record becomes a comment about a contradiction someone already fixed")))

(deftest a-recorded-contradiction-must-name-the-right-versions
  (let [floor (inc (apply max provisioned-versions))
        errors (platform-errors {:coordinate "java.base" :source :platform
                                 :role :crypto-ffi-process-runtime
                                 :minimum-version floor
                                 :assurance-gap :platform-runtime-not-content-addressed
                                 :floor-unmet-by-ci {:provisioned [1]}})]
    (is (= :platform-floor-contradiction-drift (:kind (first errors))))))

(deftest setup-java-versions-are-read-from-the-workflow
  (let [file (java.io.File/createTempFile "ci-workflow" ".yml")]
    (try
      (spit file "steps:\n  - uses: actions/setup-java@v4\n    with:\n      java-version: \"21\"\n  - uses: actions/setup-java@v4\n    with:\n      java-version: 25\n")
      (is (= [21 25] (tcb/provisioned-java-versions (.getPath file))))
      (finally (.delete file))))
  (is (nil? (tcb/provisioned-java-versions "does/not/exist.yml"))
      "an unreadable workflow reports nothing rather than an empty measurement"))

(deftest the-security-adoption-record-must-agree-with-the-inventory
  (let [inventory (tcb/read-inventory)
        deps (edn/read-string (slurp "deps.edn"))
        adoption {:security/git-sha (apply str (repeat 40 "f"))}
        result (tcb/validate inventory deps adoption {:classpath :not-in-scope :review :not-in-scope})]
    (is (false? (:valid? result)))
    (is (= [{:kind :external-adoption-drift
             :coordinate "io.github.kotoba-lang/security"
             :expected "49fc4ce359752e9fe6e547e9071b5b9b40da937a"
             :actual (:security/git-sha adoption)}]
           (:errors result)))))

(deftest resolved-jar-digest-drift-is-fail-closed
  (let [inventory (tcb/read-inventory)
        chicory (first (keep-indexed
                        (fn [index entry]
                          (when (= :maven (:source entry)) index))
                        (:tcb/external inventory)))
        changed (assoc-in inventory [:tcb/external chicory :sha256]
                          (apply str (repeat 64 "0")))
        result (tcb/validate changed)]
    (is (false? (:valid? result)))
    (is (= :external-digest-drift (-> result :errors first :kind)))))

;; --- transitive closure (classpath) half ----------------------------------

(deftest every-recorded-classpath-entry-has-a-role
  ;; The stronger claim -- every jar the JVM *loaded* is recorded -- cannot be
  ;; answered under the `:test` alias, whose classpath is not the one the
  ;; inventory describes. `clojure -M:tcb-check` asks it on the production
  ;; classpath and is the only place it means anything; asserting it here
  ;; produced seventeen false drifts (ADR-0062).
  ;;
  ;; What is checkable anywhere is the inventory's own side: an entry with no
  ;; role is a jar nobody reviewed, recorded as though someone had.
  (let [entries (:tcb/classpath (tcb/read-inventory))]
    (is (<= 9 (count entries)) "an empty classpath section would pass vacuously")
    (doseq [{:keys [jar role]} entries]
      (is (keyword? role) (str jar " is recorded with no role")))))

(deftest the-closure-covers-what-no-declaration-names
  (let [recorded (set (map :jar (:tcb/classpath (tcb/read-inventory))))]
    (is (some #(re-matches #"clojure-.*\.jar" %) recorded)
        "org.clojure/clojure reaches the classpath transitively and is named by
         no :deps entry -- the gap :tcb/classpath exists to close")))

(deftest classpath-digest-drift-is-fail-closed
  (let [inventory (assoc-in (tcb/read-inventory) [:tcb/classpath 0 :sha256]
                            (apply str (repeat 64 "0")))
        result (tcb/validate inventory)]
    (is (false? (:valid? result)))
    (is (some #(= :classpath-digest-drift (:kind %)) (:errors result)))))

(deftest an-unrecorded-classpath-jar-is-fail-closed
  (let [inventory (update (tcb/read-inventory) :tcb/classpath
                          (fn [entries] (vec (remove #(= "clojure-1.12.5.jar" (:jar %)) entries))))
        result (tcb/validate inventory)]
    (is (false? (:valid? result)))
    (is (some #(and (= :classpath-unrecorded (:kind %))
                    (= "clojure-1.12.5.jar" (:jar %)))
              (:errors result)))))

(deftest a-narrower-classpath-is-not-drift
  (let [inventory (update (tcb/read-inventory) :tcb/classpath
                          conj {:jar "not-on-this-classpath.jar" :role :example
                                :scope :runtime :sha256 (apply str (repeat 64 "0"))})]
    (is (true? (:valid? (tcb/validate inventory
                                      (clojure.edn/read-string (slurp "deps.edn"))
                                      (clojure.edn/read-string (slurp "security-adoption.edn"))
                                      {:classpath :not-in-scope :review :not-in-scope})))
        "the check runs under more than one alias; recorded-but-absent is normal")))

;; --- adopted build properties ---------------------------------------------
;;
;; `qualification/build-identity.edn` names the supply-chain properties this
;; repository holds on purpose. The list is worth what `:tcb/external` was worth
;; before it was checked, so it is checked: a property must name a mechanism
;; that exists and either a gate or an explicit gap.

(def build-identity (edn/read-string (slurp tcb/build-identity-path)))

(defn- with-property [entry]
  ;; A shape fixture, not a review artifact: the recorded digest describes the
  ;; document on disk, and this one has a different :adopted list (ADR-0070).
  (tcb/validate-build-identity (-> build-identity
                                   (assoc :adopted [entry])
                                   (dissoc :build-identity/content-digest))))

(def example-property
  {:property :example
   :statement "An example property."
   :mechanism ["src/aiueos/tcb.clj"]
   :gate ["test/aiueos/tcb_test.clj"]})

(deftest the-checked-in-property-record-is-clean
  (is (= [] (vec (tcb/validate-build-identity))))
  (is (seq (:adopted build-identity)))
  (is (seq (:non-goals build-identity))
      "what is deliberately not taken is part of the record, not an omission"))

(deftest example-property-baseline-is-clean
  (is (= [] (vec (with-property example-property)))))

(deftest a-property-with-neither-a-gate-nor-a-gap-cannot-pass-silently
  (let [errors (with-property (dissoc example-property :gate))]
    (is (= :property-unenforced-and-ungapped (:kind (first errors))))))

(deftest an-unenforced-property-may-declare-its-gap-instead
  (is (= [] (vec (with-property (-> example-property
                                    (dissoc :gate)
                                    (assoc :assurance-gaps [:not-yet-wired])))))))

(deftest a-property-naming-a-mechanism-that-does-not-exist-is-fail-closed
  (let [errors (with-property (assoc example-property :mechanism ["src/aiueos/gone.clj"]))]
    (is (some #(= :property-path-missing (:kind %)) errors))))

(deftest a-trusted-mechanism-outside-the-inventory-is-fail-closed
  ;; Otherwise the implementation of a declared property could change without
  ;; the review the inventory exists to force.
  (let [errors (tcb/validate-build-identity
                (-> build-identity
                    (assoc :adopted [example-property])
                    (dissoc :build-identity/content-digest))
                (assoc (tcb/read-inventory) :tcb/files []))]
    (is (= :property-mechanism-not-in-tcb (:kind (first errors))))))

(deftest a-non-goal-without-a-reason-is-fail-closed
  (let [errors (tcb/validate-build-identity
                (-> build-identity
                    (assoc :non-goals [{:property :something}])
                    (dissoc :build-identity/content-digest)))]
    (is (= :non-goal-without-reason (:kind (first errors))))))

(deftest an-emptied-property-record-is-fail-closed
  (is (= :build-identity-empty
         (:kind (first (tcb/validate-build-identity (assoc build-identity :adopted []))))))
  (is (= :build-identity-missing
         (:kind (first (tcb/validate-build-identity nil))))))

;; --- the review date is checkable (ADR-0067) -------------------------------

(deftest the-review-date-moves-with-the-inventory
  (is (= (:tcb/content-digest (tcb/read-inventory))
         (tcb/inventory-content-digest))
      "an entry changed without the digest being re-recorded, which means
       :tcb/as-of is claiming a review that did not cover it")
  (is (re-matches #"\d{4}-\d{2}-\d{2}" (str (:tcb/as-of (tcb/read-inventory))))
      "and the date is a date"))

(deftest an-entry-changed-without-re-review-is-stale
  (let [changed (update (tcb/read-inventory) :tcb/files
                        (fn [files] (assoc-in (vec files) [0 :role] :something-else)))
        result (tcb/validate changed
                             (edn/read-string (slurp "deps.edn"))
                             (edn/read-string (slurp "security-adoption.edn"))
                             {:classpath :not-in-scope})]
    (is (false? (:valid? result)))
    (is (some #(= :as-of-stale (:kind %)) (:errors result))
        "changing a role is a review question, and the date has to say it was asked")))

(deftest an-inventory-with-no-content-digest-is-fail-closed
  (let [result (tcb/validate (dissoc (tcb/read-inventory) :tcb/content-digest)
                             (edn/read-string (slurp "deps.edn"))
                             (edn/read-string (slurp "security-adoption.edn"))
                             {:classpath :not-in-scope})]
    (is (some #(= :content-digest-missing (:kind %)) (:errors result)))))

;; --- build-identity has a review date too (ADR-0070) -----------------------

(deftest the-adopted-contract-carries-its-own-review-digest
  (let [doc (edn/read-string (slurp "qualification/build-identity.edn"))]
    (is (= (:build-identity/content-digest doc)
           (tcb/build-identity-content-digest doc))
        "a property changed without the digest being re-recorded, so
         :build-identity/as-of is claiming a review that did not cover it")
    (is (re-matches #"\d{4}-\d{2}-\d{2}" (str (:build-identity/as-of doc))))))

(deftest a-property-changed-without-re-review-is-stale
  (let [doc (edn/read-string (slurp "qualification/build-identity.edn"))
        ;; keeps the recorded digest, changes what it covers
        changed (update doc :adopted (fn [ps] (assoc-in (vec ps) [0 :statement] "rewritten")))]
    (is (some #(= :build-identity-as-of-stale (:kind %))
              (tcb/validate-build-identity changed))
        "rewriting a property's statement is a review question")))

(deftest a-named-gate-with-no-tests-in-it-is-not-a-gate
  (let [doc (edn/read-string (slurp "qualification/build-identity.edn"))
        empty-gate "test/aiueos/__empty_gate_probe.cljc"]
    (spit empty-gate ";; deliberately empty: written and deleted by this test\n")
    (try
      (let [changed (-> doc
                        (update :adopted (fn [ps] (assoc-in (vec ps) [0 :gate] [empty-gate])))
                        ;; no digest: this fixture is about the gate floor, and
                        ;; a shape fixture is not a review artifact
                        (dissoc :build-identity/content-digest))]
        (is (some #(= :property-gate-has-no-tests (:kind %))
                  (tcb/validate-build-identity changed))
            "existence was already checked; an empty file passes an existence
             check exactly as well as a full one"))
      (finally (.delete (io/file empty-gate))))))
