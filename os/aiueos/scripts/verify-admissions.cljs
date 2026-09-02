#!/usr/bin/env nbb
;; Runs the admission objects against their contracts WITHOUT a JVM.
;;
;;   npm run verify-admissions -- [contract.edn ...]
;;
;; which is
;;
;;   node --stack-size=4000 "$(command -v nbb)" \
;;     --classpath "$(clojure -Spath -M:verify-admissions)" \
;;     os/aiueos/scripts/verify-admissions.cljs [contract.edn ...]
;;
;; `-M:verify-admissions`, NOT `-M:test`. This line said `-M:test` from the day
;; it was written and `package.json` said `-M:verify-admissions`, and the two
;; are different compilers on purpose (see the alias comment in `deps.edn`).
;; Measured 2026-09-02: with the `:test` closure -- amu 6889fa73, which carries
;; kotoba-sema 244765d4 -- EVERY contract fails at
;;
;;     FAILED: source reader rejected input
;;        {:phase :read}
;;
;; before a single vector runs, because that kotoba-sema predates the
;; linked-source printer fix. The same contract passes on the classpath this
;; file's own receipt names. A run following this comment could not check
;; anything, and the comment was the only thing wrong.
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

;; --- the Qwen3.5 forward pass, first tranche (ADR-0147) -------------------
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
;; --- device-worker client --------------------------------------------------
;; `device-worker-canonical` writes the text the device SIGNS. Its contract is
;; the only one here whose expected bytes come from a second implementation
;; rather than from a published test vector: the v2 rows are printed by
;; `os/aiueos/tests/device_result_v2_model.c --dump-canonical`, which is the C
;; this object replaces, and the v3 rows are the worked example in
;; `docs/device-worker-v3.md` (network-awai/cloud-murakumo-api), which is the
;; server that will verify what the device signs.
;;
;; The expected text is a VECTOR OF LINES rather than one string with escapes.
;; A canonical form is exactly newline-joined fields, so lines are its shape,
;; and an EDN document written through a shell heredoc corrupts `\"` -- which
;; here would silently move a field boundary.

