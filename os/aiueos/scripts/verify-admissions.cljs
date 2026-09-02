#!/usr/bin/env nbb
;; Runs the admission objects against their contracts WITHOUT a JVM.
;;
;;   node --stack-size=4000 "$(command -v nbb)" \
;;     --classpath "$(clojure -Spath -M:test)" \
;;     os/aiueos/scripts/verify-admissions.cljs [contract.edn ...]
;;
;; `--stack-size` is not decoration, and 4000 IS NOT ENOUGH FOR EVERY CONTRACT.
;; Two different things need it. `value-runtime-sha256` is 9 KB of nested `if`
;; and the ClojureScript ANALYZER recurses over it; 4000 is measured to be
;; enough for that, and the runner says so rather than reporting a module it
;; could not analyse as absent. The INTERPRETER needs its own room, once per
;; Kotoba call: `aes128-gcm` is 4,565 calls deep per GHASH multiplication, and
;; at 4000 the first vectors of a cold run trapped while later ones passed --
;; V8 shrinks the interpreter's frames once it tiers up, so the limit is
;; TIMING-DEPENDENT rather than a fixed property of the object.
;;
;; Each contract states what it needs in `:verification :node-stack-size`. Run
;; the deep ones as
;;   ulimit -s 65520
;;   node --stack-size=60000 "$(command -v nbb)" --classpath ... verify-admissions.cljs ...
;; `--stack-size` above the thread's actual stack segfaults instead of
;; throwing, so the `ulimit` is the half that makes the flag safe.
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

;; AES-128-GCM is the first contract here whose ANSWER IS NOT THE RETURN VALUE.
;; `aiueos-cid-v1-admit` and its neighbours decide; this one transforms, and a
;; reason code of 0 says only that the object thought it succeeded. The
;; ciphertext, the tag and -- for a refused open -- the fact that the buffer
;; still holds ciphertext are all in memory, so `:expect-memory` carries them
;; and `run-single` compares them against the image the object actually wrote.
(defmethod prepare :aiueos.aes128-gcm/v1 [contract v]
  (let [{:keys [base image-bytes ctx-offset data-offset]}
        (get-in contract [:verification :memory])
        aad (hex-bytes (:aad-hex v))
        data (hex-bytes (:data-hex v))
        ;; The overrides exist so a refusal can be provoked by a LENGTH the
        ;; object is handed rather than by bytes it would have to read to find
        ;; the problem -- refusing 12289 must not cost 12289 bytes of image.
        aad-len (or (:aad-len-override v) (count aad))
        data-len (or (:data-len-override v) (count data))]
    {:entry 'aiueos-aes128-gcm
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at ctx-offset (hex-bytes (:key-hex v)))
                (write-at (+ ctx-offset 16) (hex-bytes (:nonce-hex v)))
                (write-at (+ ctx-offset 28) [aad-len])
                (write-at (+ ctx-offset 32) (hex-bytes (:tag-hex v)))
                (write-at (+ ctx-offset 64) aad)
                (write-at data-offset data))
     :args [(+ base ctx-offset)
            (or (:ctx-len v) (get-in contract [:ctx :bytes]))
            (if (some? (:data-base-override v))
              (:data-base-override v)
              (+ base data-offset))
            data-len
            (:mode v)]
     :expect-memory
     (cond-> []
       (:expect-data-hex v)
       (conj {:label :data :offset data-offset
              :bytes (hex-bytes (:expect-data-hex v))})
       (:expect-tag-hex v)
       (conj {:label :tag :offset (+ ctx-offset 32)
              :bytes (hex-bytes (:expect-tag-hex v))}))}))

