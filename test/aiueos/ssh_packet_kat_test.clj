;; `ssh-packet.kotoba` against `ssh.transport` — the org-ietf-ssh framing
;; fixture vectors, driven through the KIR interpreter.
;;
;; Tranche T1 of the SSH pure port (os/aiueos/docs/ssh-pure-port-plan.md §4
;; T1). The object is the RFC 4253 §6 framing decision — the admission
;; `packet-payload` performs and the C pump's `ssh_unwrap` (kernel/pci.c:2281)
;; mirrors — as a self-contained kernel object. The fixture vectors are the
;; packet section of kotoba-lang/org-ietf-ssh's test/ssh/transport_test.cljs
;; (pinned at 85e5d95c88880ced721c24cd263ce92f48662989): payload [0..19]
;; framed at block 8, the 8-alignment check, the round-trip, the 4-byte
;; refusal — completed at the boundaries the fixture implies (the
;; minimum-padding frame, the empty payload, one mutation per refusal clause).
;;
;; Why the KIR interpreter and not the machine: the object compiles to an
;; x86-64 ET_REL kernel object and this workstation is aarch64, so nothing
;; here executes the emitted bytes. `kotoba.kir` reproduces the three window
;; checks the backends emit, which makes it an oracle for the bounds and not
;; only for the arithmetic — a read past the declared segment traps
;; `:kernel-memory-fault` here and would trap UD2 on the machine.
;;
;; Why the object is not linked into the boot image: the object's own
;; `aiueos-` export has no row in kotoba-native's `kernel-object-entries`
;; allowlist at either aiueos compiler pin, so a packaged `.o` would either
;; carry the colliding `kotoba_aiueos_probe` symbol (the silent-default rule
;; at the object pin, 9cf3a0a — exactly what amu#626 / aiueos ADR-0054
;; removed) or refuse outright (the allowlist-checking rule at the kernel
;; pin, 13d2f5df). Wiring lands with the QEMU-gate tranche, with the
;; kotoba-native row. See also the namespace comment in `ssh-packet.kotoba`
;; and the commit message.

(ns aiueos.ssh-packet-kat-test
  (:require [clojure.java.io :as io]
            [clojure.test :refer [deftest is testing]]
            [kotoba.compiler.core :as compiler]
            [kotoba.kir :as ir]
            [ssh.transport :as t]))

(def ^:private source-file
  (io/file "os" "aiueos" "kotoba" "ssh-packet.kotoba"))

(defn- source-available?
  "Fails rather than skips, for the reason
  `aiueos.tcp-seq-acceptable-parity-test` gives: a `when`-guard here reports
  green on a checkout without the object, which is a skip wearing a pass."
  []
  (let [present? (.exists source-file)]
    (is present? (str "kotoba object not found at " source-file))
    present?))

(def ^:private kir
  (delay (:kir (compiler/compile-source (slurp source-file) :wasm32-kotoba-v1 {}))))

;; --- the KIR memory model ---------------------------------------------------
;;
;; The object sees one caller-owned region per invocation. Base 4096 keeps
;; the region clear of the null page; the layout mirrors the object's own
;; baked-KAT scratch so the same offsets feed every vector:
;;
;;   4096 +   0   payload workspace, 32 bytes
;;   4096 +  32   frame output, 64 bytes
;;   4096 +  96   admit output, 64 bytes
;;   4096 + 160   reframe output, 64 bytes
;;   4096 + 224   refusal workspace, 64 bytes

(def ^:private base 4096)
(def ^:private region 512)

(def ^:private slot-payload 0)
(def ^:private slot-frame 32)
(def ^:private slot-admit 96)
(def ^:private slot-reframe 160)
(def ^:private slot-refuse 224)

(defn- fresh-image []
  (volatile! (vec (repeat (+ base region) 0))))

(defn- call-object
  "One invocation of the entry: seg at slot `seg-slot`, out at `out-slot`.
  Returns [exit image-after]."
  [image seg-slot seg-len out-slot out-len mode]
  (let [exit (ir/execute @kir 'ssh-packet
                         [(+ base seg-slot) seg-len (+ base out-slot) out-len mode]
                         {:fuel 1048576 :memory {:base base :bytes image}})]
    [exit image]))

