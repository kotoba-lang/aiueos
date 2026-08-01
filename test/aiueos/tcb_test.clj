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
    "src/aiueos/vfio.cljc"})

(deftest checked-in-tcb-inventory-has-no-drift
  (is (= {:valid? true :files 24 :external 5 :errors []}
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
;; checks can produce errors.

(def git-dep-inventory
  {:tcb/version 2
   :tcb/files []
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