;; The key schedule beside the AEAD, and the same reason for `:expect-memory`:
;; the reason code says the derivation ran, and the derived bytes are what
;; decides whether the connection is readable by the peer or by anyone else.
(defmethod prepare :aiueos.hkdf-sha256/v1 [contract v]
  (let [{:keys [base image-bytes ctx-offset]} (get-in contract [:verification :memory])
        key (hex-bytes (:key-hex v))
        label (hex-bytes (:label-hex v))
        context (hex-bytes (:context-hex v))]
    {:entry 'aiueos-hkdf-sha256
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at ctx-offset key)
                (write-at (+ ctx-offset 96)
                          [(or (:key-len-override v) (count key))
                           (or (:label-len-override v) (count label))
                           (or (:context-len-override v) (count context))
                           (or (:out-len v) 32)])
                (write-at (+ ctx-offset 112) label)
                (write-at (+ ctx-offset 128) context))
     :args [(+ base ctx-offset)
            (or (:ctx-len v) (get-in contract [:ctx :bytes]))
            (:mode v)]
     :expect-memory
     (cond-> []
       (:expect-out-hex v)
       (conj {:label :out :offset (+ ctx-offset 64)
              :bytes (hex-bytes (:expect-out-hex v))}))}))

(defn- be-bytes
  "`n` as `width` big-endian bytes. The record layer's sequence number, lengths
  and the contract's expectations are all big-endian fields."
  [n width]
  (mapv (fn [i] (bit-and (js/Math.floor (/ n (js/Math.pow 256 (- width 1 i)))) 0xff))
        (range width)))

;; The record layer. Three regions' worth of state in two: the ctx carries the
;; key, the IV, the sequence and the in/out length fields, and `rec` is the
;; record itself, transformed in place.
(defmethod prepare :aiueos.tls13-record/v1 [contract v]
  (let [{:keys [base image-bytes ctx-offset rec-offset]}
        (get-in contract [:verification :memory])
        record (hex-bytes (:record-hex v))
        plaintext (hex-bytes (:plaintext-hex v))
        seal? (= 1 (:mode v))
        ;; A record image is either the literal bytes, or a run of zeros with
        ;; those bytes written over its head -- the second shape is how a
        ;; 12,310-byte refusal is provoked without 12 KiB of hex in a contract.
        rec-image (if (:record-fill-bytes v)
                    (reduce (fn [acc [i b]] (assoc acc i b))
                            (vec (repeat (:record-fill-bytes v) 0))
                            (map-indexed vector record))
                    record)
        plaintext-length (or (:plaintext-length-override v) (count plaintext))
        rec-len (or (:rec-len v)
                    (if seal? (+ 22 (count plaintext)) (count rec-image)))]
    {:entry 'aiueos-tls13-record
     :base base
     :image (cond-> (-> (vec (repeat image-bytes 0))
                        (write-at ctx-offset (hex-bytes (:key-hex v)))
                        (write-at (+ ctx-offset 16) (hex-bytes (:iv-hex v)))
                        (write-at (+ ctx-offset 48) (be-bytes (or (:sequence v) 0) 8))
                        (write-at (+ ctx-offset 56) [(or (:content-type v) 0)])
                        (write-at (+ ctx-offset 58) (be-bytes plaintext-length 2))
                        (write-at rec-offset rec-image))
              ;; SEAL is handed the plaintext where the record body will be.
              seal? (write-at (+ rec-offset 5) plaintext))
     :args [(+ base ctx-offset)
            (or (:ctx-len v) (get-in contract [:ctx :bytes]))
            (+ base rec-offset) rec-len (:mode v)]
     :expect-memory
     (cond-> []
       (:expect-record-hex v)
       (conj {:label :record :offset rec-offset
              :bytes (hex-bytes (:expect-record-hex v))})
       (:expect-plaintext-hex v)
       (conj {:label :plaintext :offset (+ rec-offset 5)
              :bytes (hex-bytes (:expect-plaintext-hex v))})
       (:expect-content-type v)
       (conj {:label :content-type :offset (+ ctx-offset 56)
              :bytes [(:expect-content-type v)]})
       (some? (:expect-plaintext-length v))
       (conj {:label :plaintext-length :offset (+ ctx-offset 58)
              :bytes (be-bytes (:expect-plaintext-length v) 2)})
       (some? (:expect-record-length v))
       (conj {:label :record-length :offset (+ ctx-offset 60)
              :bytes (be-bytes (:expect-record-length v) 2)}))}))