(defn- call-frame
  "Mode 0 over `payload`, written into the payload workspace first."
  [image payload]
  (doseq [[i b] (map-indexed vector payload)]
    (vswap! image assoc (+ slot-payload i) b))
  (call-object image slot-payload (count payload) slot-frame 64 0))

(defn- call-admit [image seg-slot seg-len out-slot]
  (call-object image seg-slot seg-len out-slot 64 1))

(defn- call-reframe [image seg-slot seg-len out-slot]
  (call-object image seg-slot seg-len out-slot 64 2))

(defn- read-region [image slot n]
  (mapv @image (range slot (+ slot n))))

(defn- be32
  "The uint32 out[0..3] of an admit (or reframe) output carries the payload
  length, big-endian."
  [four-bytes]
  (+ (* 16777216 (nth four-bytes 0))
     (* 65536 (nth four-bytes 1))
     (* 256 (nth four-bytes 2))
     (nth four-bytes 3)))

(defn- write-into
  "Put `bytes` at slot `slot` of the image (a helper, not part of the object)."
  [image slot bytes]
  (doseq [[i b] (map-indexed vector bytes)]
    (vswap! image assoc (+ base slot i) b))
  image)

;; --- the reference ----------------------------------------------------------
;; `ssh.transport/packet` and `packet-payload` run here directly. They are the
;; definitions the object ports, so agreement with them IS the port claim —
;; and they come from the same repository revision the plan names.

(def ^:private reference-packet t/packet)
(def ^:private reference-payload t/packet-payload)

;; --- 1. the fixture vector: frame -> admit -> reframe -----------------------

(def ^:private fixture-payload (vec (range 20)))

(deftest fixture-vector-round-trips
  (when (source-available?)
    (testing "payload [0..19], the transport_test.cljs packet fixture"
      (let [image (fresh-image)
            [fexit _] (call-frame image fixture-payload)
            framed (read-region image slot-frame 32)]
        (is (zero? fexit) "mode 0 returns zero on the fixture payload")
        (is (= (reference-packet fixture-payload) framed)
            "the framed bytes are the hand-computed packet, byte for byte")
        (is (zero? (mod (count framed) 8))
            "the frame is 8-aligned — the fixture's packet-aligned check")
        (is (= fixture-payload (reference-payload framed))
            "the reference admits the same payload from the same bytes"))
      ;; admit recovers the payload; the length convention puts it in the
      ;; buffer, out[0..3], because a zero-length payload is legal and a
      ;; 0 return has to keep meaning success.
      (let [image (fresh-image)]
        (call-frame image fixture-payload)
        (let [[aexit aimage] (call-admit image slot-frame 32 slot-admit)
              payload-len (be32 (take 4 (read-region aimage slot-admit 4)))]
          (is (zero? aexit) "mode 1 returns zero on a well-framed packet")
          (is (= 20 payload-len) "the admitted payload length is 20")
          (is (= fixture-payload
                 (drop 4 (read-region aimage slot-admit (+ 4 payload-len))))
              "the admitted payload is the source bytes")))
      ;; reframe over the frame is the frame byte for byte.
      (let [image (fresh-image)]
        (call-frame image fixture-payload)
        (let [[rexit rimage] (call-reframe image slot-frame 32 slot-reframe)]
          (is (zero? rexit) "mode 2 returns zero on a well-framed packet")
          (is (= (read-region rimage slot-frame 32)
                 (read-region rimage slot-reframe 32))
              "the reframe is the frame, byte for byte"))))))

;; --- 2. the boundary vectors the fixture implies ----------------------------

