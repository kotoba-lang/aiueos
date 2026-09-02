#!/usr/bin/env nbb
;; The Kotoba BOOTX64.EFI loader modules, executed on a firmware.
;;
;; `os/aiueos/uefi/loader-probe.kotoba` composes `aiueos.uefi.console` and
;; `aiueos.uefi.elf` into one UEFI application through the PROJECT route
;; (`--source-path`), so they are modules of one program rather than the
;; separate objects a kernel `.o` would have to be -- a kernel object exports
;; one symbol and cannot call another, and a loader that cannot call a
;; subroutine is not a loader.
;;
;; What this measures that a compiler suite cannot: that the PT_LOAD admission
;; contract answers the same verdicts when it is x86-64 machine code being run
;; by OVMF as it does when it is a data structure being walked by an
;; interpreter, and that the console module's output is bytes a real firmware
;; console driver rendered.
;;
;; Usage: nbb os/aiueos/scripts/smoke-qemu-uefi-loader.cljs /path/to/amu
;;        [--expect-markers STR] [--expect-verdicts STR]
;;
;; The two --expect flags exist so a deliberately broken variant can say WHICH
;; different answer it expects. A break that merely fails is not a
;; demonstration that this script discriminates.
(ns smoke-qemu-uefi-loader
  (:require ["child_process" :as cp]
            ["crypto" :as crypto]
            ["fs" :as fs]
            ["os" :as os]
            ["path" :as path]
            [clojure.string :as str]))

(def repo (path/resolve (path/join (path/dirname *file*) ".." ".." "..")))
(def aiueos (path/join repo "os" "aiueos"))
(def source (path/join aiueos "uefi" "loader-probe.kotoba"))
(def source-path (path/join aiueos "kotoba"))

;; K entry ran; H ImageHandle survived; S SystemTable survived; C ConOut found;
;; N ClearScreen+SetAttribute+OutputString all returned EFI_SUCCESS; A the real
;; kernel's headers were admitted; b..f each mutation refused with ITS OWN
;; reason; P a page the firmware allocated was WRITTEN and read back; W the
;; placement rule answered all five of its clauses; Z kernel-system-table still
;; answers, so R9 survived every call.
(def default-markers "KHSCNAbcdefPWZ")

;; The reasons `aiueos.uefi.elf/admit` gives for the six headers, in order.
;; Not "all non-zero": each names one clause, and swapping two of these
;; mutations would leave a "some refusal happened" check green.
(def default-verdicts "0 2 5 24 23 41")

;; fwstore: and the reasons `aiueos.uefi.memory/window-reason` gives for the
;; five placements, in order -- admitted, below 4 GiB, not allocated, ends
;; above 64 GiB, ends past the pages the firmware gave. The last is the clause
;; the 511 pages of alignment slack exist for.
(def default-window "0 4 3 6 7")

(def expected-entry "0000000000101000")
(def expected-status 33)

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
  (or (some #(when (and % (fs/existsSync %)) %)
            (cons (.. js/process -env -OVMF_CODE) ovmf-candidates))
      (die (str "OVMF firmware not found. Looked at: "
                (str/join ", " ovmf-candidates)))))

(defn flag [args name fallback]
  (let [i (.indexOf (clj->js args) name)]
    (if (neg? i) fallback (nth args (inc i) fallback))))

(defn- sha256 [file]
  (-> (crypto/createHash "sha256")
      (.update (fs/readFileSync file))
      (.digest "hex")))

