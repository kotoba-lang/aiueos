#!/usr/bin/env nbb
;; Runs the admission objects against their contracts WITHOUT a JVM.
;;
;;   node --stack-size=4000 "$(command -v nbb)" \
;;     --classpath "$(clojure -Spath -M:test)" \
;;     os/aiueos/scripts/verify-admissions.cljs [contract.edn ...]
;;
;; `--stack-size` is not decoration. `value-runtime-sha256` is 9 KB of nested
;; `if`, and the ClojureScript analyzer recurses deeply enough over it to
;; exhaust node's default stack; 4000 is measured to be enough and the runner
;; says so rather than reporting a module it could not analyse as absent.
;;
;; WHY NOT `.clj`. These three verifiers were JVM programs calling
;; `kotoba.compiler.core/compile-project`, which is `.clj` and has no portable
;; sibling. Everything under it already was portable -- `kotoba.compiler.project`
;; (the linker), `kotoba.sema` (the frontend) and `kotoba.kir` (the lowering and
;; the interpreter) are all `.cljc` -- so the JVM was carried by one function
;; and one printer. The printer is fixed upstream (amu: `source-text` emitted
;; ClojureScript BigInts as `#object[BigInt 42]`, which the reader then refused
;; on the way back in, so `link-source` failed for every project it would
;; otherwise have linked). This file is the linking that `compile-project`
;; would have done, in the four portable namespaces it delegates to.
;;
;; The cost is measured and not hidden: the ClojureScript interpreter runs
;; SHA-256 roughly ten times slower than the JVM one. See the receipt's
;; `:elapsed-ms` -- it is the price of the acceptance rule that build and
;; verification carry no JVM, not an accident.
(ns verify-admissions
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            [kotoba.compiler.project :as project]
            [kotoba.sema :as sema]
            [kotoba.kir :as ir]
            ["fs" :as fs]
            ["path" :as path]))

(defn- fail! [message data]
  (throw (ex-info message (assoc data :phase :aiueos-verify-admissions))))

(defn- hex-bytes [s]
  (when (odd? (count s)) (fail! "odd-length hexadecimal vector" {:hex s}))
  (mapv #(js/parseInt (apply str %) 16) (partition 2 (seq (or s "")))))

(defn- write-at [image offset bytes]
  (reduce (fn [v [i b]] (assoc v (+ offset i) b)) image (map-indexed vector bytes)))

(defn- word [x] (js/Number x))

;; --- per-contract argument builders ----------------------------------------
;; The three objects take genuinely different arguments, so this is a table and
;; not a clever generalisation. A contract whose `:format` is absent here is a
;; contract this runner cannot honestly claim to have checked.

(defmulti ^:private prepare (fn [contract _vector] (:format contract)))

(defmethod prepare :aiueos.cid-v1-admit/v1 [contract v]
  (let [{:keys [base image-bytes cid-offset scratch-offset block-offset]}
        (get-in contract [:verification :memory])
        cid (hex-bytes (:cid-hex v))
        block (if (:block-bytes v)
                (vec (repeat (:block-bytes v) (or (:fill-byte v) 0)))
                (hex-bytes (:block-hex v)))]
    {:entry 'aiueos-cid-v1-admit
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at cid-offset cid)
                (write-at block-offset block))
     :args [(+ base cid-offset) (count cid)
            (+ base block-offset) (count block)
            (if (some? (:scratch v)) (:scratch v) (+ base scratch-offset))]}))

(defmethod prepare :aiueos.unixfs-file-admit/v1 [contract v]
  (let [{:keys [base image-bytes node-offset]} (get-in contract [:verification :memory])
        node (hex-bytes (:node-hex v))]
    {:entry 'aiueos-unixfs-file-admit
     :base base
     :image (write-at (vec (repeat image-bytes 0)) node-offset node)
     :args [(+ base node-offset) (or (:node-length v) (count node))
            (:expected-links v) (:expected-filesize v)]}))