(defn- ascii-bytes [s] (mapv #(.charCodeAt % 0) (seq s)))

(defn- be64-bytes [value]
  ;; Eight bytes, most significant first. `Math.floor` division rather than
  ;; shifting: JavaScript's bitwise operators are 32-bit and a cycle counter or
  ;; a 16-digit job-id is not.
  (mapv (fn [scale] (bit-and (js/Math.floor (/ value scale)) 0xff))
        [72057594037927936 281474976710656 1099511627776 4294967296
         16777216 65536 256 1]))

(defn- canonical-text [contract v]
  (let [lines (:expect-lines v)]
    (when (and (seq lines) (:expected v) (pos? (:expected v))
               (not= (:expected v) (count (str/join "\n" lines))))
      (fail! "REFUSING TO REPORT A PASS: the contract's stated length and its stated text disagree"
             {:contract (:format contract) :vector (:name v)
              :stated-length (:expected v)
              :text-length (count (str/join "\n" lines))}))
    (str/join "\n" lines)))

(defmethod prepare :aiueos.device-worker-canonical/v1 [contract v]
  (let [{:keys [base image-bytes ctx-offset out-offset]}
        (get-in contract [:verification :memory])
        ctx (:ctx contract)
        fixture (:fixture contract)
        field (fn [k] (get v k (get fixture k)))
        ;; `:operation-code` / `:stop-reason-code` exist so a refusal vector can
        ;; put a byte the protocol does not define into the context WITHOUT that
        ;; byte having to be given a name in the contract's own vocabulary. A
        ;; name for `9` would be a name for something the protocol has not got.
        operation (or (:operation-code v)
                      (get (:operations contract) (field :operation)))
        stop (or (:stop-reason-code v)
                 (get (:stop-reasons contract) (field :stop-reason)))
        _ (when-not (and operation stop)
            (fail! "REFUSING TO REPORT A PASS: this vector names an operation or stop reason the contract does not define"
                   {:vector (:name v) :operation (field :operation)
                    :stop-reason (field :stop-reason)}))
        text (canonical-text contract v)
        image (as-> (vec (repeat image-bytes 0)) img
                (write-at img (+ ctx-offset (get-in ctx [:protocol :offset]))
                          [(field :protocol)])
                (write-at img (+ ctx-offset (get-in ctx [:operation :offset]))
                          [operation])
                (write-at img (+ ctx-offset (get-in ctx [:stop-reason :offset]))
                          [stop])
                (reduce (fn [m [k offset]]
                          (write-at m (+ ctx-offset offset)
                                    (be64-bytes (or (field k) 0))))
                        img (:numbers ctx))
                (reduce (fn [m [k [offset width]]]
                          (let [s (or (field k) "")]
                            (when (and (seq s) (not= width (count s)))
                              (fail! "REFUSING TO REPORT A PASS: a fixed-width context field was given the wrong number of characters"
                                     {:vector (:name v) :field k
                                      :declared width :given (count s)}))
                            (write-at m (+ ctx-offset offset) (ascii-bytes s))))
                        img (:text ctx)))]
    {:entry 'aiueos-device-worker-canonical
     :base base
     :image image
     :args [(+ base ctx-offset)
            (or (:ctx-len v) (:bytes ctx))
            (+ base out-offset)
            (or (:out-cap v) (get-in contract [:out :bytes]))]
     :expect-memory
     (cond-> []
       (seq text)
       (conj {:label :canonical :offset out-offset :bytes (ascii-bytes text)}))}))

;; --- the streaming SHA-256 family ------------------------------------------
;; Three objects over one shared module (`aiueos.lib.sha256-stream`). All three
;; write their answer into the CALLER'S state region rather than returning it,
;; so `:expect-memory` is not decoration here -- the return value of a
;; successful `sha256-region` call is the constant 0, and a vector that checked
;; only that would pass against an object that hashed nothing.

(defn- message-bytes
  "The message a vector describes, in either of the two spellings a SHA-256
  vector needs: a published ASCII string, or N copies of one byte. A vector
  that gives neither is a vector with no input, and saying so here is cheaper
  than a digest mismatch that looks like an arithmetic bug."
  [contract v]
  (cond
    (contains? v :message-ascii) (ascii-bytes (:message-ascii v))
    (contains? v :message-hex) (hex-bytes (:message-hex v))
    (contains? v :message-bytes) (vec (repeat (:message-bytes v) (or (:fill-byte v) 0)))
    :else (fail! "REFUSING TO REPORT A PASS: this vector declares no message"
                 {:contract (:format contract) :vector (:name v)})))

(defn- digest-expectation [v offset]
  (when (:digest-hex v)
    [{:label :digest :offset offset :bytes (hex-bytes (:digest-hex v))}]))

(defmethod prepare :aiueos.sha256-region/v1 [contract v]
  (let [{:keys [base image-bytes st-offset src-offset]}
        (get-in contract [:verification :memory])
        msg (message-bytes contract v)]
    {:entry 'aiueos-sha256-region
     :base base
     :image (write-at (vec (repeat image-bytes 0)) src-offset msg)
     :args [(+ base st-offset)
            (if (contains? v :st-len) (:st-len v) (get-in contract [:st :bytes]))
            (+ base src-offset)
            (if (contains? v :src-len) (:src-len v) (count msg))]
     :expect-memory (digest-expectation v (+ st-offset 424))}))

(defn- be32-bytes [value]
  (mapv (fn [scale] (bit-and (js/Math.floor (/ value scale)) 0xff))
        [16777216 65536 256 1]))

(defn- digest-tokens
  "`:tokens` is either a literal vector of ids or one of two named ranges. The
  ranges are named rather than written out because a 64-element literal in a
  contract is 64 chances to transcribe one wrong, and the digest they are
  checked against was computed from the range."
  [contract v]
  (let [t (:tokens v)]
    (cond
      (vector? t) t
      (= t :range-100-163) (vec (range 100 164))
      (= t :range-1-20) (vec (range 1 21))
      :else (fail! "REFUSING TO REPORT A PASS: this vector names a token set this runner does not know"
                   {:contract (:format contract) :vector (:name v) :tokens t}))))

(defmethod prepare :aiueos.device-worker-digest/v1 [contract v]
  (let [{:keys [base image-bytes st-offset tok-offset]}
        (get-in contract [:verification :memory])
        tokens (digest-tokens contract v)
        max-tokens-offset (get-in contract [:st :caller-writes :max-tokens])]
    {:entry 'aiueos-device-worker-digest
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at tok-offset (vec (mapcat be32-bytes tokens)))
                ;; The MODE, written by the caller and validated by the object.
                (write-at (+ st-offset max-tokens-offset)
                          (be32-bytes (or (:max-tokens v) 0))))
     :args [(+ base st-offset)
            (if (contains? v :st-len) (:st-len v) (get-in contract [:st :bytes]))
            (+ base tok-offset)
            (if (contains? v :tok-count) (:tok-count v) (count tokens))]
     :expect-memory (digest-expectation v (+ st-offset 424))}))

