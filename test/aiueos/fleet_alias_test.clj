(ns aiueos.fleet-alias-test
  "The fleet alias resolves without credentials.

  A murakumo node holds no credentials by invariant. `:test-fleet` is the alias
  a fleet gate runs, so **every dependency it names must be fetchable
  anonymously** — and one was not: `kotoba-lang/org-ietf-tcp` is private, and
  `clojure -P` on levi died with *could not read Username for
  https://github.com* before a single test ran (ADR-0068).

  The egress probe that cleared this alias for the fleet asked
  `curl https://github.com` and got 200. Reachability is not authorization, and
  a probe that answers a different question than the one asked is the defect
  this series keeps finding, here in its own preparation."
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            [clojure.test :refer [deftest is]]))

(def ^:private anonymous-deps
  "Git dependencies known to be public. Adding a name here is a claim that the
  repository can be cloned with no credentials; the fleet finds out the hard
  way if it is wrong."
  #{'io.github.cognitect-labs/test-runner
    'io.github.kotoba-lang/kotoba-kir
    'io.github.kotoba-lang/amu
    'io.github.kotoba-lang/security
    'io.github.kotoba-lang/abi})

(def ^:private deps (delay (edn/read-string (slurp "deps.edn"))))

(deftest the-fleet-alias-exists-and-is-the-one-the-gate-names
  (let [alias (get-in @deps [:aliases :test-fleet])]
    (is (some? alias) "scripts/fleet-ci/gates.edn names :alias \"test-fleet\"")
    (is (seq (:main-opts alias)))))

(deftest every-fleet-dependency-can-be-fetched-without-credentials
  (doseq [[coord _] (get-in @deps [:aliases :test-fleet :extra-deps])]
    (is (contains? anonymous-deps coord)
        (str coord " is in :test-fleet and is not on the anonymous list. A"
             " fleet node has no credentials: if this repository is private the"
             " gate dies resolving the classpath, before any test runs and"
             " before anything can report why."))))

(deftest the-private-parity-test-is-not-selected
  (let [main-opts (get-in @deps [:aliases :test-fleet :main-opts])]
    (is (some #(str/includes? (str %) "tcp-seq-acceptable-parity-test") main-opts)
        "the namespace requires org-ietf-tcp at load time, so excluding it by
         metadata is not enough -- the runner must not scan it at all")))

(deftest the-full-alias-still-carries-what-the-fleet-cannot
  (let [test-deps (get-in @deps [:aliases :test :extra-deps])]
    (is (contains? test-deps 'io.github.kotoba-lang/org-ietf-tcp)
        "the parity test still runs for a person with credentials; the fleet
         alias is narrower than the suite, not a replacement for it")))