(defmethod prepare :aiueos.value-runtime-cas-verify/v1 [contract v]
  (let [{:keys [base image-bytes expected-offset output-offset workspace-offset block-offset]}
        (get-in contract [:verification :memory])
        input (if (:generated-bytes v)
                (vec (repeat (:generated-bytes v) (or (:fill-byte v) 0)))
                (hex-bytes (:input-hex v)))]
    {:entry 'aiueos-value-runtime-cas-verify
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at expected-offset (hex-bytes (:expected-digest-hex v)))
                (write-at block-offset input))
     :args [(+ base block-offset) (count input)
            (+ base expected-offset) (+ base output-offset) (+ base workspace-offset)]}))

;; --- step-based contracts --------------------------------------------------
;; `value-handle-arena` is not one call per vector: a vector is a sequence of
;; steps sharing one page, and one of them starts from a page that is already
;; initialized with the lock held. The layout comes from the contract's own
;; `:header` and `:slots`, so this transcribes rather than restates it.

(defn- write-u32 [image offset value]
  (reduce (fn [v [i b]] (assoc v (+ offset i) b))
          image
          (map-indexed vector
                       [(bit-and value 0xff)
                        (bit-and (bit-shift-right value 8) 0xff)
                        (bit-and (bit-shift-right value 16) 0xff)
                        (bit-and (bit-shift-right value 24) 0xff)])))

(defn- arena-page [contract initial image-bytes arena-offset]
  (let [{:keys [lock magic next-handle live-count version]} (:header contract)
        page (vec (repeat image-bytes 0))]
    (if-not initial
      page
      (cond-> page
        true (write-u32 (+ arena-offset (:offset lock)) (or (:lock initial) 0))
        (:initialized? initial)
        (-> (write-u32 (+ arena-offset (:offset magic)) (:value magic))
            (write-u32 (+ arena-offset (:offset version)) (:value version)))
        true (write-u32 (+ arena-offset (:offset next-handle))
                        (or (:next-handle initial) (:initial next-handle)))
        true (write-u32 (+ arena-offset (:offset live-count))
                        (count (:entries initial)))))))

(defmulti ^:private prepare-steps (fn [contract _vector] (:format contract)))

(defmethod prepare-steps :aiueos.value-handle-arena/v1 [contract v]
  (let [{:keys [base image-bytes arena-offset]} (get-in contract [:verification :memory])]
    {:entry 'aiueos-value-handle-arena
     :base base
     :image (arena-page contract (:initial v) image-bytes arena-offset)
     :args-of (fn [[operation handle descriptor]]
                [(+ base arena-offset) (:bytes (:page contract))
                 operation handle descriptor])}))

(defmethod prepare-steps :default [contract _]
  (fail! "REFUSING TO REPORT A PASS: no step builder for this contract format"
         {:format (:format contract)}))

(defmethod prepare :default [contract _]
  (fail! "REFUSING TO REPORT A PASS: no argument builder for this contract format"
         {:format (:format contract)}))

;; --- compile ---------------------------------------------------------------