(defmethod prepare-steps :aiueos.sha256-stream/v1 [contract v]
  (let [{:keys [base image-bytes st-offset src-offset]}
        (get-in contract [:verification :memory])
        msg (message-bytes contract v)]
    {:entry 'aiueos-sha256-stream
     :base base
     :image (write-at (vec (repeat image-bytes 0)) src-offset msg)
     ;; `[mode src-delta src-len]`, with an optional fourth element overriding
     ;; `st-len` so the length refusal can be a step rather than a vector of
     ;; its own. `src-delta` is an offset INTO THE MESSAGE, which is what makes
     ;; a block call point at the next 64 bytes of the same image the previous
     ;; call read.
     :args-of (fn [[mode src-delta src-len st-len]]
                [mode (+ base st-offset)
                 (or st-len (get-in contract [:st :bytes]))
                 (+ base src-offset (or src-delta 0))
                 (or src-len 0)])}))
;; nic: the RTL8125 driver objects. ONE builder for the whole family, unlike
;; every method above it, because these six take the same KIND of argument --
;; an address of a region in the image, a length, and some literals -- and
;; differ only in which regions and how many. So the contract names its own
;; entry and writes its arguments as a template, and this resolves the region
;; keywords against the base.
;;
;;   :args [:mmio 4096 :out 16]        -> [base+mmio-offset 4096 base+out-offset 16]
;;   :args [[:tx 16] 32 ...]           -> [base+tx-offset+16 32 ...]
;;   :args [0 32 ...]                  -> passed through
;;
;; The `[region offset]` form exists so a vector can name a MISALIGNED address
;; without hard-coding a number that silently stops meaning what it meant when
;; the layout moves.
;;
;; ⚠ ONLY TWO OF THE SIX OBJECTS CAN BE RUN HERE, and it is not a gap in this
;; builder. `kotoba.kir`'s interpreter refuses `kernel-fence-load`,
;; `kernel-fence-store` and `kernel-rdtsc` with `:kernel-privileged-unavailable`
;; -- deliberately, and its comment says why: a barrier orders memory operations
;; against a machine this interpreter is not running on, and an oracle that
;; executes one operation at a time would be answering "the barrier worked" from
;; something that never had the problem. `rtl8125-ring-build`, `-program`,
;; `-tx-submit` and `-rx-poll` all carry fences, so the oracle cannot execute
;; them AT ALL -- it traps rather than returning a wrong answer, which is the
;; right refusal and still leaves those four with no host-side evidence. Their
;; evidence is `aiueos_rtl8125_kotoba_selftest` running the EMITTED MACHINE CODE
;; against a software model under QEMU, which is a stronger claim about the same
;; program and a weaker one about portability.
(defmethod prepare :aiueos.rtl8125/v1 [contract v]
  (let [{:keys [base image-bytes regions]} (get-in contract [:verification :memory])
        entry (:entry contract)
        _ (when-not (symbol? entry)
            (fail! "REFUSING TO REPORT A PASS: this contract names no entry symbol"
                   {:contract (:format contract)}))
        region-at (fn [k]
                    (or (get regions k)
                        (fail! "REFUSING TO REPORT A PASS: a vector names a region the contract does not lay out"
                               {:vector (:name v) :region k})))
        image (reduce (fn [img {:keys [offset bytes hex]}]
                        (write-at img offset (if hex (hex-bytes hex) (vec bytes))))
                      (vec (repeat image-bytes 0))
                      (:seed v))
        resolve-arg (fn [a]
                      (cond (keyword? a) (+ base (region-at a))
                            (vector? a) (+ base (region-at (first a)) (second a))
                            :else a))]
    {:entry entry
     :base base
     :image image
     :args (mapv resolve-arg (:args v))
     :expect-memory (:expect-memory v)}))

