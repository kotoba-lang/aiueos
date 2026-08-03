(ns aiueos.tcb-test
  (:require [aiueos.tcb :as tcb]
            [clojure.edn :as edn]
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
  (is (= {:valid? true :files 26 :external 5 :classpath 9 :errors []}
         (tcb/validate))))

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
  (is (true? (:valid? (tcb/validate git-dep-inventory git-deps-edn nil)))))

(deftest runtime-dependency-missing-from-the-inventory-is-fail-closed
  (let [deps (assoc-in git-deps-edn [:deps 'io.github.example/unlisted]
                       {:mvn/version "1.0.0"})
        result (tcb/validate git-dep-inventory deps nil)]
    (is (false? (:valid? result)))
    (is (= [{:kind :external-undeclared
             :coordinate "io.github.example/unlisted"}]
           (:errors result)))))

(deftest git-pin-drift-between-inventory-and-deps-is-fail-closed
  (let [deps (assoc-in git-deps-edn [:deps 'io.github.example/lib :git/sha]
                       "bbbb000000000000000000000000000000000000")
        result (tcb/validate git-dep-inventory deps nil)]
    (is (false? (:valid? result)))
    (is (= :external-git-sha-drift (-> result :errors first :kind)))))

(deftest an-unpinned-external-dependency-cannot-pass-silently
  (let [inventory (update-in git-dep-inventory [:tcb/external 0] dissoc :git-sha)
        result (tcb/validate inventory git-deps-edn nil)]
    (is (false? (:valid? result)))
    (is (= :external-unpinned (-> result :errors first :kind)))))

(deftest a-platform-entry-without-a-declared-gap-cannot-pass-silently
  (let [inventory (assoc git-dep-inventory
                         :tcb/external [{:coordinate "java.base" :source :platform
                                         :role :crypto-ffi-process-runtime}])
        result (tcb/validate inventory {:deps {}} nil)]
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
  (:errors (tcb/validate (platform-inventory entry) {:deps {}} nil)))

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
        result (tcb/validate inventory deps adoption)]
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

(deftest every-classpath-jar-is-recorded-with-a-role
  (let [recorded (into {} (map (juxt :jar identity))
                       (:tcb/classpath (tcb/read-inventory)))]
    (is (every? #(keyword? (:role (get recorded (:jar %)))) (tcb/classpath-jars))
        "a jar loaded but unrecorded is an unreviewed member of the TCB closure")))

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
    (is (true? (:valid? (tcb/validate inventory)))
        "the check runs under more than one alias; recorded-but-absent is normal")))