(defn- compile-graph [root-dir {:keys [root modules]}]
  ;; A contract with no `:graph` would otherwise reach the linker as an empty
  ;; source map and come back as "project source map is empty", which reads
  ;; like a linker problem rather than a missing declaration.
  (when-not (and root (seq modules))
    (fail! "REFUSING TO REPORT A PASS: this contract declares no :graph, so there is nothing to compile"
           {:root root :modules modules}))
  (let [sources (into {} (map (fn [m]
                                [m (fs/readFileSync
                                    (path/join root-dir "os/aiueos/kotoba"
                                               (str (str/replace (name m) #"^aiueos\." "") ".kotoba"))
                                    "utf8")])
                              modules))
        linked (try (project/link-source sources root)
                    (catch :default e
                      (if (str/includes? (str (ex-message e)) "Maximum call stack")
                        (fail! "REFUSING TO REPORT A PASS: the analyzer exhausted node's stack -- rerun with `node --stack-size=4000 $(command -v nbb)`"
                               {:root root})
                        (throw e))))]
    (-> (:source linked)
        ;; The linked namespace carries compiler-generated `__kotoba_*` names,
        ;; which ordinary source is forbidden to use. `compile-project` opens
        ;; the same seam for the same reason.
        (sema/analyze {:admit-linked-synthetics? true})
        ir/lower)))

;; --- run -------------------------------------------------------------------

(defn- execute-once [kir entry args base image fuel]
  (try (word (ir/execute kir entry args
                         {:memory {:base base :bytes image} :fuel fuel}))
       (catch :default e (ex-data e))))

(defn- refuse-on-trap! [contract label actual]
  (when (map? actual)
    (fail! (if (= :kernel-memory-unavailable (:trap actual))
             "REFUSING TO REPORT A PASS: this kotoba-kir cannot execute kernel memory operations, so nothing was actually run"
             "vector trapped where a result was expected")
           {:contract (:format contract) :vector label :trap actual})))

(defn- run-single
  "One call per vector, each against a fresh image."
  [contract kir entry fuel started]
  (let [{:keys [floors]} (:verification contract)
        observed
        (doall
         (for [v (:vectors contract)]
           (let [{:keys [base image args]} (prepare contract v)
                 actual (execute-once kir entry args base (volatile! image) fuel)]
             (refuse-on-trap! contract (:name v) actual)
             (when-not (= (:expected v) actual)
               (fail! "vector mismatch"
                      {:contract (:format contract) :vector (:name v)
                       :expected (:expected v) :actual actual}))
             actual)))
        traps
        (doall
         (for [t (:traps contract)]
           (let [{:keys [base image args]} (prepare contract t)
                 actual (execute-once kir entry args base (volatile! image) fuel)]
             (when-not (map? actual)
               (fail! "trap vector returned a value" {:vector (:name t) :actual actual}))
             (when-not (and (= (:expect-trap t) (:trap actual))
                            (= (:expect-check t) (:check actual)))
               (fail! "trap vector named the wrong fault"
                      {:vector (:name t)
                       :expected [(:expect-trap t) (:expect-check t)]
                       :actual [(:trap actual) (:check actual)]}))
             (:name t))))
        seen (set observed)
        declared (set (keys (:reasons contract)))
        reachable (set (remove (set (:unreachable-by-construction contract)) declared))
        unobserved (sort (remove seen reachable))]

    ;; Floors. A run that executed nothing must not return what a run that
    ;; executed everything returns.
    (when (< (count observed) (or (:minimum-vectors floors) 1))
      (fail! "fewer vectors ran than the contract's floor"
             {:ran (count observed) :floor (:minimum-vectors floors)}))
    (when (and (:every-reachable-reason-observed floors) (seq unobserved))
      (fail! "a declared reason was never produced by any vector" {:unobserved unobserved}))
    (when (and (:both-verdicts-observed floors) (not= #{0 1} seen))
      (fail! "the object never produced both verdicts" {:observed (vec (sort seen))}))
    (when-let [impossible (seq (filter seen (set (:unreachable-by-construction contract))))]
      (fail! "a reason declared unreachable by construction was produced"
             {:reasons (vec (sort impossible))}))

    {:contract (:format contract)
     :vectors (count observed) :traps (count traps)
     :observed (vec (sort seen))
     :elapsed-ms (- (js/Date.now) started)}))

(defn- run-stepped
  "A vector is a sequence of steps sharing ONE image, so the object's own
  writes are what the next step reads. That is the whole point of the arena
  vectors: `:lifecycle-and-nonreuse` only means anything if step 8 sees the
  handle step 6 released."
  [contract kir entry fuel started]
  (let [{:keys [floors]} (:verification contract)
        counted (volatile! 0)]
    (doseq [v (:vectors contract)]
      (let [{:keys [base image args-of]} (prepare-steps contract v)
            page (volatile! image)]
        (when (empty? (:steps v))
          (fail! "REFUSING TO REPORT A PASS: this vector declares no steps"
                 {:vector (:name v)}))
        (doseq [[index step] (map-indexed vector (:steps v))]
          (let [actual (execute-once kir entry (args-of (:args step)) base page fuel)]
            (refuse-on-trap! contract [(:name v) index] actual)
            (when-not (= (:expected step) actual)
              (fail! "step mismatch"
                     {:contract (:format contract) :vector (:name v) :step index
                      :args (:args step) :expected (:expected step) :actual actual}))
            (vswap! counted inc)))))
    (when (< (count (:vectors contract)) (or (:minimum-vectors floors) 1))
      (fail! "fewer vectors ran than the contract's floor"
             {:ran (count (:vectors contract)) :floor (:minimum-vectors floors)}))
    (when (< @counted (or (:minimum-steps floors) 1))
      (fail! "fewer steps ran than the contract's floor"
             {:ran @counted :floor (:minimum-steps floors)}))
    {:contract (:format contract)
     :vectors (count (:vectors contract)) :steps @counted :traps 0
     :elapsed-ms (- (js/Date.now) started)}))

(defn- run-contract [root-dir contract-path]
  (let [contract (edn/read-string (fs/readFileSync contract-path "utf8"))
        {:keys [fuel]} (:verification contract)
        started (js/Date.now)
        ;; Before anything else: a contract with no vectors would otherwise
        ;; compile cleanly, run nothing, and be caught only by the minimum-
        ;; vectors floor further down -- after the builder had already been
        ;; handed `nil` to read the entry symbol out of.
        _ (when (empty? (:vectors contract))
            (fail! "REFUSING TO REPORT A PASS: this contract declares no vectors"
                   {:contract (:format contract)}))
        stepped? (some :steps (:vectors contract))
        kir (compile-graph root-dir (:graph contract))
        exports (set (:exports kir))
        entry (:entry (if stepped?
                        (prepare-steps contract (first (:vectors contract)))
                        (prepare contract (first (:vectors contract)))))]
    (when-not (contains? exports entry)
      (fail! "the linked project does not export the entry this runner calls"
             {:entry entry :exports (vec exports)}))
    (if stepped?
      (run-stepped contract kir entry fuel started)
      (run-single contract kir entry fuel started))))

(defn- compiler-sha
  "The amu this measurement is about. A receipt without it is a number with no
  closure -- `verify_value_runtime_all.clj` says the same about its own, and
  the two are deliberately different pins: this runner needs
  `kotoba.compiler.project`'s ClojureScript printer fix, and the JVM aggregate
  keeps the compiler its committed verdicts were measured against."
  [root-dir]
  (let [deps (edn/read-string (fs/readFileSync (path/join root-dir "deps.edn") "utf8"))
        sha (get-in deps [:aliases :verify-admissions :extra-deps
                          'io.github.kotoba-lang/amu :git/sha])]
    ;; Fail closed rather than print `nil`. A receipt naming no compiler is
    ;; the shape this whole series exists to stop.
    (when-not (and (string? sha) (re-matches #"[0-9a-f]{40}" sha))
      (fail! "REFUSING TO REPORT A PASS: deps.edn declares no amu pin for the :verify-admissions alias, so this receipt could not name the compiler it measured"
             {:found sha}))
    sha))

(defn -main [& args]
  (let [root-dir (or (some #(when (str/starts-with? % "--root=") (subs % 7)) args) ".")
        paths (vec (remove #(str/starts-with? % "--") args))
        ;; Resolved FIRST. It used to be read while printing the receipt, so a
        ;; run with no declared compiler spent six minutes executing vectors
        ;; and then refused -- correct, and the wrong way round.
        amu-sha (compiler-sha root-dir)
        paths (if (seq paths)
                paths
                (mapv #(path/join root-dir "os/aiueos/contracts" %)
                      ["cid-v1-admit-v1.edn" "unixfs-file-admit-v1.edn"
                       "value-runtime-cas-verify-v1.edn"
                       "value-handle-arena-v1.edn"]))
        results (mapv #(run-contract root-dir %) paths)]
    (doseq [r results]
      (println (str "CONTRACT\t" (:contract r)
                    "\tvectors=" (:vectors r)
                    (when (:steps r) (str "\tsteps=" (:steps r)))
                    "\ttraps=" (:traps r)
                    (when (:observed r) (str "\tobserved=" (str/join "," (:observed r))))
                    "\tms=" (:elapsed-ms r))))
    (println (str "CONTRACTS\t" (count results)))
    (println (pr-str {:format :aiueos.verify-admissions/v1
                      :host :nbb :jvm false
                      :amu-sha amu-sha
                      :contracts results
                      :status :passed}))))

;; nbb prints an exception's message and drops its ex-data, so a failing run
;; said "step mismatch" and never which step. The data is the whole diagnosis.
(try (apply -main *command-line-args*)
     (catch :default e
       (println "FAILED:" (ex-message e))
       (println "  " (pr-str (ex-data e)))
       (set! (.-exitCode js/process) 1)))