;; --- the tokenizer family --------------------------------------------------
;;
;; Three objects, one image and one builder, because the three take the SAME
;; four arguments -- a model window and a workspace -- and differ only in which
;; header slots they read. The model is a seven-token mini vocabulary written
;; at image offset 0; the workspace follows at `:work-offset` and every other
;; offset in the map is relative to IT, which is what the objects themselves
;; mean by a workspace offset.
;;
;; `:prelude-args` is the contract's own base and lengths, never the vector's
;; overrides: a vector that hands the object a 16 MiB window in order to be
;; refused for it must not make the index build refuse first.

(defn- u32-bytes [v]
  [(bit-and v 0xff) (bit-and (bit-shift-right v 8) 0xff)
   (bit-and (bit-shift-right v 16) 0xff) (bit-and (bit-shift-right v 24) 0xff)])

(defn- tokenizer-prepare [contract v entry]
  (let [m (get-in contract [:verification :memory])
        f (:fixture contract)
        base (:base m)
        wo (:work-offset m)
        ;; A vector may name one of the fixture's ALTERNATE models by keyword
        ;; rather than repeat 232 characters of hex to change three bytes.
        model-hex (let [h (or (:model-hex v) (:model-hex f))]
                    (if (keyword? h) (get f h) h))
        model (hex-bytes model-hex)
        input (cond (:input-fill v)
                    (vec (repeat (:count (:input-fill v)) (:byte (:input-fill v))))
                    (:input-hex v) (hex-bytes (:input-hex v))
                    :else [])
        ids (vec (mapcat u32-bytes (:ids v)))
        header (merge {4 (or (:vocab-count v) (:vocab-count f))
                       8 (if (some? (:merge-count v)) (:merge-count v) (:merge-count f))
                       12 (or (:tokens-at v) (:tokens-at f))
                       16 (or (:merges-at v) (:merges-at f))
                       44 0
                       48 (:input-offset m)
                       52 (count input)
                       56 (:output-offset m)
                       60 (or (:output-capacity v) (:output-capacity m))
                       68 (:scratch-offset m)
                       72 (or (:scratch-bytes v) (:scratch-bytes m))
                       76 (:ids-offset m)
                       80 (if (some? (:id-count v)) (:id-count v) (count (:ids v)))
                       84 (:text-offset m)
                       88 (or (:text-capacity v) (:text-capacity m))}
                      (:header v))
        image (reduce (fn [img [off val]] (write-u32 img (+ wo off) val))
                      (-> (vec (repeat (:image-bytes m) 0))
                          (write-at 0 model)
                          (write-at (+ wo (:input-offset m)) input)
                          (write-at (+ wo (:ids-offset m)) ids))
                      header)]
    {:entry entry
     :base base
     :image image
     :args [(if (some? (:model-base v)) (:model-base v) base)
            (if (some? (:model-len v)) (:model-len v) (count model))
            (if (some? (:work-base v)) (:work-base v) (+ base wo))
            (if (some? (:work-len v)) (:work-len v) (:work-bytes m))]
     :prelude-args (when-not (false? (:prelude? v))
                     [base (count model) (+ base wo) (:work-bytes m)])
     :expect-memory
     (cond-> []
       (:expect-ids v)
       (conj {:label :ids :offset (+ wo (:output-offset m))
              :bytes (vec (mapcat u32-bytes (:expect-ids v)))})
       (some? (:expect-id-count v))
       (conj {:label :id-count :offset (+ wo 64) :bytes (u32-bytes (:expect-id-count v))})
       (:expect-text-hex v)
       (conj {:label :text :offset (+ wo (:text-offset m))
              :bytes (hex-bytes (:expect-text-hex v))})
       (some? (:expect-text-length v))
       (conj {:label :text-length :offset (+ wo 92)
              :bytes (u32-bytes (:expect-text-length v))})
       (:expect-header v)
       (into (map (fn [[off val]]
                    {:label (keyword (str "header-" off)) :offset (+ wo off)
                     :bytes (u32-bytes val)})
                  (:expect-header v))))}))