;; --- the Qwen3.5 forward pass, first tranche (ADR-0135) -------------------
;;
;; Three objects that are three copies of one piece of arithmetic:
;; `aiueos-qwen35-matvec` inlines the other two, because a kernel object
;; exports one symbol and cannot call another. So the three builders below feed
;; the SAME oracle-derived bytes into all three, and a divergence between them
;; is a vector mismatch here rather than something to be discovered later.
;;
;; The expected bytes come from an independent ClojureScript re-derivation of
;; the C over `Float32Array`, so every rounding in an expectation is a real
;; binary32 rounding. That proves the algorithm against a second reading and
;; says nothing about what amu emitted; the evidence that covers the backend is
;; the QEMU self-test `QWEN-PARITY`, which compares the emitted objects
;; against the compiled C on the CPU.

(defn- row-bytes
  "`aiueos_qwen35_quant_row_bytes` for the four types this tranche decodes."
  [qtype count]
  (case qtype
    0 (* count 4)
    8 (* (quot count 32) 34)
    12 (* (quot count 256) 144)
    14 (* (quot count 256) 210)
    ;; A type the object refuses. The runner still has to place SOMETHING, and
    ;; a Q5_K row is the honest size for the vector that provokes -6/-5.
    13 (* (quot count 256) 176)
    (fail! "no row byte count for this quantisation type" {:qtype qtype})))

(defmethod prepare :aiueos.qwen35-dequant-row/v1 [contract v]
  (let [{:keys [base image-bytes src-offset dst-offset]}
        (get-in contract [:verification :memory])
        row (hex-bytes (:row-hex v))
        count (:count v)
        src-len (or (:src-len-override v) (row-bytes (:qtype v) count))
        dst-len (or (:dst-len-override v) (* count 4))]
    {:entry 'aiueos-qwen35-dequant-row
     :base base
     :image (write-at (vec (repeat image-bytes 0)) src-offset row)
     :args [(:qtype v)
            (if (some? (:src-base-override v)) (:src-base-override v) (+ base src-offset))
            src-len
            (if (some? (:dst-base-override v)) (:dst-base-override v) (+ base dst-offset))
            dst-len]
     :expect-memory
     (cond-> []
       (:expect-row-hex v)
       (conj {:label :row :offset dst-offset :bytes (hex-bytes (:expect-row-hex v))}))}))

(defmethod prepare :aiueos.qwen35-dot-f32/v1 [contract v]
  (let [{:keys [base image-bytes left-offset right-offset]}
        (get-in contract [:verification :memory])
        left (hex-bytes (:left-hex v))
        right (hex-bytes (:right-hex v))
        count (or (:count-override v) (:count v))]
    {:entry 'aiueos-qwen35-dot-f32
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at left-offset left)
                (write-at right-offset right))
     ;; The overrides exist so a refusal can be provoked by a LENGTH rather
     ;; than by bytes the runner would have to place: refusing a count of
     ;; 65,537 must not cost 262 KiB of image.
     :args [(+ base left-offset)
            (or (:a-len-override v) (:len-override v) (* 4 (max count 0)))
            (+ base right-offset)
            (or (:b-len-override v) (:len-override v) (* 4 (max count 0)))
            count]}))


(defn- write-u64 [image offset value]
  (-> image
      (write-u32 offset (mod value 4294967296))
      (write-u32 (+ offset 4) (js/Math.floor (/ value 4294967296)))))