(deftest minimum-padding-frame-round-trips
  (when (source-available?)
    ;; payload 7: unpadded 12, 12 mod 8 = 4, pad = 4 exactly — the smallest
    ;; padding RFC 4253 §6 admits, and the boundary of the (< p 4) correction
    ;; in the authority's `pad-length`.
    (let [payload (vec (range 7))
          image (fresh-image)]
      (call-frame image payload)
      (let [framed (read-region image slot-frame 16)]
        (is (= 4 (nth framed 4)) "padding_length is exactly 4 at the floor")
        (is (= (reference-packet payload) framed)
            "the floor frame matches the reference"))
      (let [[aexit aimage] (call-admit image slot-frame 16 slot-admit)
            payload-len (be32 (take 4 (read-region aimage slot-admit 4)))]
        (is (zero? aexit) "the floor frame admits")
        (is (= 7 payload-len) "the floor frame's payload length is 7")
        (is (= payload (drop 4 (read-region aimage slot-admit 11)))
            "the floor frame's payload is the source bytes")
        (let [[rexit rimage] (call-reframe image slot-frame 16 slot-reframe)]
          (is (zero? rexit) "the floor frame reframes")
          (is (= (read-region rimage slot-frame 16)
                 (read-region rimage slot-reframe 16))
              "the floor round-trip is byte-stable"))))))

(deftest empty-payload-admits
  (when (source-available?)
    ;; The authority admits a zero-length payload (packet_length ==
    ;; padding_length + 1 passes every clause of `packet-payload`) — which is
    ;; exactly why mode 1 reports the payload length through the buffer
    ;; rather than the return value.
    (let [image (fresh-image)]
      (call-frame image [])
      (let [framed (read-region image slot-frame 16)]
        (is (= (reference-packet []) framed)
            "the empty-payload frame matches the reference"))
      (let [[aexit aimage] (call-admit image slot-frame 16 slot-admit)]
        (is (zero? aexit)
            "the empty payload ADMITS — the case a boolean return could not")
        (is (zero? (be32 (take 4 (read-region aimage slot-admit 4))))
            "the reported payload length is zero"))
      (let [[rexit rimage] (call-reframe image slot-frame 16 slot-reframe)]
        (is (zero? rexit))
        (is (= (read-region rimage slot-frame 16)
               (read-region rimage slot-reframe 16)))))))

(deftest pad-arithmetic-agrees-with-the-reference-at-both-blocks
  (when (source-available?)
    ;; The modes hard-code the pre-NEWKEYS block of 8; the authority's rule is
    ;; asked at 16 too, pinning the two edges of `pad-length`: the correction
    ;; below the 4-byte floor, and the zero remainder (5+11 = 16, rem 0).
    (is (zero? (mod (count (reference-packet fixture-payload)) 8))
        "the fixture frame is 8-aligned")
    (is (= 4 (t/pad-length 7 8)) "pad 4 at the floor, payload 7")
    (is (= 16 (t/pad-length 11 16)) "pad 16 at the zero-remainder edge")
    (is (= 15 (t/pad-length 12 16)) "pad 15, no correction")
    (let [payload-11 (vec (range 11))
          image (fresh-image)]
      (call-frame image payload-11)
      (is (= (reference-packet payload-11)
             (read-region image slot-frame 32))
          "the block-8 frame of an 11-byte payload matches"))))

;; --- 3. the refused boundary, one mutation per clause -----------------------
;; Each refusal vector changes exactly one thing from a valid payload-7
;; frame, and the code the object returns must be the ONLY code that clause
;; produces. The control vector (no mutation) admits.

(def ^:private floor-frame
  "The valid payload-7 frame the refusals mutate, from the reference."
  (reference-packet (vec (range 7))))

(defn- refusal-exit
  "Present `seg` with declared length `seg-len`, out-len `out-len`, mode 1.
  Returns the entry's code."
  [seg seg-len out-len]
  (let [image (write-into (fresh-image) slot-refuse seg)]
    (first (call-object image slot-refuse seg-len slot-admit (or out-len 64) 1))))

(deftest every-refusal-clause-fires-with-its-own-code
  (when (source-available?)
    (testing "the control: the unmuted frame admits"
      (is (zero? (refusal-exit floor-frame 16 64))
          "a valid frame admits before any mutation is asked"))
    ;; -3: below the 6-byte floor, no framing bytes to read
    (is (= -3 (refusal-exit floor-frame 5 64))
        "seg-len 5 is refused with -3")
    ;; -4: padding_length 3, below the RFC floor of 4
    (is (= -4 (refusal-exit (assoc floor-frame 4 3) 16 64))
        "padding_length 3 is refused with -4")
    ;; -5: one byte truncated — the declared packet_length now runs past the
    ;; buffer
    (is (= -5 (refusal-exit floor-frame 15 64))
        "a truncated frame is refused with -5")
    ;; -6: packet_length 4 under padding_length 4 — the framing contradicts
    ;; itself, payload_len = -1
    (is (= -6 (refusal-exit (assoc floor-frame 0 0 1 0 2 0 3 4) 16 64))
        "packet_length below padding_length + 1 is refused with -6")
    ;; -7: out smaller than 4 + payload_length
    (is (= -7 (refusal-exit floor-frame 16 3))
        "out-len 3 is refused with -7")))

