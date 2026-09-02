#!/usr/bin/env nbb
;; Does the JDK-free route produce the SAME kernel object as the JVM?
;;
;; Compiles every aiueos kernel source both ways in one pass and compares the
;; two digests directly, so the answer never depends on a previously recorded
;; run of the other route. Both invocations go through `bin/amu`: the JVM-free
;; one with --jvm-free (which refuses rather than falling back), the other by
;; calling `clojure -M:run` the way the launcher does before this change.
;;
;; Evidence floors (ADR-2608136000): a source that could not be compiled by
;; EITHER route is :could-not-run, never :differs -- an unanswered comparison
;; must not read like an answered one. A pass over zero sources refuses with
;; its own exit code rather than reporting a clean parity.
(ns verify-jvm-free-object-parity
  (:require ["node:child_process" :as child]
            ["node:crypto" :as crypto]
            ["node:fs" :as fs]
            ["node:os" :as os]
            ["node:path" :as path]))

(def root (or (first (filter #(not (.startsWith % "--")) *command-line-args*))
              (throw (js/Error. "usage: verify-jvm-free-object-parity.cljs <aiueos-root> --compiler <dir>"))))
(def compiler
  (let [args (vec *command-line-args*)
        i (.indexOf args "--compiler")]
    (when (neg? i) (throw (js/Error. "--compiler <amu checkout> is required")))
    (nth args (inc i))))
(def only
  (let [args (vec *command-line-args*)
        i (.indexOf args "--only")]
    (when-not (neg? i) (js/parseInt (nth args (inc i))))))

(def kotoba-dir (.join path root "os" "aiueos" "kotoba"))
(def target "x86_64-aiueos-kernel-v1")

(defn- sha256 [buf] (-> (.createHash crypto "sha256") (.update buf) (.digest "hex")))

(defn- tree-digest
  "One digest over every compiler source this run depends on.

  Taken before the first compile and again after the last. A run whose compiler
  changed underneath it has compared two different compilers and must not
  report digests -- measured the hard way on 2026-08-31, when editing
  `native_package.cljs` mid-run made two objects `differ` that are byte-identical
  when either route is run alone. The earlier object-producer measurement was
  discarded for exactly this (aiueos ADR-0129); a run that can be silently
  disturbed will be, so the disturbance is made loud instead of remembered."
  [dir]
  (let [hash (.createHash crypto "sha256")]
    (letfn [(walk [d]
              (doseq [entry (sort (.readdirSync fs d))
                      :let [full (.join path d entry)]
                      :when (not (contains? #{"node_modules" ".git" "target"} entry))]
                (if (.isDirectory (.statSync fs full))
                  (walk full)
                  (do (.update hash full) (.update hash (.readFileSync fs full))))))]
      (walk (.join path dir "src"))
      (walk (.join path dir "bin"))
      (.update hash (.readFileSync fs (.join path dir "deps-lock.edn"))))
    (.digest hash "hex")))

;; A toolchain that never started, not an object that would not compile. This
;; machine runs many agents at once; at load average 76 the JVM frontend has
;; failed to LOAD, and `measure-object-producer.cljs` already retries for it
;; (aiueos ADR-0129). This script shipped without that and immediately paid for
;; it: `ipv4-checksum` was reported FAILED in the first full run and compiles
;; and MATCHES when run alone. Reporting "could not answer" as "answered no" is
;; the defect this whole family of tools exists to find.
(def ^:private could-not-run
  [#"FileNotFoundException" #"MODULE_NOT_FOUND" #"could not resolve the dependency closure"
   #"ClassNotFoundException" #"Could not locate" #"Killed" #"ETIMEDOUT"
   #"OutOfMemoryError" #"Cannot allocate memory" #"Resource temporarily unavailable"
   #"ExceptionInInitializerError" #"NoClassDefFoundError" #"Too many open files"
   #"Syntax error compiling" #"Unable to resolve" #"Execution error \(.*Exception\) at clojure"])

(defn- classify [text]
  (cond (re-find #"POISONED" text) :reached-a-jvm
        (some #(re-find % text) could-not-run) :could-not-run
        :else :failed))

;; The JVM-free route is not asked to be JVM-free, it is PREVENTED from
;; reaching one: `clojure` and `java` on its PATH are stubs that exit 77 and
;; say so. Without this the run would be measuring the flag's intent rather
;; than its effect -- and a silent fallback would report a clean parity,
;; because a JVM-built object obviously matches a JVM-built object.
(def poison-dir (.mkdtempSync fs (.join path (.tmpdir os) "no-jvm-")))
(doseq [name ["clojure" "java" "javac" "clj"]]
  (let [p (.join path poison-dir name)]
    (.writeFileSync fs p (str "#!/bin/sh\necho 'POISONED " name " was invoked' >&2\nexit 77\n"))
    (.chmodSync fs p 0755)))

(defn- run [cmd args & [{:keys [no-jvm?]}]]
  (let [env (js/Object.assign #js {} (.-env js/process))]
    (when no-jvm?
      (set! (.-PATH env) (str poison-dir ":" (.-PATH env))))
    (let [r (.spawnSync child cmd (clj->js args)
                        #js {:cwd compiler :encoding "utf8" :timeout 1800000
                             :maxBuffer (* 64 1024 1024) :env env})]
      {:status (.-status r)
       :text (str (.-stdout r) (.-stderr r))})))

;; The project route is entered ON DEMAND, and the demand is the compiler's own
;; refusal.
;;
;; A source that declares `(:require ...)` is a project and needs
;; `--source-path`; without it amu answers
;; `:kotoba.error/namespace-require-needs-project`. Five committed objects are
;; built that way since amu#742 (aes128-gcm, tls13-record, hkdf-sha256,
;; cid-v1-admit, value-runtime-cas-verify) and this script would have reported
;; every one of them as :failed -- a statement about the invocation, not about
;; the object.
;;
;; Passing the flag ALWAYS is not the fix, and that was measured rather than
;; assumed: `fnv1a.kotoba` declares no `ns` at all, and with `--source-path` it
;; is refused with "project module requires exactly one namespace" (2026-09-02,
;; amu bb51dc14). Deciding from a regex over the source is not the fix either
;; -- a `(:require ...)` spelled in a way the regex missed would get the wrong
;; invocation and a verdict that reads like an answer.
;;
;; So the compiler decides. Plain first; if the refusal names the project
;; route, run it again with `--source-path`. Both routes make the same decision
;; independently, so neither is measured under a shape the other did not get.
;; `--unpinned` travels WITH `--source-path`, and it is not decoration: the two
;; routes disagree about what a bare `--source-path` means. The JVM route
;; refuses a multi-module compile without it ("a multi-module compile needs
;; pinned inputs"); the JVM-free route proceeds and reports
;; `:kotoba.compile/inputs :unpinned-source-path`. Sending the flag on both
;; makes the comparison about the OBJECT rather than about that divergence.
;; Measured 2026-09-02: adding it to the JVM-free route leaves the bytes
;; unchanged (cid-v1-admit, 3aeb2308... either way).
(defn- invoke [route src out source-path?]
  (case route
    :jvm-free (run (.join path compiler "bin" "amu")
                   (cond-> ["compile" src "--target" target "--output" out "--jvm-free"]
                     source-path? (conj "--source-path" kotoba-dir "--unpinned"))
                   {:no-jvm? true})
    :jvm (run "clojure"
              (cond-> ["-M:run" "compile" src "--target" target "--output" out]
                source-path? (conj "--source-path" kotoba-dir "--unpinned")))))

(defn- compile-one [route src out]
  (let [plain (invoke route src out false)
        {:keys [status text]}
        (if (and (not (zero? (:status plain)))
                 (re-find #"namespace-require-needs-project" (:text plain)))
          (do (.rmSync fs out #js {:force true})
              (invoke route src out true))
          plain)]
    (if (and (zero? status) (.existsSync fs out))
      {:ok true :sha (sha256 (.readFileSync fs out)) :bytes (.-size (.statSync fs out))}
      {:ok false :verdict (classify text)
       ;; The FIRST diagnostic line, not the last. A Java stack trace ends in
       ;; "... 1 more", which names nothing; the exception type is at the top.
       :message (or (some->> (.split text "\n")
                             (filter #(re-find #"(?i)error|exception|refus|denied|reject" %))
                             first
                             (#(.slice (str %) 0 240)))
                    (-> text (.split "\n") (->> (remove empty?) last str)
                        (.slice 0 240)))})))

;; Retry EVERY failure once, not only the ones a pattern recognises.
;;
;; The pattern list was grown one transient at a time and was wrong twice in one
;; afternoon: first `FileNotFoundException` loading the JVM frontend, then
;; `ExceptionInInitializerError`. Both objects compile, and match, when run
;; alone. Enumerating the ways a loaded machine can fail to start a toolchain is
;; not a list that converges -- but a transient does not reproduce, and that IS
;; a test. So the retry decides, and the patterns only classify what survived
;; it. Failures are rare enough that retrying all of them costs almost nothing.
(defn- compile-with-retry [route src out]
  (let [first-try (compile-one route src out)]
    (if (:ok first-try)
      first-try
      (do (.rmSync fs out #js {:force true})
          (let [second-try (compile-one route src out)]
            (assoc second-try :retried true
                   ;; Survived a retry AND looks like a toolchain that never
                   ;; started: still not an answer about the object.
                   :verdict (:verdict second-try)))))))

;; `<stem>.kotoba` beside the object, or the ns->path spelling
;; `aiueos/<munged stem>.kotoba` its source moves to when it becomes an
;; importable module. Looking only for the flat name would DROP those objects
;; from the run -- `sha256.o` and `digest-equal.o` are exactly that case since
;; ADR-0137 -- and a shorter list reads as a clean pass over everything that
;; was left.
(defn- source-of [base]
  (->> [(str base ".kotoba")
        (str "aiueos/" (.replaceAll base "-" "_") ".kotoba")]
       (filter #(.existsSync fs (.join path kotoba-dir %)))
       first))

(def sources
  (->> (.readdirSync fs kotoba-dir)
       (filter #(.endsWith % ".o"))
       (map #(.slice % 0 -2))
       (filter source-of)
       sort
       (#(if only (take only %) %))
       vec))

(when (zero? (count sources))
  (println "REFUSED\tno kernel sources found under" kotoba-dir)
  (.exit js/process 2))

(def work (.mkdtempSync fs (.join path (.tmpdir os) "parity-")))
(def compiler-before (tree-digest compiler))

(def results
  (vec (for [base sources
             :let [src (.join path kotoba-dir (source-of base))
                   a (compile-with-retry :jvm-free src (.join path work (str base ".nbb.o")))
                   b (compile-with-retry :jvm src (.join path work (str base ".jvm.o")))
                   verdict (cond
                             (and (:ok a) (:ok b)) (if (= (:sha a) (:sha b)) :match :differs)
                             ;; Either route unable to answer makes the
                             ;; COMPARISON unanswered, whichever side it was.
                             (= :reached-a-jvm (:verdict a)) :reached-a-jvm
                             (or (= :could-not-run (:verdict a))
                                 (= :could-not-run (:verdict b))) :could-not-run
                             :else :failed)]]
         (do (println (str base "\t" (name verdict)
                           (when (= :match verdict) (str "\t" (:bytes a) "B"))
                           (when (= :differs verdict)
                             (str "\tnbb=" (:sha a) " jvm=" (:sha b)))
                           (when (contains? #{:could-not-run :failed :reached-a-jvm} verdict)
                             (str "\t" (or (:message a) (:message b))))))
             (cond-> {:object base :verdict verdict
                      :retried (boolean (or (:retried a) (:retried b)))}
               (:ok a) (assoc :bytes (:bytes a) :sha256 (:sha a))
               (not (:ok a)) (assoc :jvm-free-message (:message a))
               (not (:ok b)) (assoc :jvm-message (:message b)))))))

(.rmSync fs work #js {:recursive true :force true})
(.rmSync fs poison-dir #js {:recursive true :force true})

(let [compiler-after (tree-digest compiler)]
  (when-not (= compiler-before compiler-after)
    (println (str "REFUSED\tthe compiler changed while this run was reading it"
                  "\n\tbefore " compiler-before "\n\tafter  " compiler-after
                  "\n\tEvery verdict above compared two different compilers."
                  " Discard them and re-run against a checkout nothing edits."))
    (.exit js/process 3)))

(def by (frequencies (map :verdict results)))
(def summary
  {:format :aiueos.jvm-free-object-parity/v1
   :target (keyword target)
   :scanned (count results)
   :compiler-tree-sha256 compiler-before
   :match (get by :match 0) :differs (get by :differs 0)
   :could-not-run (get by :could-not-run 0) :failed (get by :failed 0)
   :reached-a-jvm (get by :reached-a-jvm 0)
   :retried (count (filter :retried results))
   :results results})
(println (str "SCANNED\t" (count results)
              "\tMATCH\t" (get by :match 0)
              "\tDIFFERS\t" (get by :differs 0)
              "\tCOULD-NOT-RUN\t" (get by :could-not-run 0)
              "\tFAILED\t" (get by :failed 0)
              "\tREACHED-A-JVM\t" (get by :reached-a-jvm 0)))
(let [out (.join path root "qualification" "jvm-free-object-parity.edn")]
  (when (.existsSync fs (.dirname path out))
    (.writeFileSync fs out (str (pr-str summary) "\n"))
    (println "wrote" out)))
(.exit js/process (cond (pos? (get by :could-not-run 0)) 2
                        (pos? (+ (get by :differs 0) (get by :failed 0)
                                 (get by :reached-a-jvm 0))) 1
                        :else 0))