(defmethod prepare :aiueos.qwen35-matvec/v1 [contract v]
  (let [{:keys [base image-bytes arena-offset]}
        (get-in contract [:verification :memory])
        qtype (:qtype v)
        rows (:rows v)
        cols (:cols v)
        rb (row-bytes qtype cols)
        weights (hex-bytes (or (:weights-hex v) ""))
        input (hex-bytes (or (:input-hex v) ""))
        weights-bytes (* rows rb)
        input-offset weights-bytes
        output-offset (+ input-offset (* cols 4))
        scratch-offset (+ output-offset (* rows 4))
        arena-bytes (+ scratch-offset (* cols 4))
        plan-offset (+ arena-offset arena-bytes)
        plan (-> (vec (repeat 96 0))
                 (write-u32 0 qtype)
                 (write-u32 4 (or (:reserved-4 v) 0))
                 (write-u64 8 (or (:rows-override v) rows))
                 (write-u64 16 (or (:cols-override v) cols))
                 (write-u64 24 (or (:weights-offset-override v) 0))
                 (write-u64 32 (or (:weights-bytes-override v) weights-bytes))
                 (write-u64 40 input-offset)
                 (write-u64 48 output-offset)
                 (write-u64 56 scratch-offset)
                 (write-u64 64 (or (:first-override v) (:first v) 0))
                 (write-u64 72 (or (:end-override v) (:end v) rows))
                 (write-u64 80 (or (:reserved-80 v) 0))
                 (write-u64 88 (or (:reserved-88 v) 0)))]
    (when (> (+ plan-offset 96) image-bytes)
      (fail! "REFUSING TO REPORT A PASS: this vector's arena does not fit the contract's image"
             {:vector (:name v) :needed (+ plan-offset 96) :image-bytes image-bytes}))
    {:entry 'aiueos-qwen35-matvec
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at arena-offset weights)
                (write-at (+ arena-offset input-offset) input)
                (write-at plan-offset plan))
     :args [(if (some? (:arena-base-override v)) (:arena-base-override v) (+ base arena-offset))
            arena-bytes
            (if (some? (:plan-base-override v)) (:plan-base-override v) (+ base plan-offset))
            (or (:plan-len-override v) 96)]
     :expect-memory
     (cond-> []
       (:expect-output-hex v)
       (conj {:label :output :offset (+ arena-offset output-offset)
              :bytes (hex-bytes (:expect-output-hex v))}))}))

(defmethod prepare :default [contract _]
  (fail! "REFUSING TO REPORT A PASS: no argument builder for this contract format"
         {:format (:format contract)}))

;; --- compile ---------------------------------------------------------------

(def ^:private kotoba-dir "os/aiueos/kotoba")