(deftest the-window-refusal-fires-before-any-load
  (when (source-available?)
    ;; seg-len 16385 is above the 16k load window: the entry refuses before
    ;; the machine would ever fault. The image is normal; the declared length
    ;; alone carries the refusal.
    (is (= -8 (refusal-exit floor-frame 16385 64))
        "seg-len above 16384 is refused with -8")))

(deftest the-mode-refusal-fires
  (when (source-available?)
    (let [image (write-into (fresh-image) slot-refuse floor-frame)]
      (is (= -9 (first (call-object image slot-refuse 16 slot-admit 64 7)))
          "a mode that is none of 0, 1, 2 is refused with -9"))))

(deftest the-null-base-clauses-are-caller-side-only
  ;; The -1/-2 clauses exist for the C caller, whose null pointers are what
  ;; they refuse. The subset's region-provenance check refuses any Kotoba
  ;; call site whose base argument is not a rooted region — a literal 0
  ;; included (measured against the pinned compiler) — so no Kotoba caller,
  ;; and no Kotoba KAT, can present a null base. The clauses are therefore
  ;; checked from the object's own guard structure, not from a vector: the
  ;; entry's first two tests run before any mode dispatch and cannot shadow
  ;; the refusal codes below them (each clause is the only producer of its
  ;; code). Recorded here so the absence is a decision, not an omission.
  (is (some? (re-find #"\(= seg 0\) -1"
                      (slurp source-file)))
      "the entry still refuses a null seg with -1")
  (is (some? (re-find #"\(= out 0\) -2"
                      (slurp source-file)))
      "the entry still refuses a null out with -2"))

;; --- 4. the object's own baked KAT ------------------------------------------
;; The boot path calls `ssh-packet-kat` with a 288-byte scratch
;; region. Until the wiring tranche lands, the same entry runs here — the
;; exact function the boot will call, over the exact vectors, in the KIR.
;; A later tranche emits AIUEOS_SSH_PKT_OK when this returns 0.

(deftest the-baked-kat-passes
  (when (source-available?)
    (let [image (fresh-image)
          exit (ir/execute @kir 'ssh-packet-kat [base]
                           {:fuel 1048576 :memory {:base base :bytes image}})]
      (is (zero? exit)
          "the baked KAT returns zero — every phase and every vector passed"))))

;; --- 5. agreement with the reference over a payload sweep -------------------
;; The fixture pins three payloads; the sweep pins the SHAPE: frame and
;; admit agree with `ssh.transport` for every payload length across both
;; edges of the pad rule, so no length in [0, 40) is one where the port
;; quietly differs from what it ports.

(deftest frame-and-admit-agree-over-a-payload-sweep
  (when (source-available?)
    (doseq [n (range 0 40)]
      (let [payload (vec (take n (cycle [(mod n 251)])))
            image (fresh-image)]
        (call-frame image payload)
        (let [framed (read-region image slot-frame (+ 4 (count (reference-packet payload))))]
          (is (= (reference-packet payload) framed)
              (str "payload " n ": framed bytes match the reference"))
          (is (zero? (mod (count (reference-packet payload)) 8))
              (str "payload " n ": reference frame is 8-aligned"))
          (let [[aexit aimage]
                (call-admit image slot-frame
                            (count (reference-packet payload)) slot-admit)]
            (is (zero? aexit) (str "payload " n ": admits"))
            (is (= n (be32 (take 4 (read-region aimage slot-admit 4))))
                (str "payload " n ": admitted length"))
            (is (= payload
                   (drop 4 (read-region aimage slot-admit (+ 4 n))))
                (str "payload " n ": admitted payload"))))))))
