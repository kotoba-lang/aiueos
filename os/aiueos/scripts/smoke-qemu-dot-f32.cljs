#!/usr/bin/env nbb
;; The f32 dot product, executed.
;;
;; `kernel-dot-f32` (kotoba-gmir ADR 0010) selects ONE OF TWO instruction
;; sequences at run time, from a `cpuid`/`xgetbv` guard: eight lanes of AVX2
;; where the machine has it, and legacy scalar SSE where it does not. The
;; compiler's own suites can assert the bytes of both; they cannot assert that
;; the two compute the same number, because that is a claim about a machine.
;;
;; So this runs it on two machines. `-cpu max` under TCG exposes AVX2 and takes
;; the vector arm; `-cpu qemu64` does not and takes the scalar one. The
;; experiment is that the eight hex digits on the debug console are IDENTICAL,
;; and that both equal the answer `kotoba.kir`'s reference interpreter gives for
;; the same input -- which is what "bit-identical by construction" has to mean
;; if it means anything.
;;
;; This workstation is an Apple M4 and Rosetta exposes no AVX, so TCG is not a
;; convenience here: it is the only machine available that can answer at all.
;;
;; Usage: nbb os/aiueos/scripts/smoke-qemu-dot-f32.cljs /path/to/compiler
(ns smoke-qemu-dot-f32
  (:require ["child_process" :as cp]
            ["fs" :as fs]
            ["os" :as os]
            ["path" :as path]))

(def repo (path/resolve (path/join (path/dirname *file*) ".." ".." "..")))
(def aiueos (path/join repo "os" "aiueos"))
(def source (path/join aiueos "native" "dot-f32-probe.kotoba"))

;; The answer `kotoba.kir` gives for the vector the probe writes:
;;   A = [2^24, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]   B = twelve 1.0s
;;
;; 0x4B800004 is (s0+s1)+(s2+s3) over four lanes taking the lower half of each
;; eight-element block before the upper, then a four-element scalar tail.
;; 0x4B800000 is what a straight left-to-right sum answers -- every 1 lost into
;; the gap above 2^24 -- so this constant does not merely say "a dot product
;; happened", it says WHICH ONE.
(def expected-digits "4B800004")
(def expected-marker "DOT")
(def expected-status 33)

;; The console is <feature-nibble><eight digits>"DOT". The nibble is what the
;; guard tests, reported by the guest:
;;
;;   bit 0  leaf 1 ECX[27]  OSXSAVE
;;   bit 1  leaf 1 ECX[28]  AVX
;;   bit 2  leaf 7 EBX[5]   AVX2
;;   bit 3  XCR0 & 6 == 6
;;
;; It is the CONTROL. "Both machines printed the same digits" says nothing
;; unless the two machines differ in what the guard branches on, and it is also
;; the only way to know WHICH ARM each run took -- the guard's answer is
;; internal to one MC instruction and leaves no other trace.
(def feature-bits [[1 "osxsave"] [2 "avx"] [4 "avx2"] [8 "xcr0-ymm"]])

(defn decode-features [digit]
  (let [value (js/parseInt digit 16)]
    {:value value
     :named (mapv second (filter (fn [[bit _]] (pos? (bit-and value bit)))
                                 feature-bits))
     ;; The guard admits the AVX2 arm only when all four hold. Anything else
     ;; is the scalar arm, and that is the guard working, not failing.
     :arm (if (= 15 value) "avx2" "scalar")}))

;; Both models run the same artifact. `max` exposes AVX2 under TCG and takes
;; the vector arm; `qemu64` is a plain x86-64 with SSE2 and no AVX at all, so
;; the guard's leaf-1 ECX bit 28 test fails and the scalar arm runs.
(def cpu-models ["max" "qemu64"])

(def ovmf-candidates
  ["/opt/homebrew/share/qemu/edk2-x86_64-code.fd"
   "/usr/share/OVMF/OVMF_CODE_4M.fd"
   "/usr/share/OVMF/OVMF_CODE.fd"
   "/usr/share/edk2/x64/OVMF_CODE.fd"])

(defn die [message]
  (js/console.error (str "error: " message))
  (js/process.exit 1))

(defn run! [command args options]
  (let [result (cp/spawnSync command (clj->js args)
                             (clj->js (merge {:encoding "utf8"} options)))]
    {:status (.-status result)
     :stdout (or (.-stdout result) "")
     :stderr (or (.-stderr result) "")}))

(defn firmware []
  (or (some #(when (fs/existsSync %) %) (cons (.. js/process -env -OVMF_CODE)
                                              ovmf-candidates))
      (die (str "OVMF firmware not found. Looked at: "
                (clojure.string/join ", " ovmf-candidates)))))