(defn build! [compiler out]
  (let [efi (path/join out "esp" "EFI" "BOOT" "BOOTX64.EFI")]
    (fs/mkdirSync (path/dirname efi) #js {:recursive true})
    (let [compiled (run! (path/join compiler "bin" "amu")
                         ["compile" source
                          "--source-path" source-path
                          "--target" "x86_64-aiueos-uefi-v1"
                          "--artifact" "image"
                          "--output" efi
                          "--jvm-free"]
                         {})]
      (when-not (zero? (:status compiled))
        (die (str "compile failed:\n" (:stdout compiled) (:stderr compiled)))))
    ;; The same floor the pure-Kotoba kernel build keeps. A loader that
    ;; quietly linked a C object would prove nothing about a Kotoba loader.
    (doseq [entry (fs/readdirSync out #js {:recursive true})]
      (when (re-find #"\.(c|S|o|obj|a|so)$" entry)
        (die (str "foreign/C artifact entered the loader output: " entry))))
    {:efi efi :bytes (.-size (fs/statSync efi)) :sha256 (sha256 efi)}))

(defn observe [out code]
  (let [debug (path/join out "debug.log")
        serial (path/join out "serial.log")]
    (let [result (run! (or (.. js/process -env -QEMU_SYSTEM_X86_64)
                           "qemu-system-x86_64")
                       ["-machine" "q35,accel=tcg" "-cpu" "max" "-m" "256M"
                        "-drive" (str "if=pflash,format=raw,readonly=on,file=" code)
                        "-drive" (str "format=raw,file=fat:rw:" (path/join out "esp"))
                        "-device" "isa-debugcon,iobase=0xe9,chardev=dbg"
                        "-chardev" (str "file,id=dbg,path=" debug)
                        "-device" "isa-debug-exit,iobase=0xf4,iosize=0x04"
                        "-display" "none" "-no-reboot"
                        "-serial" (str "file:" serial)]
                       {:timeout 300000})]
      {:status (:status result)
       :debug (if (fs/existsSync debug) (fs/readFileSync debug "utf8") "")
       :serial (if (fs/existsSync serial)
                 (str/replace (fs/readFileSync serial "utf8") #"\r" "")
                 "")})))

(defn -main [& args]
  (let [compiler (or (first (remove #(str/starts-with? % "--") args))
                     (die "usage: smoke-qemu-uefi-loader.cljs /path/to/amu"))
        want-markers (flag args "--expect-markers" default-markers)
        want-verdicts (flag args "--expect-verdicts" default-verdicts)
        want-window (flag args "--expect-window" default-window)
        out (fs/mkdtempSync (path/join (os/tmpdir) "aiueos-uefi-loader-"))
        built (build! compiler out)
        seen (observe out (firmware))
        status (:status seen)
        debug (:debug seen)
        serial (:serial seen)
        ;; The firmware's own console driver rendered these, so the serial log
        ;; is a second and independent observation of the same decisions the
        ;; debug console marked.
        verdicts (second (re-find #"verdict ([0-9 ]+)" serial))
        window (second (re-find #"window ([0-9 ]+)" serial))
        entry (second (re-find #"entry\s+([0-9A-F]{16})" serial))]
    (println (str "BOOTX64.EFI " (:bytes built) " bytes image="
                  (subs (:sha256 built) 0 16)))
    ;; fwstore: the freshness receipt (ADR-0155). This harness compiles into
    ;; its own `mkdtemp` and never reads a pre-built artifact, so the staleness
    ;; ADR-0155 measured -- a KERNEL.ELF surviving a failed build -- cannot
    ;; happen here. What CAN happen, and what this refuses, is the boot reading
    ;; a different file from the one just compiled: the ESP is mounted
    ;; `fat:rw:`, so the firmware may write to it, and a harness that pointed
    ;; QEMU at the wrong path would report the same markers a correct run does
    ;; if a stale image were there. The digest is taken after the compile and
    ;; recomputed after the boot, on the byte the drive was built from.
    (let [after (sha256 (:efi built))]
      (when-not (= (:sha256 built) after)
        (die (str "REFUSED stale-image: the file QEMU booted is not the one"
                  " this run compiled\n  compiled " (:sha256 built)
                  "\n  after boot " after))))
    (println (str "exit=" status " debugcon=" (pr-str debug)))
    (println (str "entry=" (pr-str entry) " verdicts=" (pr-str verdicts)
                  " window=" (pr-str window)))
    (when-not (= expected-status status)
      (die (str "QEMU exited " status ", expected " expected-status
                " -- the probe's own checks did not reach isa-debug-exit")))
    (when-not (= want-markers debug)
      (die (str "debug console was " (pr-str debug) ", expected "
                (pr-str want-markers))))
    (when-not (= want-verdicts verdicts)
      (die (str "the firmware console reported verdicts " (pr-str verdicts)
                ", expected " (pr-str want-verdicts))))
    (when-not (= want-window window)
      (die (str "the firmware console reported window verdicts " (pr-str window)
                ", expected " (pr-str want-window))))
    ;; e_entry of the fixture kernel, read out of the literal pool by
    ;; `aiueos.uefi.elf/entry-point` and printed by `aiueos.uefi.console`.
    (when-not (= expected-entry entry)
      (die (str "e_entry printed as " (pr-str entry)
                ", expected " expected-entry)))
    (println (str "AIUEOS_UEFI_LOADER_OK markers=" debug
                  " verdicts=" (str/replace want-verdicts " " ",")
                  " window=" (str/replace want-window " " ",")
                  " entry=" entry))))

(apply -main (drop 3 (js->clj js/process.argv)))