(defn- module-source
  "The file a module namespace names, by the SAME rule amu's resolver uses --
  `.` becomes `/`, `-` becomes `_` -- with the historical flat spelling kept as
  a fallback.

  Both are needed and neither is optional. Every module that existed before
  today sits flat in `os/aiueos/kotoba` under a hyphenated name that no
  namespace munging produces (`aiueos.hkdf-sha256` ->
  `hkdf-sha256.kotoba`), so dropping the fallback would stop every existing
  contract compiling. But `amu compile --source-path` resolves
  `aiueos.lib.sha256-core` to `aiueos/lib/sha256_core.kotoba` and will not find
  a flat file, so a module written to be shared has to be reachable by the
  munged path -- and this verifier has to agree with the compiler about which
  bytes a namespace names, or it verifies a different program than the one that
  ships."
  [root-dir namespace]
  (let [munged (-> (str namespace) (str/replace "." "/") (str/replace "-" "_"))
        flat (str/replace (name namespace) #"^aiueos\." "")
        candidates [(path/join root-dir kotoba-dir (str munged ".kotoba"))
                    (path/join root-dir kotoba-dir (str flat ".kotoba"))]
        found (first (filter #(.existsSync fs %) candidates))]
    (when-not found
      (fail! "REFUSING TO REPORT A PASS: a declared module has no source file"
             {:module namespace :tried (vec candidates)}))
    (fs/readFileSync found "utf8")))

(defn- compile-graph [root-dir {:keys [root modules]}]
  ;; A contract with no `:graph` would otherwise reach the linker as an empty
  ;; source map and come back as "project source map is empty", which reads
  ;; like a linker problem rather than a missing declaration.
  (when-not (and root (seq modules))
    (fail! "REFUSING TO REPORT A PASS: this contract declares no :graph, so there is nothing to compile"
           {:root root :modules modules}))
  (let [sources (into {} (map (fn [m] [m (module-source root-dir m)])) modules)
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
    (fail! (cond
             (= :kernel-memory-unavailable (:trap actual))
             "REFUSING TO REPORT A PASS: this kotoba-kir cannot execute kernel memory operations, so nothing was actually run"
             ;; The interpreter recurses once per Kotoba call, so a deeply
             ;; recursive object exhausts NODE'S STACK and `kir/execute` reports
             ;; it as `:fuel-exhausted` with `:host-stack-exhausted true`. That
             ;; is the host running out of room, not the object running out of
             ;; fuel and not the object being wrong -- and at 4000 it is
             ;; TIMING-DEPENDENT: measured 2026-09-02 on aes128-gcm, the first
             ;; vectors of a cold run trapped and later ones passed, because V8
             ;; shrinks the interpreter's frames once it tiers up. Reported as a
             ;; refusal with the flag that names it, never as a mismatch.
             (:host-stack-exhausted actual)
             "REFUSING TO REPORT A PASS: node's stack was exhausted, so this vector was not executed -- rerun with a larger `ulimit -s` and `node --stack-size=...` (the contract's :verification :node-stack-size states what this one needs)"
             :else "vector trapped where a result was expected")
           {:contract (:format contract) :vector label :trap actual})))

;; The bytes the object left behind, compared against the bytes the contract
;; says it should have. A vector that declares none of these is checking only
;; the reason code, which for a transforming object is most of nothing.
(defn- check-memory! [contract label expect-memory page]
  (let [image @page]
    (reduce
     (fn [n {:keys [offset bytes] :as expectation}]
       (let [actual (subvec image offset (+ offset (count bytes)))]
         (when-not (= (vec bytes) (vec actual))
           (fail! "memory mismatch"
                  {:contract (:format contract) :vector label
                   :region (:label expectation) :offset offset
                   :expected (mapv #(bit-and % 0xff) bytes)
                   :actual (mapv #(bit-and % 0xff) actual)})))
       (inc n))
     0
     expect-memory)))

(defn- run-single
  "One call per vector, each against a fresh image."
  [contract kir entry fuel started]
  (let [{:keys [floors]} (:verification contract)
        memory-assertions (volatile! 0)
        observed
        (doall
         (for [v (:vectors contract)]
           (let [{:keys [base image args expect-memory]} (prepare contract v)
                 page (volatile! image)
                 actual (execute-once kir entry args base page fuel)]
             (refuse-on-trap! contract (:name v) actual)
             (when-not (= (:expected v) actual)
               (fail! "vector mismatch"
                      {:contract (:format contract) :vector (:name v)
                       :expected (:expected v) :actual actual}))
             (vswap! memory-assertions +
                     (check-memory! contract (:name v) expect-memory page))
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
    ;; A contract that declares a memory floor and then runs vectors carrying no
    ;; `:expect-memory` has checked reason codes only. For a transforming object
    ;; that is a pass over the part that does not matter.
    (when (< @memory-assertions (or (:minimum-memory-assertions floors) 0))
      (fail! "fewer memory assertions ran than the contract's floor"
             {:ran @memory-assertions
              :floor (:minimum-memory-assertions floors)}))
    (when (and (:every-reachable-reason-observed floors) (seq unobserved))
      (fail! "a declared reason was never produced by any vector" {:unobserved unobserved}))
    (when (and (:both-verdicts-observed floors) (not= #{0 1} seen))
      (fail! "the object never produced both verdicts" {:observed (vec (sort seen))}))
    (when-let [impossible (seq (filter seen (set (:unreachable-by-construction contract))))]
      (fail! "a reason declared unreachable by construction was produced"
             {:reasons (vec (sort impossible))}))

    {:contract (:format contract)
     :vectors (count observed) :traps (count traps)
     :memory-assertions @memory-assertions
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
                      ;; The two TLS objects are here rather than named by
                      ;; hand, because a contract nothing runs by default is a
                      ;; contract nobody runs. They cost about twenty minutes
                      ;; between them on this machine (see their
                      ;; `:verification :largest-executed-record-bytes` and the
                      ;; elapsed times in the receipt) -- the price of
                      ;; executing an AEAD in a ClojureScript interpreter, and
                      ;; stated rather than hidden.
                      ["cid-v1-admit-v1.edn" "unixfs-file-admit-v1.edn"
                       "value-runtime-cas-verify-v1.edn"
                       "value-handle-arena-v1.edn"
                       "aes128-gcm-v1.edn" "hkdf-sha256-v1.edn"
                       "tls13-record-v1.edn"]))
        results (mapv #(run-contract root-dir %) paths)]
    (doseq [r results]
      (println (str "CONTRACT\t" (:contract r)
                    "\tvectors=" (:vectors r)
                    (when (:steps r) (str "\tsteps=" (:steps r)))
                    "\ttraps=" (:traps r)
                    (when (:memory-assertions r)
                      (str "\tmemory=" (:memory-assertions r)))
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