(defn build! [compiler out]
  (let [kernel (path/join out "KERNEL.ELF")
        efi (path/join out "esp" "EFI" "BOOT" "BOOTX64.EFI")
        binary (path/join compiler "bin" "kotoba-compiler")]
    (fs/mkdirSync (path/dirname efi) #js {:recursive true})
    (let [compiled (run! binary ["compile" source
                                 "--target" "x86_64-aiueos-kernel-v1"
                                 "--artifact" "image"
                                 "--fuel" "32768"
                                 "--output" kernel]
                         {})]
      (when-not (zero? (:status compiled))
        (die (str "compile failed:\n" (:stdout compiled) (:stderr compiled)))))
    (let [packaged (run! binary ["package-aiueos-boot" kernel "--output" efi] {})]
      (when-not (zero? (:status packaged))
        (die (str "package failed:\n" (:stdout packaged) (:stderr packaged)))))
    ;; The same no-foreign-object floor the shipping kernel's build script
    ;; keeps. A test variant that quietly linked a C object would prove
    ;; nothing about a pure-Kotoba kernel.
    (doseq [entry (fs/readdirSync out #js {:recursive true})]
      (when (re-find #"\.(c|o|obj|a|so)$" entry)
        (die (str "foreign/C artifact entered the probe output: " entry))))
    {:kernel kernel :efi efi}))

(defn observe [out cpu code]
  (let [log (path/join out (str "debug-" cpu ".log"))]
    (when (fs/existsSync log) (fs/unlinkSync log))
    (let [result (run! (or (.. js/process -env -QEMU_SYSTEM_X86_64)
                           "qemu-system-x86_64")
                       ["-machine" "q35,accel=tcg"
                        "-cpu" cpu
                        "-m" "128M" "-smp" "2"
                        "-drive" (str "if=pflash,format=raw,readonly=on,file=" code)
                        "-drive" (str "format=raw,file=fat:rw:" (path/join out "esp"))
                        "-device" "isa-debugcon,iobase=0xe9,chardev=debug"
                        "-chardev" (str "file,id=debug,path=" log)
                        "-device" "isa-debug-exit,iobase=0xf4,iosize=0x04"
                        "-display" "none" "-serial" "none" "-no-reboot"]
                       {:timeout 300000})
          console (if (fs/existsSync log) (fs/readFileSync log "utf8") "")]
      {:cpu cpu :status (:status result) :console console})))

(defn -main [& args]
  (let [compiler (or (first args)
                     (die "usage: smoke-qemu-dot-f32.cljs /path/to/compiler"))
        out (fs/mkdtempSync (path/join (os/tmpdir) "aiueos-dot-f32-"))
        code (firmware)
        _ (build! compiler out)
        observations (mapv #(observe out % code) cpu-models)]
    (doseq [{:keys [cpu status console]} observations]
      (println (str "-cpu " cpu ": exit=" status " console=" (pr-str console))))
    (doseq [{:keys [cpu status console]} observations]
      (when-not (= expected-status status)
        (die (str "-cpu " cpu " exited " status ", expected " expected-status
                  " -- the probe's own checks failed inside the guest")))
      (when-not (re-find #"^[0-9A-F][0-9A-F]{8}DOT$" console)
        (die (str "-cpu " cpu " printed " (pr-str console)
                  ", which is not <feature-nibble><eight digits>DOT"))))
    (let [decoded (mapv (fn [{:keys [cpu console]}]
                          (assoc (decode-features (subs console 0 1))
                                 :cpu cpu
                                 :digits (subs console 1 9)))
                        observations)]
      (doseq [{:keys [cpu value named arm digits]} decoded]
        (println (str "  " cpu ": features=" value " " (pr-str named)
                      " arm=" arm " digits=" digits)))
      ;; Every run must agree with the reference interpreter. This is the
      ;; assertion that fails if an arm computes something else.
      (doseq [{:keys [cpu digits]} decoded]
        (when-not (= expected-digits digits)
          (die (str "-cpu " cpu " answered " digits ", and kotoba.kir answers "
                    expected-digits))))
      ;; The two models must actually have been different machines, or an
      ;; agreement between them is an agreement between one sequence and
      ;; itself.
      (when-not (= 2 (count (set (map :value decoded))))
        (die (str "both models reported the same CPU features "
                  (pr-str (mapv :value decoded))
                  " -- the two runs saw the same machine, so nothing here "
                  "distinguishes the arms")))
      (let [arms (set (map :arm decoded))]
        (println (str "AIUEOS_DOT_F32_QEMU digits=" expected-digits
                      " arms-exercised=" (clojure.string/join "," (sort arms))
                      " models=" (clojure.string/join "," cpu-models)))
        (if (contains? arms "avx2")
          (do (println (str "AIUEOS_DOT_F32_QEMU_OK both-arms-executed"
                            " and-agree-with-kotoba-kir exit=" expected-status))
              (js/process.exit 0))
          ;; NOT a pass, and NOT a failure. The scalar arm ran on two machines
          ;; and answered what the oracle answers; the AVX2 arm did not run at
          ;; all, so nothing here says anything about it.
          ;;
          ;; The reason is the guard working exactly as designed: `-cpu max`
          ;; has AVX and AVX2 in CPUID, and CR4.OSXSAVE is CLEAR, because
          ;; nothing set it. A kernel that used YMM anyway would not fault --
          ;; it would compute wrong answers intermittently and only under
          ;; load, which is the whole reason the guard tests bit 27.
          ;;
          ;; Setting it needs CR4 and `xsetbv`, and neither has a Kotoba
          ;; spelling: `kernel-write-cr0` exists and there is no CR4 operator
          ;; and no `kernel-xsetbv` anywhere in the surface (measured
          ;; 2026-09-02 across kotoba-sema, kotoba-gmir and kotoba-native).
          ;; That is what `prepare_bsp_extended_state()` does in the C kernel
          ;; -- CR4 bits 9, 10 and 18, then `xsetbv` -- and it is a real K16
          ;; gap, not a property of this test.
          (do (js/console.error
               (str "AIUEOS_DOT_F32_QEMU_AVX2_ARM_NOT_EXERCISED"
                    " reason=cr4-osxsave-clear"
                    " blocker=no-kotoba-operator-for-cr4-or-xsetbv"
                    " -- the scalar arm ran on both models and agrees with"
                    " kotoba.kir; the AVX2 arm did not run, so this says"
                    " nothing about it"))
              ;; Neither 0 nor 1: the question could not be answered.
              (js/process.exit 2)))))))

(apply -main (drop 3 (js->clj js/process.argv)))