(defmethod prepare :aiueos.qwen35-vocab-index-build/v1 [contract v]
  (tokenizer-prepare contract v 'aiueos-qwen35-vocab-index-build))

(defmethod prepare :aiueos.qwen35-tokenize/v1 [contract v]
  (tokenizer-prepare contract v 'aiueos-qwen35-tokenize))

(defmethod prepare :aiueos.qwen35-detokenize/v1 [contract v]
  (tokenizer-prepare contract v 'aiueos-qwen35-detokenize))

;; --- the Qwen3.5 forward pass, second tranche (ADR-0148) -------------------
;;
;; The scalar functions between the matvecs. Expectations come from the same
;; independent ClojureScript re-derivation over `Float32Array` the first
;; tranche used, and the same caveat applies: it proves the algorithm against a
;; second reading of the C and says nothing about what amu emitted. The
;; evidence that covers the backend is the QEMU self-test `QWEN-PARITY`,
;; which caught an argument-order mistake in its own harness that this
;; interpreter could not have.

(defmethod prepare :aiueos.qwen35-activation/v1 [contract v]
  (let [{:keys [base image-bytes a-offset b-offset]}
        (get-in contract [:verification :memory])
        a (hex-bytes (or (:a-hex v) ""))
        b (hex-bytes (or (:b-hex v) ""))]
    {:entry 'aiueos-qwen35-activation
     :base base
     :image (-> (vec (repeat image-bytes 0))
                (write-at a-offset a)
                (write-at b-offset b))
     :args [(:mode v)
            (if (some? (:a-base-override v)) (:a-base-override v) (+ base a-offset))
            (if (some? (:b-base-override v)) (:b-base-override v) (+ base b-offset))
            (:count v)
            0]
     :expect-memory
     (cond-> []
       (:expect-a-hex v)
       (conj {:label :a :offset a-offset :bytes (hex-bytes (:expect-a-hex v))}))}))

;; Three modes with three different argument meanings, which is why this is a
;; `case` rather than one shape: mode 0 reads `b` as a weights POINTER and
;; `c` as an element count, modes 1 and 2 read them as a head count and a
;; width. Getting that wrong is not a crash -- it normalises the wrong vector
;; into the wrong place and every bound still holds.
(defmethod prepare :aiueos.qwen35-norm/v1 [contract v]
  (let [{:keys [base image-bytes a-offset b-offset out-offset]}
        (get-in contract [:verification :memory])
        a (hex-bytes (or (:a-hex v) ""))
        b (hex-bytes (or (:b-hex v) ""))
        image (-> (vec (repeat image-bytes 0))
                  (write-at a-offset a)
                  (write-at b-offset b))
        base-a (if (some? (:a-base-override v)) (:a-base-override v) (+ base a-offset))
        base-b (if (some? (:b-base-override v)) (:b-base-override v) (+ base b-offset))
        base-d (if (some? (:d-base-override v)) (:d-base-override v) (+ base b-offset))]
    {:entry 'aiueos-qwen35-norm
     :base base
     :image image
     :args (case (long (:mode v))
             0 [0 base-a base-b (:count v) (+ base out-offset)]
             1 [1 base-a (:heads v) (:width v) 0]
             2 [2 base-a (:heads v) (:width v) base-d]
             ;; a mode the object refuses; the remaining words are still real
             ;; regions so the refusal is provoked by the MODE and nothing else
             [(:mode v) base-a (or (:heads v) 1) (or (:width v) (:count v) 8) 0])
     :expect-memory
     (cond-> []
       (:expect-out-hex v)
       (conj {:label :out :offset out-offset :bytes (hex-bytes (:expect-out-hex v))})
       (:expect-a-hex v)
       (conj {:label :a :offset a-offset :bytes (hex-bytes (:expect-a-hex v))}))}))


;; --- the Qwen3.5 forward pass, third tranche (ADR-0142) --------------------
;;
;; `full_attention`'s de-interleave / fused softmax / position-zero reduction,
;; and `recurrent_step`'s gated DeltaNet update. Both take
;; `[arena arena-bytes plan plan-bytes]`, so the vector describes a PLAN and
;; this builder lays 96 bytes out from the contract's own memory map. A vector
;; may override any slot by byte offset (`:plan-override {8 0}`), which is how
;; the refusals are reached without a second argument shape.
;;
;; Expectations come from the same independent ClojureScript re-derivation over
;; `Float32Array` / `Float64Array` the first two tranches used, and the same
;; caveat applies: it proves the algorithm against a second reading of the C
;; and says nothing about what amu emitted. The evidence that covers the
;; backend is the QEMU self-test `QWEN-PARITY`.

(defn- u64-bytes [v]
  (into (u32-bytes (mod v 4294967296))
        (u32-bytes (js/Math.floor (/ v 4294967296)))))

(defn- plan-bytes
  "96 bytes from `slot -> value`, u32 at 0 and 4 and u64 from 8 on -- the
  layout both objects read. A slot absent from the map is zero, which is what
  every reserved slot must be."
  [slots]
  (reduce (fn [image [slot value]]
            (write-at image slot
                      (if (< slot 8) (u32-bytes value) (u64-bytes value))))
          (vec (repeat 96 0))
          (sort-by key slots)))

(defn- arena-plan-prepare [contract v entry slots]
  (let [m (get-in contract [:verification :memory])
        base (:base m)
        image (reduce (fn [img [region hex]]
                        (write-at img (+ (:arena-offset m) (get m region)) (hex-bytes hex)))
                      (vec (repeat (:image-bytes m) 0))
                      (:regions (meta slots) (:regions v)))
        slots (merge slots (into {} (map (fn [[k x]] [(long k) (long x)]))
                                 (:plan-override v)))]
    {:entry entry
     :base base
     :image (write-at image (:plan-offset m) (plan-bytes slots))
     :args [(if (some? (:arena-override v)) (:arena-override v) (+ base (:arena-offset m)))
            (:arena-bytes m)
            (if (some? (:plan-base-override v)) (:plan-base-override v) (+ base (:plan-offset m)))
            (if (some? (:plan-length v)) (:plan-length v) 96)]
     :expect-memory
     (reduce (fn [acc [label region hex]]
               (if hex
                 (conj acc {:label label
                            :offset (+ (:arena-offset m) (get m region))
                            :bytes (hex-bytes hex)})
                 acc))
             []
             (:expectations (meta slots)))}))

(defmethod prepare :aiueos.qwen35-recurrent-step/v1 [contract v]
  (let [m (get-in contract [:verification :memory])]
    (arena-plan-prepare
     contract v 'aiueos-qwen35-recurrent-step
     (with-meta
       {0 0 4 0
        8 (:dim v)
        16 (:state m) 24 (:key m) 32 (:query m) 40 (:value m)
        48 (:correction m) 56 (:output m)
        64 (:decay-bits v) 72 (:beta-bits v)
        80 0 88 0}
       {:regions [[:state (:state-hex v)] [:key (:key-hex v)]
                  [:query (:query-hex v)] [:value (:value-hex v)]]
        :expectations [[:state :state (:expect-state-hex v)]
                       [:output :output (:expect-output-hex v)]]}))))

(defmethod prepare :aiueos.qwen35-attention/v1 [contract v]
  (let [m (get-in contract [:verification :memory])]
    (arena-plan-prepare
     contract v 'aiueos-qwen35-attention
     (with-meta
       {0 (:mode v) 4 0
        8 (:heads v) 16 256
        24 (if (= 1 (:mode v)) 0 (:out m))
        32 (:qg m)
        40 (case (long (:mode v)) 0 0 1 (:key m) 2 (:value m))
        48 (if (= 1 (:mode v)) (:value m) 0)
        56 (if (= 1 (:mode v)) (:query m) 0)
        64 (if (= 1 (:mode v)) 1024 0)
        72 (if (= 0 (:mode v)) 0 (:group v))
        80 (if (= 1 (:mode v)) (:position v) 0)
        88 (if (= 1 (:mode v)) (:scratch m) 0)}
       {:regions [[:qg (:qg-hex v)] [:query (:query-hex v)]
                  [:key (:key-hex v)] [:value (:value-hex v)]]
        :expectations [[:qg :qg (:expect-qg-hex v)]
                       [:out :out (:expect-out-hex v)]]}))))

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

;; A PRELUDE is another object's call, run against the same page before the
;; vector's own. It exists because `aiueos-qwen35-tokenize` and
;; `aiueos-qwen35-detokenize` read tables a THIRD object writes, and a kernel
;; object cannot call another (ADR-0030): without this the only way to give
;; those two a workspace would be to transcribe the tables into the contract as
;; hex, which is exactly the "a constant rather than a derivation" failure
;; these contracts keep naming. A vector opts out with `:prelude? false`, which
;; is how the "no index in the workspace" refusal is reached.
;;
;; The prelude runs with the contract's OWN base and lengths, never the
;; vector's argument overrides -- a vector that hands the object a 16 MiB
;; window to be refused for must not make the prelude refuse first.
(defn- run-prelude! [contract prelude base page fuel prelude-args label]
  (when (and prelude prelude-args)
    (let [actual (execute-once (:kir prelude) (:entry prelude) prelude-args base page fuel)]
      (refuse-on-trap! contract [label :prelude] actual)
      (when-not (= (:expected (:declared prelude) 0) actual)
        (fail! "REFUSING TO REPORT A PASS: the prelude refused, so the vector ran against a workspace the contract does not describe"
               {:contract (:format contract) :vector label
                :entry (:entry prelude) :actual actual})))))

(defn- run-single
  "One call per vector, each against a fresh image."
  [contract kir entry fuel started prelude]
  (let [{:keys [floors]} (:verification contract)
        memory-assertions (volatile! 0)
        observed
        (doall
         (for [v (:vectors contract)]
           (let [{:keys [base image args expect-memory prelude-args]} (prepare contract v)
                 page (volatile! image)
                 _ (run-prelude! contract prelude base page fuel prelude-args (:name v))
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
           (let [{:keys [base image args prelude-args]} (prepare contract t)
                 page (volatile! image)
                 _ (run-prelude! contract prelude base page fuel prelude-args (:name t))
                 actual (execute-once kir entry args base page fuel)]
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
        counted (volatile! 0)
        memory-assertions (volatile! 0)]
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
            ;; A STEP MAY ASSERT BYTES, not only a reason code. `run-single`
            ;; has had this since ADR-0132 and this runner did not, which was
            ;; the same gap that comment names: a transforming object whose
            ;; success value is a constant 0 is checked over the part that does
            ;; not matter. The whole point of `aiueos-sha256-stream` is that
            ;; the LAST step leaves a digest behind, and nothing could say so.
            (vswap! memory-assertions +
                    (check-memory! contract [(:name v) index]
                                   (mapv (fn [e]
                                           (cond-> e
                                             (:hex e) (assoc :bytes (hex-bytes (:hex e)))))
                                         (:expect-memory step))
                                   page))
            (vswap! counted inc)))))
    (when (< (count (:vectors contract)) (or (:minimum-vectors floors) 1))
      (fail! "fewer vectors ran than the contract's floor"
             {:ran (count (:vectors contract)) :floor (:minimum-vectors floors)}))
    (when (< @counted (or (:minimum-steps floors) 1))
      (fail! "fewer steps ran than the contract's floor"
             {:ran @counted :floor (:minimum-steps floors)}))
    (when (< @memory-assertions (or (:minimum-memory-assertions floors) 0))
      (fail! "fewer memory assertions ran than the contract's floor"
             {:ran @memory-assertions
              :floor (:minimum-memory-assertions floors)}))
    {:contract (:format contract)
     :vectors (count (:vectors contract)) :steps @counted :traps 0
     :memory-assertions @memory-assertions
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
                        (prepare contract (first (:vectors contract)))))
        prelude (when-let [declared (:prelude contract)]
                  (let [pkir (compile-graph root-dir (:graph declared))]
                    (when-not (contains? (set (:exports pkir)) (:entry declared))
                      (fail! "the prelude project does not export the entry the contract names"
                             {:entry (:entry declared) :exports (vec (:exports pkir))}))
                    {:kir pkir :entry (:entry declared) :declared declared}))]
    (when-not (contains? exports entry)
      (fail! "the linked project does not export the entry this runner calls"
             {:entry entry :exports (vec exports)}))
    (if stepped?
      (run-stepped contract kir entry fuel started)
      (run-single contract kir entry fuel started prelude))))

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
                      ;; The three streaming-SHA-256 contracts are here for
                      ;; the same reason. `sha256-region-v1` carries a
                      ;; 1,000-byte vector and `sha256-stream-v1` drives the
                      ;; same message a block at a time; between them that is
                      ;; the only place the block LOOP runs more than twice
                      ;; off-target, and it is minutes rather than seconds.
                      ;; Anything larger is the QEMU self-test's job, not a
                      ;; ClojureScript interpreter's.
                      ;; The two RTL8125 contracts are cheap (under three
                      ;; seconds between them) and are here for the same reason
                      ;; the TLS pair is: a contract nothing runs by default is
                      ;; a contract nobody runs. The other four driver objects
                      ;; have no contract here at all, and that is stated in the
                      ;; builder's comment rather than left as an absence --
                      ;; the oracle refuses the fences they carry.
                      ["cid-v1-admit-v1.edn" "unixfs-file-admit-v1.edn"
                       "value-runtime-cas-verify-v1.edn"
                       "value-handle-arena-v1.edn"
                       "rtl8125-identify-v1.edn" "rtl8125-link-up-v1.edn"
                       "aes128-gcm-v1.edn" "hkdf-sha256-v1.edn"
                       "tls13-record-v1.edn"
                       "sha256-region-v1.edn" "sha256-stream-v1.edn"
                       "device-worker-digest-v1.edn"
                       ;; The tokenizer family. Cheap by comparison -- about
                       ;; forty seconds between them, because the vocabulary
                       ;; they run against is seven tokens rather than an AEAD
                       ;; over a 12 KiB record. Two of the three declare a
                       ;; `:prelude`, so each of their vectors runs the index
                       ;; build first against the same page.
                       "qwen35-vocab-index-build-v1.edn"
                       "qwen35-tokenize-v1.edn"
                       "qwen35-detokenize-v1.edn"]))
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
