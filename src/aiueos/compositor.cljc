(ns aiueos.compositor
  "Desktop face (root ADR-2608221625 compositor unit).

  Hosted: this process owns window-session-state surfaces. QEMU for
  `smoke` is `-device virtio-gpu-pci` with `-display none` so P1b's
  no-keyboard bind path stays. That smoke does **not** prove guest 2D.

  Guest 2D: `clojure -M:compositor gpu` boots KERNEL.ELF (existing
  UEFI smoke, virtio-vga = virtio-gpu protocol) and admits only serial
  `AIUEOS_VIRTIO_GPU_CREATE result=ok` **and** `AIUEOS_VIRTIO_GPU_FLUSH
  result=ok`. GET_DISPLAY_INFO, GOP-once, and QMP query-pci are reds.

  Hosted WM: `clojure -M:compositor wm` admits two stacked surfaces,
  raise that changes z-order, DADS title bars, and pointer routing to
  the focused guest. WM does **not** require IME.

  Hosted IME: `clojure -M:compositor ime` admits romaji→hiragana in this
  process. IME-on consumes `ka` (guest does not see latin). Enter
  commits `か`. IME-off is the named red (`:ime-bypass`). Hosted kanji:
  `clojure -M:compositor kanji` admits Space converting `か` to `加`.
  Space that commits kana is leftover `:kanji-absent`. Guest IME:
  `clojure -M:compositor guest-ime` admits KERNEL.ELF Kotoba
  `aiueos-ime-commit` (`k`+`a` → U+304B) on guest serial. Hosted JVM
  IME does not count. Guest 2D stays `gpu`. Hosted kami-engine: `clojure
  -M:compositor kami` admits `kami.webgpu/init!` then `draw!` on
  `#kami-viewport`. A sky-only `beginRenderPass` clear is leftover
  `:clear-only-desktop`. Guest WM: `clojure -M:compositor guest-wm`
  admits KERNEL.ELF Kotoba `aiueos-wm-hit` (two overlapping boot
  rects, z-front at overlap is 2, miss-front at 40,40 is 1, raise
  is 1). Hosted JVM `wm` does not count. After guest WM, hosted
  leftover is still `:native-compositor-absent` (one virtio-gpu
  resource, virtio-input synthetic, P5). Guest paint:
  `clojure -M:compositor guest-paint` admits KERNEL.ELF painting both
  boot rects in Kotoba z-order and sampling the overlap pixel. Hosted
  JVM `wm` does not count. A key-order paint (window 1 on top at
  overlap) is leftover `:key-order-paint`. After guest paint, hosted
  leftover is still `:native-compositor-absent` (permission
  broker, native component runtime, one virtio-gpu resource, P5).
  Guest input: `clojure -M:compositor guest-input` admits KERNEL.ELF
  consuming a virtio-keyboard used-ring event. C filling the envelope
  is leftover `:synthetic-smoke`. After guest input, hosted leftover
  is still `:native-compositor-absent` (permission broker, native
  component runtime, one virtio-gpu resource, P5).
  Guest gpu-two: `clojure -M:compositor guest-gpu-two` admits KERNEL.ELF
  creating and flushing two virtio-gpu 2D resources when Kotoba
  `kotoba_aiueos_wm_hit` admits n=2. C hardcoding resource count is
  leftover `:one-resource`. After guest gpu-two, hosted leftover is still
  `:native-compositor-absent` (permission broker, native component
  runtime, one virtio-gpu scanout, P5).

  Exit 0 = the named command admitted. Exit 1 = refused. Exit 3 =
  QEMU/firmware/serial/browser could not be answered.

  Commands: `clojure -M:compositor smoke` | `gpu` | `wm` | `ime` | `kanji` | `kami` | `guest-ime` | `guest-wm` | `guest-paint` | `guest-input` | `guest-gpu-two` | `serve`"
  (:require [aiueos.compositor.desktop :as desktop]
            [aiueos.phone-bind :as pb]
            [clojure.string :as str]
            #?(:clj [clojure.edn :as edn])
            #?(:clj [clojure.java.io :as io])
            #?(:clj [clojure.pprint :as pprint])))

(defn gpu-argv?
  [argv]
  (boolean (and (some #{"virtio-gpu-pci"} argv)
                (pb/headless-argv? argv))))

(defn pci-names-gpu?
  "QEMU query-pci does not echo the qdev name virtio-gpu-pci.
  Measured on qemu-system-aarch64 10.1 virt + virtio-gpu-pci: class 896
  desc Display controller, virtio device id 4176 (0x1050).
  This is the `smoke` PCI floor. It does **not** admit guest 2D."
  [qmp-body]
  (boolean
   (and (string? qmp-body)
        (or (str/includes? qmp-body "virtio-gpu")
            (str/includes? qmp-body "virtio-vga")
            (str/includes? qmp-body "Display controller")
            (str/includes? qmp-body "VGA controller")
            (str/includes? qmp-body "\"device\": 4176")
            (str/includes? qmp-body "\"device\":4176")))))

(defn guest-2d-create+flush?
  "True only when the *guest serial* says virtio-gpu 2D create and flush
  completed. Display-info, GOP-once, and query-pci are not this."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_VIRTIO_GPU_CREATE result=ok" serial)
        (re-find #"(?m)^AIUEOS_VIRTIO_GPU_FLUSH result=ok" serial))))

(defn gpu-2d-result
  "Classify a KERNEL.ELF boot for the compositor `gpu` profile.
  `pci-only?` is the named red: QMP listed a GPU and nobody ran create/flush."
  [{:keys [serial qemu-unmeasured? pci-only?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    pci-only?
    {:green? false :exit 1 :reason :pci-device-listed-does-not-count
     :leftover [:pci-device-listed-does-not-count]}

    (guest-2d-create+flush? serial)
    {:green? true :exit 0 :reason :guest-2d-create-flush :leftover []}

    (re-find #"(?m)^AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info"
             (or serial ""))
    {:green? false :exit 1 :reason :gpu-2d-create-flush-absent
     :leftover [:gpu-2d-create-flush-absent]}

    :else
    {:green? false :exit 1 :reason :gpu-2d-absent
     :leftover [:gpu-2d-absent]}))

(defn guest-ime-ok?
  "True only when KERNEL.ELF serial says Kotoba IME committed U+304B
  without echoing latin `ka`. Hosted JVM IME serial does not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_IME_OK committed=u\+304b latin-leak=0" serial)
        (not (re-find #"(?m)^AIUEOS_GUEST_IME_OK committed=ka" serial))
        (not (re-find #"AIUEOS_COMPOSITOR_IME_OK" serial)))))

(defn guest-ime-result
  "Classify a KERNEL.ELF boot for compositor `guest-ime`.
  Hosted `clojure -M:compositor ime` is the named red."
  [{:keys [serial qemu-unmeasured? hosted-ime?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-ime?
        (re-find #"AIUEOS_COMPOSITOR_IME_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-ime-does-not-count
     :leftover [:hosted-ime-does-not-count]}

    (guest-ime-ok? serial)
    {:green? true :exit 0 :reason :guest-ime-kotoba-commit :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_IME leftover=latin-leak" (or serial ""))
    {:green? false :exit 1 :reason :latin-leak :leftover [:latin-leak]}

    (re-find #"(?m)^AIUEOS_GUEST_IME leftover=vector-miss" (or serial ""))
    {:green? false :exit 1 :reason :vector-miss :leftover [:vector-miss]}

    :else
    {:green? false :exit 1 :reason :guest-ime-absent
     :leftover [:guest-ime-absent]}))

(defn guest-wm-ok?
  "True only when KERNEL.ELF serial says Kotoba WM hit the four
  boot-desktop vectors. Hosted JVM WM serial does not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_WM_OK two-surfaces z-hit=2 miss-front=1 raise=1 one-surface=0" serial)
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-wm-result
  "Classify a KERNEL.ELF boot for compositor `guest-wm`.
  Hosted `clojure -M:compositor wm` is the named red."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-wm-ok? serial)
    {:green? true :exit 0 :reason :guest-wm-kotoba-hit :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_WM leftover=one-surface-ignored" (or serial ""))
    {:green? false :exit 1 :reason :one-surface-ignored :leftover [:one-surface-ignored]}

    (re-find #"(?m)^AIUEOS_GUEST_WM leftover=z-order-ignored" (or serial ""))
    {:green? false :exit 1 :reason :z-order-ignored :leftover [:z-order-ignored]}

    (re-find #"(?m)^AIUEOS_GUEST_WM leftover=always-front" (or serial ""))
    {:green? false :exit 1 :reason :always-front :leftover [:always-front]}

    (re-find #"(?m)^AIUEOS_GUEST_WM leftover=raise-is-noop" (or serial ""))
    {:green? false :exit 1 :reason :raise-is-noop :leftover [:raise-is-noop]}

    (re-find #"(?m)^AIUEOS_GUEST_WM leftover=vector-miss" (or serial ""))
    {:green? false :exit 1 :reason :vector-miss :leftover [:vector-miss]}

    :else
    {:green? false :exit 1 :reason :guest-wm-absent
     :leftover [:guest-wm-absent]}))

(defn guest-paint-ok?
  "True only when KERNEL.ELF serial says both boot rects were painted
  in Kotoba z-order and the overlap pixel followed front, not key
  order. Hosted JVM WM serial does not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_PAINT_OK boot-overlap=2 raised-overlap=1 key-order=0" serial)
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-paint-result
  "Classify a KERNEL.ELF boot for compositor `guest-paint`.
  Hosted `clojure -M:compositor wm` is the named red."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-paint-ok? serial)
    {:green? true :exit 0 :reason :guest-paint-z-order :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_PAINT leftover=fb-too-small" (or serial ""))
    {:green? false :exit 1 :reason :fb-too-small :leftover [:fb-too-small]}

    (re-find #"(?m)^AIUEOS_GUEST_PAINT leftover=one-guest-scanout" (or serial ""))
    {:green? false :exit 1 :reason :one-guest-scanout :leftover [:one-guest-scanout]}

    (re-find #"(?m)^AIUEOS_GUEST_PAINT leftover=key-order-paint" (or serial ""))
    {:green? false :exit 1 :reason :key-order-paint :leftover [:key-order-paint]}

    (re-find #"(?m)^AIUEOS_GUEST_PAINT leftover=always-front-paint" (or serial ""))
    {:green? false :exit 1 :reason :always-front-paint :leftover [:always-front-paint]}

    (re-find #"(?m)^AIUEOS_GUEST_PAINT leftover=vector-miss" (or serial ""))
    {:green? false :exit 1 :reason :vector-miss :leftover [:vector-miss]}

    :else
    {:green? false :exit 1 :reason :guest-paint-absent
     :leftover [:guest-paint-absent]}))

(defn guest-input-ok?
  "True only when KERNEL.ELF serial says the desktop envelope came
  from a virtio-input used-ring event, not the synthetic fill.
  Hosted JVM compositor input does not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_INPUT_OK eventq-used=1 synthetic=0" serial)
        (not (re-find #"(?m)^AIUEOS_GUEST_INPUT leftover=synthetic-smoke" serial))
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-input-result
  "Classify a KERNEL.ELF boot for compositor `guest-input`.
  Hosted `clojure -M:compositor wm` is the named red. C filling the
  envelope is leftover `:synthetic-smoke`."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-input-ok? serial)
    {:green? true :exit 0 :reason :guest-input-eventq :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_INPUT leftover=synthetic-smoke" (or serial ""))
    {:green? false :exit 1 :reason :synthetic-smoke :leftover [:synthetic-smoke]}

    (re-find #"(?m)^AIUEOS_GUEST_INPUT leftover=eventq-empty" (or serial ""))
    {:green? false :exit 1 :reason :eventq-empty :leftover [:eventq-empty]}

    :else
    {:green? false :exit 1 :reason :guest-input-absent
     :leftover [:guest-input-absent]}))

(defn guest-gpu-two-ok?
  "True only when KERNEL.ELF serial says two virtio-gpu 2D resources
  were created and flushed after Kotoba admitted n=2. Hosted JVM gpu
  serial alone does not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_GPU_TWO_OK resources=2 flush=2 kotoba-n=2" serial)
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-gpu-two-result
  "Classify a KERNEL.ELF boot for compositor `guest-gpu-two`.
  Hosted `clojure -M:compositor wm` is the named red. One resource
  when Kotoba admits two is leftover `:one-resource`."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-gpu-two-ok? serial)
    {:green? true :exit 0 :reason :guest-gpu-two-resources :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_GPU_TWO leftover=one-resource" (or serial ""))
    {:green? false :exit 1 :reason :one-resource :leftover [:one-resource]}

    :else
    {:green? false :exit 1 :reason :guest-gpu-two-absent
     :leftover [:guest-gpu-two-absent]}))

(defn guest-scanout-two-ok?
  "True only when KERNEL.ELF serial says two virtio-gpu scanouts were
  bound after Kotoba admitted n=2. Hosted JVM wm serial alone does
  not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_SCANOUT_TWO_OK scanouts=2 resource-0=1 resource-1=2 kotoba-n=2" serial)
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-scanout-two-result
  "Classify a KERNEL.ELF boot for compositor `guest-scanout-two`.
  Hosted `clojure -M:compositor wm` is the named red. One scanout
  when Kotoba admits two is leftover `:one-scanout`."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-scanout-two-ok? serial)
    {:green? true :exit 0 :reason :guest-scanout-two-bound :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_SCANOUT_TWO leftover=one-scanout" (or serial ""))
    {:green? false :exit 1 :reason :one-scanout :leftover [:one-scanout]}

    :else
    {:green? false :exit 1 :reason :guest-scanout-two-absent
     :leftover [:guest-scanout-two-absent]}))

(defn html-has-desktop-face?
  [html]
  (and (re-find #"dads-button" html)
       (re-find #"jp-go-dds" html)
       (re-find #"href=\"#session\"" html)
       (re-find #"href=\"#setup\"" html)
       (re-find #"href=\"#desktop\"" html)
       (re-find #"id=\"kami-viewport\"" html)
       (re-find #"kami.webgpu" html)
       (re-find #"window.__aiueosSessionAlive" html)
       (not (re-find #"liquid-glass" html))))

(defn html-has-wm-face?
  "DADS decorations for two stacked windows. A JSON dump of surfaces,
  or a single notes iframe with no title bar, is red."
  [html]
  (boolean
   (and (html-has-desktop-face? html)
        (re-find #"id=\"wm-stage\"" html)
        (>= (count (re-seq #"class=\"[^\"]*wm-window" html)) 2)
        (re-find #"wm-titlebar" html)
        (re-find #"dads-chip-label" html)
        (re-find #"data-raise" html)
        (re-find #"dads-heading" html)
        (not (re-find #"liquid-glass" html)))))

(defn html-has-ime-face?
  "DADS candidate bar on #desktop. WM title bars without #ime-bar are red
  for the ime gate (WM itself stays green)."
  [html]
  (boolean
   (and (html-has-wm-face? html)
        (re-find #"id=\"ime-bar\"" html)
        (re-find #"id=\"ime-preedit\"" html)
        (re-find #"id=\"ime-toggle\"" html)
        (re-find #"data-ime" html)
        (not (re-find #"liquid-glass" html)))))

(defn html-has-kanji-face?
  "DADS candidate list on #desktop. IME bar without #ime-candidates is
  red for the kanji gate (kana itself stays green)."
  [html]
  (boolean
   (and (html-has-ime-face? html)
        (re-find #"id=\"ime-candidates\"" html))))

(defn html-has-kami-face?
  "SPA loads the kami.webgpu presenter and names the clear-only red.
  `beginRenderPass` inside the executor bundle is not this red; the old
  sky-clear `presentKami` (`requesting WebGPU for kami.webgpu.ir`) is."
  [html]
  (boolean
   (and (html-has-desktop-face? html)
        (re-find #"src=\"/kami-presenter.js\"" html)
        (re-find #"aiueosKamiPresent" html)
        (re-find #"data-executor=\"kami.webgpu\"" html)
        (re-find #"clear-only-desktop" html)
        (not (re-find #"requesting WebGPU for kami.webgpu.ir" html)))))

(defn html-has-guest-ime-face?
  "SPA names the KERNEL.ELF guest IME gate. Hosted ime/kanji without
  `clojure -M:compositor guest-ime` is red for this face."
  [html]
  (boolean
   (and (html-has-kanji-face? html)
        (re-find #"clojure -M:compositor guest-ime" html)
        (re-find #"native compositor" html))))

(defn html-has-guest-wm-face?
  "SPA names the KERNEL.ELF guest WM gate. Hosted wm without
  `clojure -M:compositor guest-wm` is red for this face."
  [html]
  (boolean
   (and (html-has-guest-ime-face? html)
        (re-find #"clojure -M:compositor guest-wm" html))))

(defn html-has-guest-paint-face?
  "SPA names the KERNEL.ELF guest paint gate. Guest WM without
  `clojure -M:compositor guest-paint` is red for this face."
  [html]
  (boolean
   (and (html-has-guest-wm-face? html)
        (re-find #"clojure -M:compositor guest-paint" html))))

(defn html-has-guest-input-face?
  "SPA names the KERNEL.ELF guest input gate. Guest paint without
  `clojure -M:compositor guest-input` is red for this face."
  [html]
  (boolean
   (and (html-has-guest-paint-face? html)
        (re-find #"clojure -M:compositor guest-input" html))))

(defn html-has-guest-gpu-two-face?
  "SPA names the KERNEL.ELF guest gpu-two gate. Guest input without
  `clojure -M:compositor guest-gpu-two` is red for this face."
  [html]
  (boolean
   (and (html-has-guest-input-face? html)
        (re-find #"clojure -M:compositor guest-gpu-two" html))))

(defn html-has-guest-scanout-two-face?
  "SPA names the KERNEL.ELF guest scanout-two gate. Guest gpu-two without
  `clojure -M:compositor guest-scanout-two` is red for this face."
  [html]
  (boolean
   (and (html-has-guest-gpu-two-face? html)
        (re-find #"clojure -M:compositor guest-scanout-two" html))))

(defn presenter-is-kami-webgpu?
  "The compiled bundle must be kami.webgpu (init!/draw!), not a stub
  that returns admitted without the executor."
  [js]
  (boolean
   (and (string? js)
        (re-find #"aiueosKamiPresent" js)
        (re-find #"init_BANG_" js)
        (re-find #"draw_BANG_" js))))

(defn- index-of [xs x]
  (loop [i 0]
    (cond
      (>= i (count xs)) -1
      (= (nth xs i) x) i
      :else (recur (inc i)))))

(defn gpu-argv
  "P1b chassis argv plus virtio-gpu. Display stays `none` so bind still
  needs no local keyboard (動線 D is extra). The GPU device is the
  display-session evidence."
  [opts]
  (let [base (vec (pb/chassis-argv-shape (merge opts {:graphics "virtio-gpu"
                                                      :display "none"})))]
    (if (some #{"virtio-gpu-pci"} base)
      base
      (let [i (index-of base "-display")]
        (if (neg? i)
          (into base ["-device" "virtio-gpu-pci"])
          (let [at (+ i 2)]
            (vec (concat (subvec base 0 at)
                         ["-device" "virtio-gpu-pci"]
                         (subvec base at)))))))))

#?(:clj
   (do
     (defn desktop-file
       [dir]
       (io/file dir "state" "desktop.edn"))

     (defn persist!
       [dir d]
       (let [f (desktop-file dir)]
         (io/make-parents f)
         (spit f (pr-str (desktop/persistable d)))
         f))

     (defn load-persisted
       [dir]
       (let [f (desktop-file dir)]
         (if (.isFile f)
           (desktop/ensure-ime (edn/read-string (slurp f)))
           (desktop/empty-desktop))))

     (defn boot-or-restore
       "Restore when a desktop file exists; otherwise open the default surfaces.
       A missing file is a wipe, not a successful empty session."
       [dir]
       (if (.isFile (desktop-file dir))
         (load-persisted dir)
         (desktop/boot-desktop)))

     (defn start-gpu-qemu!
       "Firmware-only aarch64 virt with virtio-gpu-pci. `smoke` admits PCI
       listing only. Guest 2D create/flush is `clojure -M:compositor gpu`."
       [dir {:keys [accel] :as opts}]
       (let [firmware (pb/find-firmware)
             qemu (pb/find-qemu)
             qmp (.getPath (pb/qmp-file dir))
             serial (.getPath (pb/serial-file dir))]
         (cond
           (nil? firmware)
           {:ok false :unmeasured true :reason :firmware-missing
            :tried pb/firmware-candidates}

           :else
           (do
             (io/make-parents (io/file dir "qemu.stdout"))
             (.delete (io/file qmp))
             (let [argv (gpu-argv {:qemu qemu :firmware firmware :qmp qmp
                                   :serial serial :accel accel})
                   pbld (doto (ProcessBuilder. ^java.util.List argv)
                          (.redirectOutput (io/file dir "qemu.stdout"))
                          (.redirectError (io/file dir "qemu.stderr")))
                   proc (.start pbld)
                   ready (pb/wait-qmp qmp 8000)
                   alive (.isAlive proc)]
               (spit (io/file dir "qemu.pid") (str (.pid proc)))
               (spit (io/file dir "qemu.argv.edn") (pr-str argv))
               (cond
                 (and ready alive (gpu-argv? argv))
                 {:ok true :process proc :argv argv :qmp qmp :serial serial
                  :firmware firmware :qemu qemu :graphics "virtio-gpu"
                  :display "none"}

                 (and (not alive) (not= accel "tcg") (nil? (:no-fallback opts)))
                 (do (try (.destroyForcibly proc) (catch Exception _))
                     (start-gpu-qemu! dir (assoc opts :accel "tcg" :no-fallback true)))

                 :else
                 {:ok false
                  :reason (if alive :qmp-timeout :qemu-exited)
                  :exit (when-not alive (.exitValue proc))
                  :argv argv
                  :stderr (when (.isFile (io/file dir "qemu.stderr"))
                            (slurp (io/file dir "qemu.stderr")))}))))))

     (defn make-compositor-runtime
       [dir]
       (assoc (pb/make-runtime {:dir dir :listen-port 0})
              :desktop (atom (boot-or-restore dir))))

     (defn run-smoke
       [{:keys [dir]}]
       (let [dir (io/file (or dir (str (System/getProperty "java.io.tmpdir")
                                       "/aiueos-compositor")))
             qemu (atom nil)
             http (atom nil)]
         (try
           (io/make-parents (desktop-file dir))
           (let [rt1 (pb/start-http! (make-compositor-runtime dir))
                 d1 @(:desktop rt1)
                 _ (persist! dir d1)
                 first-ok? (desktop/restore-admitted? d1)]
             (pb/stop-http! rt1)
             (let [rt2 (pb/start-http! (make-compositor-runtime dir))
                   _ (reset! http rt2)
                   restored @(:desktop rt2)
                   restored-ok? (desktop/restore-admitted? restored)
                   wipe-dir (io/file dir "wipe-probe")
                   _ (io/make-parents (desktop-file wipe-dir))
                   _ (when (.isFile (desktop-file wipe-dir))
                       (.delete (desktop-file wipe-dir)))
                   wiped (load-persisted wipe-dir)
                   wipe-red? (not (desktop/restore-admitted? wiped))
                   base (pb/base-url rt2)
                   page (pb/http-get (str base "/"))
                   html (:body page)
                   spa? (and (= 200 (:code page)) (html-has-desktop-face? html))
                   api (pb/http-get (str base "/api/compositor/desktop"))
                   api-ok? (and (= 200 (:code api))
                                (re-find #"window-session-state" (:body api))
                                (re-find #"guest-surface" (:body api))
                                (re-find #"kami.webgpu.ir" (:body api)))
                   q (start-gpu-qemu! dir {})
                   _ (reset! qemu q)
                   _ (reset! (:qemu rt2) q)
                   pci (when (:ok q)
                         (try (pb/qmp-eval (:qmp q) "{\"execute\":\"query-pci\"}")
                              (catch Exception e (str "qmp-error:" (.getMessage e)))))
                   _ (when (string? pci)
                       (spit (io/file dir "qemu.pci.json") pci))
                   gpu-ok? (boolean (and (:ok q) (gpu-argv? (:argv q)) (pci-names-gpu? pci)))]
               (println (str "AIUEOS_COMPOSITOR_URL=" base "/#desktop"))
               (println (str "AIUEOS_COMPOSITOR_SPA=" (if spa? "admitted" "refused")))
               (println (str "AIUEOS_COMPOSITOR_SURFACES="
                             (if (and first-ok? restored-ok? api-ok?) "admitted" "refused")))
               (println (str "AIUEOS_COMPOSITOR_RESTORE="
                             (if restored-ok? "admitted" "refused")))
               (println (str "AIUEOS_COMPOSITOR_WIPE="
                             (if wipe-red? "refused-as-required" "falsely-admitted")))
               (println (str "AIUEOS_COMPOSITOR_DISPLAY=" (or (:display q) "none")))
               (println (str "AIUEOS_COMPOSITOR_GPU="
                             (if gpu-ok? "virtio-gpu-pci" (or (:reason q) "missing"))))
               (println (str "AIUEOS_COMPOSITOR_ARGV=" (pr-str (:argv q))))
               (println (str "AIUEOS_COMPOSITOR_QEMU_LOG="
                             (.getPath (io/file dir "qemu.stderr"))))
               (println (str "AIUEOS_COMPOSITOR_SERIAL="
                             (.getPath (pb/serial-file dir))))
               (println (str "AIUEOS_COMPOSITOR_DESKTOP="
                             (.getPath (desktop-file dir))))
               (when pci
                 (println (str "AIUEOS_COMPOSITOR_PCI="
                               (subs pci 0 (min 400 (count pci))))))
               (when (and spa? first-ok? restored-ok? wipe-red? api-ok? gpu-ok?)
                 (println "AIUEOS_COMPOSITOR_OK"))
               (when-not (and spa? first-ok? restored-ok? wipe-red? api-ok? gpu-ok?)
                 (println (str "AIUEOS_COMPOSITOR_FAIL"
                               " spa=" spa?
                               " first=" first-ok?
                               " restore=" restored-ok?
                               " wipe-red=" wipe-red?
                               " api=" api-ok?
                               " qemu=" (pr-str (dissoc q :process)))))
               {:exit (cond
                        (not spa?) 1
                        (not first-ok?) 1
                        (not restored-ok?) 1
                        (not wipe-red?) 1
                        (not api-ok?) 1
                        (:unmeasured q) 3
                        (not gpu-ok?) 1
                        :else 0)
                :spa? spa? :restored restored :wiped wiped :qemu q}))
           (finally
             (when-let [q @qemu] (pb/stop-qemu! q))
             (when-let [rt @http] (pb/stop-http! rt))))))

     (defn repo-root
       []
       (io/file (System/getProperty "user.dir")))

     (defn uefi-smoke
       []
       (io/file (repo-root) "os" "aiueos" "scripts" "smoke-qemu-uefi.sh"))

     (defn kernel-serial-file
       []
       (io/file (repo-root) "build" "aiueos" "kernel-serial.log"))

     (defn gpu-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-gpu-receipt.edn"))

     (defn guest-ime-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-guest-ime-receipt.edn"))

     (defn guest-wm-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-guest-wm-receipt.edn"))

     (defn guest-paint-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-guest-paint-receipt.edn"))

     (defn guest-input-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-guest-input-receipt.edn"))

     (defn guest-gpu-two-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-guest-gpu-two-receipt.edn"))

     (defn guest-scanout-two-receipt-file
       []
       (io/file (repo-root) "build" "aiueos" "compositor-guest-scanout-two-receipt.edn"))

     (defn write-gpu-receipt!
       [receipt]
       (let [f (gpu-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn write-guest-ime-receipt!
       [receipt]
       (let [f (guest-ime-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn write-guest-wm-receipt!
       [receipt]
       (let [f (guest-wm-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn write-guest-paint-receipt!
       [receipt]
       (let [f (guest-paint-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn write-guest-input-receipt!
       [receipt]
       (let [f (guest-input-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn write-guest-gpu-two-receipt!
       [receipt]
       (let [f (guest-gpu-two-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn write-guest-scanout-two-receipt!
       [receipt]
       (let [f (guest-scanout-two-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn serial-measured?
       [serial]
       (boolean (and (string? serial) (re-find #"AIUEOS_" serial))))

     (defn run-uefi-2d!
       "Existing UEFI QEMU smoke (virtio-vga = virtio-gpu protocol).
       No AIUEOS_TEST_NET — that is P2 and not this gate. No new .sh.
       extra-env is merged into the child environment (guest-input,
       guest-scanout-two dbus SetUIInfo)."
       ([] (run-uefi-2d! nil))
       ([extra-env]
        (let [script (uefi-smoke)]
          (if-not (.isFile script)
            {:ok false :unmeasured true :reason :smoke-script-missing
             :tried (.getPath script)}
            (let [pb (doto (ProcessBuilder. ^java.util.List
                                            ["sh" (.getPath script)])
                       (.directory (repo-root))
                       (.inheritIO))
                  _ (when extra-env
                      (let [e (.environment pb)]
                        (doseq [[k v] extra-env]
                          (.put e (str k) (str v)))))
                  proc (.start pb)
                  exit (.waitFor proc)
                  serial (when (.isFile (kernel-serial-file))
                           (slurp (kernel-serial-file)))]
              {:ok (zero? exit)
               :exit exit
               :serial serial
               :serial-path (.getPath (kernel-serial-file))})))))

     (defn run-gpu
       "Guest 2D create/flush. PCI listing and GET_DISPLAY_INFO are reds."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (gpu-2d-result {:qemu-unmeasured? true})]
             (write-gpu-receipt! (assoc r :measured-at (str (java.time.Instant/now))
                                        :command "clojure -M:compositor gpu"
                                        :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GPU_2D leftover=:unmeasured")
             r)
           (let [boot (run-uefi-2d!)
                 measured? (serial-measured? (:serial boot))
                 r (gpu-2d-result {:serial (:serial boot)
                                   :qemu-unmeasured? (not measured?)
                                   :pci-only? false})
                 receipt (assoc r
                                :aiueos.compositor/gpu-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor gpu"
                                :profile :uefi-qemu-virtio-gpu-2d
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has CREATE result=ok and FLUSH result=ok. query-pci and GET_DISPLAY_INFO do not count. Not a WM. IME leftover.")]
             (write-gpu-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GPU_PROFILE=uefi-qemu-2d")
             (println (str "AIUEOS_COMPOSITOR_GPU_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GPU_RECEIPT=" (.getPath (gpu-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GPU_LEFTOVER=" (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_VIRTIO_GPU" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GPU_2D green")
               (println (str "AIUEOS_COMPOSITOR_GPU_2D not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-guest-ime
       "Guest Kotoba IME on KERNEL.ELF serial. Hosted JVM IME is red."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (guest-ime-result {:qemu-unmeasured? true})]
             (write-guest-ime-receipt!
              (assoc r :measured-at (str (java.time.Instant/now))
                     :command "clojure -M:compositor guest-ime"
                     :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GUEST_IME leftover=:unmeasured")
             r)
           (let [boot (run-uefi-2d!)
                 measured? (serial-measured? (:serial boot))
                 r (guest-ime-result {:serial (:serial boot)
                                      :qemu-unmeasured? (not measured?)
                                      :hosted-ime? false})
                 receipt (assoc r
                                :aiueos.compositor/guest-ime-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor guest-ime"
                                :profile :uefi-qemu-guest-ime
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has GUEST_IME_OK committed=u+304b latin-leak=0. Hosted clojure -M:compositor ime does not count. virtio-input synthetic-smoke remains. Native Phase 6 compositor leftover. P5 UNVERIFIED.")]
             (write-guest-ime-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GUEST_IME_PROFILE=uefi-qemu")
             (println (str "AIUEOS_COMPOSITOR_GUEST_IME_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GUEST_IME_RECEIPT="
                           (.getPath (guest-ime-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GUEST_IME_LEFTOVER="
                           (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_GUEST_IME" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GUEST_IME_OK")
               (println (str "AIUEOS_COMPOSITOR_GUEST_IME not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-guest-wm
       "Guest Kotoba WM on KERNEL.ELF serial. Hosted JVM WM is red."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (guest-wm-result {:qemu-unmeasured? true})]
             (write-guest-wm-receipt!
              (assoc r :measured-at (str (java.time.Instant/now))
                     :command "clojure -M:compositor guest-wm"
                     :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GUEST_WM leftover=:unmeasured")
             r)
           (let [boot (run-uefi-2d!)
                 measured? (serial-measured? (:serial boot))
                 r (guest-wm-result {:serial (:serial boot)
                                     :qemu-unmeasured? (not measured?)
                                     :hosted-wm? false})
                 receipt (assoc r
                                :aiueos.compositor/guest-wm-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor guest-wm"
                                :profile :uefi-qemu-guest-wm
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has GUEST_WM_OK two-surfaces z-hit=2 miss-front=1 raise=1 one-surface=0. Hosted clojure -M:compositor wm does not count. virtio-input synthetic-smoke remains. Native Phase 6 compositor leftover. P5 UNVERIFIED.")]
             (write-guest-wm-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GUEST_WM_PROFILE=uefi-qemu")
             (println (str "AIUEOS_COMPOSITOR_GUEST_WM_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GUEST_WM_RECEIPT="
                           (.getPath (guest-wm-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GUEST_WM_LEFTOVER="
                           (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_GUEST_WM" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GUEST_WM_OK")
               (println (str "AIUEOS_COMPOSITOR_GUEST_WM not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-guest-paint
       "Guest GOP paint of two z-ordered boot rects. Hosted JVM WM is red."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (guest-paint-result {:qemu-unmeasured? true})]
             (write-guest-paint-receipt!
              (assoc r :measured-at (str (java.time.Instant/now))
                     :command "clojure -M:compositor guest-paint"
                     :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GUEST_PAINT leftover=:unmeasured")
             r)
           (let [boot (run-uefi-2d!)
                 measured? (serial-measured? (:serial boot))
                 r (guest-paint-result {:serial (:serial boot)
                                        :qemu-unmeasured? (not measured?)
                                        :hosted-wm? false})
                 receipt (assoc r
                                :aiueos.compositor/guest-paint-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor guest-paint"
                                :profile :uefi-qemu-guest-paint
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has GUEST_PAINT_OK boot-overlap=2 raised-overlap=1 key-order=0. Hosted clojure -M:compositor wm does not count. virtio-input synthetic-smoke remains. Native Phase 6 compositor leftover. P5 UNVERIFIED.")]
             (write-guest-paint-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GUEST_PAINT_PROFILE=uefi-qemu")
             (println (str "AIUEOS_COMPOSITOR_GUEST_PAINT_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GUEST_PAINT_RECEIPT="
                           (.getPath (guest-paint-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GUEST_PAINT_LEFTOVER="
                           (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_GUEST_PAINT" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GUEST_PAINT_OK")
               (println (str "AIUEOS_COMPOSITOR_GUEST_PAINT not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-guest-input
       "Guest virtio-keyboard used-ring. C filling the envelope is red."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (guest-input-result {:qemu-unmeasured? true})]
             (write-guest-input-receipt!
              (assoc r :measured-at (str (java.time.Instant/now))
                     :command "clojure -M:compositor guest-input"
                     :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GUEST_INPUT leftover=:unmeasured")
             r)
           (let [boot (run-uefi-2d! {"AIUEOS_GUEST_INPUT" "1"})
                 measured? (serial-measured? (:serial boot))
                 r (guest-input-result {:serial (:serial boot)
                                        :qemu-unmeasured? (not measured?)
                                        :hosted-wm? false})
                 receipt (assoc r
                                :aiueos.compositor/guest-input-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor guest-input"
                                :profile :uefi-qemu-guest-input
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has GUEST_INPUT_OK eventq-used=1 synthetic=0. Hosted clojure -M:compositor wm does not count. C synthetic-smoke is leftover. Permission broker and native component runtime remain. P5 UNVERIFIED.")]
             (write-guest-input-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GUEST_INPUT_PROFILE=uefi-qemu")
             (println (str "AIUEOS_COMPOSITOR_GUEST_INPUT_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GUEST_INPUT_RECEIPT="
                           (.getPath (guest-input-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GUEST_INPUT_LEFTOVER="
                           (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_GUEST_INPUT" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GUEST_INPUT_OK")
               (println (str "AIUEOS_COMPOSITOR_GUEST_INPUT not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-guest-gpu-two
       "Guest two virtio-gpu 2D resources. Kotoba admits count; C hardcoding is red."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (guest-gpu-two-result {:qemu-unmeasured? true})]
             (write-guest-gpu-two-receipt!
              (assoc r :measured-at (str (java.time.Instant/now))
                     :command "clojure -M:compositor guest-gpu-two"
                     :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GUEST_GPU_TWO leftover=:unmeasured")
             r)
           (let [boot (run-uefi-2d!)
                 measured? (serial-measured? (:serial boot))
                 r (guest-gpu-two-result {:serial (:serial boot)
                                          :qemu-unmeasured? (not measured?)
                                          :hosted-wm? false})
                 receipt (assoc r
                                :aiueos.compositor/guest-gpu-two-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor guest-gpu-two"
                                :profile :uefi-qemu-guest-gpu-two
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has GUEST_GPU_TWO_OK resources=2 flush=2 kotoba-n=2. Hosted clojure -M:compositor wm does not count. One resource when Kotoba admits two is leftover. One scanout remains. P5 UNVERIFIED.")]
             (write-guest-gpu-two-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GUEST_GPU_TWO_PROFILE=uefi-qemu")
             (println (str "AIUEOS_COMPOSITOR_GUEST_GPU_TWO_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GUEST_GPU_TWO_RECEIPT="
                           (.getPath (guest-gpu-two-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GUEST_GPU_TWO_LEFTOVER="
                           (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_GUEST_GPU_TWO" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GUEST_GPU_TWO_OK")
               (println (str "AIUEOS_COMPOSITOR_GUEST_GPU_TWO not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-guest-scanout-two
       "Guest two virtio-gpu scanouts. Kotoba admits bind count; C hardcoding is red."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           (let [r (guest-scanout-two-result {:qemu-unmeasured? true})]
             (write-guest-scanout-two-receipt!
              (assoc r :measured-at (str (java.time.Instant/now))
                     :command "clojure -M:compositor guest-scanout-two"
                     :why :smoke-script-missing))
             (println "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO leftover=:unmeasured")
             r)
           ;; QEMU 10.1 virtio-gpu enables output 0 only at realize.
           ;; Extra heads need a UI frontend ui_info (cocoa/gtk/dbus).
           ;; `-display none` is leftover :one-scanout, not a Kotoba miss.
           (let [boot (run-uefi-2d!
                       {"AIUEOS_GUEST_SCANOUT_TWO" "1"
                        "AIUEOS_QEMU_TIMEOUT" "90"})
                 measured? (serial-measured? (:serial boot))
                 r (guest-scanout-two-result {:serial (:serial boot)
                                              :qemu-unmeasured? (not measured?)
                                              :hosted-wm? false})
                 receipt (assoc r
                                :aiueos.compositor/guest-scanout-two-receipt 1
                                :measured-at (str (java.time.Instant/now))
                                :command "clojure -M:compositor guest-scanout-two"
                                :profile :uefi-qemu-guest-scanout-two
                                :uefi-smoke-exit (:exit boot)
                                :serial-path (:serial-path boot)
                                :note "Green only when guest serial has GUEST_SCANOUT_TWO_OK scanouts=2 resource-0=1 resource-1=2 kotoba-n=2. Hosted clojure -M:compositor wm does not count. One scanout when Kotoba admits two is leftover. P5 UNVERIFIED.")]
             (write-guest-scanout-two-receipt! receipt)
             (println "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO_PROFILE=uefi-qemu")
             (println (str "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO_SERIAL=" (:serial-path boot)))
             (println (str "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO_RECEIPT="
                           (.getPath (guest-scanout-two-receipt-file))))
             (println (str "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO_LEFTOVER="
                           (pr-str (:leftover r))))
             (when (:serial boot)
               (doseq [line (str/split-lines (:serial boot))
                       :when (re-find #"AIUEOS_GUEST_SCANOUT_TWO" line)]
                 (println (str/replace line #"\r$" ""))))
             (if (:green? r)
               (println "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO_OK")
               (println (str "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO not-green leftover="
                             (pr-str (:leftover r)))))
             r))))

     (defn run-wm
       "Hosted window manager. No QEMU. Red if one surface or raise is a no-op."
       [{:keys [dir]}]
       (let [dir (io/file (or dir (str (System/getProperty "java.io.tmpdir")
                                       "/aiueos-compositor-wm")))
             http (atom nil)
             px (:x desktop/overlap-point)
             py (:y desktop/overlap-point)]
         (try
           (io/make-parents (desktop-file dir))
           (when (.isFile (desktop-file dir))
             (.delete (desktop-file dir)))
           (let [d (desktop/boot-desktop)
                 _ (persist! dir d)
                 two? (desktop/wm-admitted? d)
                 one-red? (not (desktop/wm-admitted? (desktop/one-surface-desktop)))
                 boot-hit (desktop/hit-window d px py)
                 key-hit (desktop/key-order-hit d px py)
                 z-not-keys? (and (some? boot-hit) (not= boot-hit key-hit))
                 zs-front (desktop/z-front d)
                 back (desktop/z-back d)
                 raised (desktop/raise d back)
                 raised-front (desktop/z-front raised)
                 raise-ok? (and (= back raised-front)
                                (not= zs-front raised-front))
                 occ? (desktop/occluded-at? raised zs-front px py)
                 [_ ev] (desktop/route-pointer raised px py)
                 route-ok? (and (= back (:hit ev))
                                (= [:panel back] (:input-target ev)))
                 rt (pb/start-http! (make-compositor-runtime dir))
                 _ (reset! http rt)
                 base (pb/base-url rt)
                 page (pb/http-get (str base "/"))
                 html (:body page)
                 spa? (and (= 200 (:code page)) (html-has-wm-face? html))
                 api (pb/http-get (str base "/api/compositor/desktop"))
                 api-ok? (boolean
                          (and (= 200 (:code api))
                               (str/includes? (:body api) "window-session-state")
                               (str/includes? (:body api) "guest-surface")
                               (str/includes? (:body api) "\"wm?\":true")))
                 raise-http (pb/http-post (str base "/api/compositor/raise")
                                          {:id back})
                 raise-parsed (:parsed raise-http)
                 raise-http-ok? (and (= 200 (:code raise-http))
                                     (= back (:front raise-parsed))
                                     (= back (:focused raise-parsed)))
                 ptr (pb/http-post (str base "/api/compositor/pointer")
                                   {:x px :y py})
                 ptr-ok? (boolean
                          (and (= 200 (:code ptr))
                               (= back (:hit (:parsed ptr)))
                               (str/includes? (str (:input-target (:parsed ptr))) "panel:")))
                 ime (desktop/ime-leftover d)
                 green? (and two? one-red? z-not-keys? raise-ok? occ? route-ok?
                             spa? api-ok? raise-http-ok? ptr-ok?)]
             (println (str "AIUEOS_COMPOSITOR_URL=" base "/#desktop"))
             (println (str "AIUEOS_COMPOSITOR_WM_SPA=" (if spa? "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_WM_SURFACES="
                           (if two? "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_WM_ONE_SURFACE="
                           (if one-red? "refused-as-required" "falsely-admitted")))
             (println (str "AIUEOS_COMPOSITOR_WM_ZORDER="
                           (if (and z-not-keys? raise-ok? occ?)
                             "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_WM_INPUT="
                           (if (and route-ok? ptr-ok?) "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_WM_DECORATION="
                           (if spa? "jp-go-dds" "missing")))
             (println (str "AIUEOS_COMPOSITOR_WM_IME leftover=:"
                           (name (:leftover ime))))
             (println (str "AIUEOS_COMPOSITOR_WM_RAISE_HTTP="
                           (if raise-http-ok? "admitted" "refused")))
             (when green? (println "AIUEOS_COMPOSITOR_WM_OK"))
             (when-not green?
               (println (str "AIUEOS_COMPOSITOR_WM_FAIL"
                             " two=" two?
                             " one-red=" one-red?
                             " z-not-keys=" z-not-keys?
                             " raise=" raise-ok?
                             " occ=" occ?
                             " route=" route-ok?
                             " spa=" spa?
                             " api=" api-ok?
                             " raise-http=" raise-http-ok?
                             " ptr=" ptr-ok?)))
             {:exit (if green? 0 1)
              :green? green?})
           (finally
             (when-let [rt @http] (pb/stop-http! rt))))))

     (defn run-ime
       "Hosted IME. No QEMU. Red if IME-on leaks latin `ka` to the guest."
       [{:keys [dir]}]
       (let [dir (io/file (or dir (str (System/getProperty "java.io.tmpdir")
                                       "/aiueos-compositor-ime")))
             http (atom nil)]
         (try
           (io/make-parents (desktop-file dir))
           (when (.isFile (desktop-file dir))
             (.delete (desktop-file dir)))
           (let [d (desktop/boot-desktop)
                 _ (persist! dir d)
                 rt (pb/start-http! (make-compositor-runtime dir))
                 _ (reset! http rt)
                 base (pb/base-url rt)
                 page (pb/http-get (str base "/"))
                 html (:body page)
                 spa? (and (= 200 (:code page)) (html-has-ime-face? html))
                 api (pb/http-get (str base "/api/compositor/desktop"))
                 api-ok? (boolean
                          (and (= 200 (:code api))
                               (str/includes? (:body api) "\"ime?\":true")
                               (str/includes? (:body api) "native-compositor-absent")))
                 k1 (pb/http-post (str base "/api/compositor/key") {:key "k"})
                 k2 (pb/http-post (str base "/api/compositor/key") {:key "a"})
                 ent (pb/http-post (str base "/api/compositor/key") {:key "Enter"})
                 on-k (and (= 200 (:code k1))
                           (true? (:consumed? (:parsed k1)))
                           (= "" (or (:guest-text (:parsed k1)) "")))
                 on-a (and (= 200 (:code k2))
                           (true? (:consumed? (:parsed k2)))
                           (= "" (or (:guest-text (:parsed k2)) ""))
                           (= "か" (or (:preedit (:parsed k2)) "")))
                 on-commit (and (= 200 (:code ent))
                                (= "か" (or (:guest-text (:parsed ent)) ""))
                                (str/includes? (str (:committed (:parsed ent))) "か"))
                 leaked? (or (true? (:latin-leaked? (:parsed k1)))
                             (true? (:latin-leaked? (:parsed k2)))
                             (re-find #"[a-zA-Z]" (str (:guest-text (:parsed k1))))
                             (re-find #"[a-zA-Z]" (str (:guest-text (:parsed k2)))))
                 off (pb/http-post (str base "/api/compositor/ime") {:on? false})
                 o1 (pb/http-post (str base "/api/compositor/key") {:key "k"})
                 o2 (pb/http-post (str base "/api/compositor/key") {:key "a"})
                 off-ok? (and (= 200 (:code off))
                              (false? (:on? (:parsed off)))
                              (= 200 (:code o1))
                              (false? (:consumed? (:parsed o1)))
                              (= "k" (or (:guest-text (:parsed o1)) ""))
                              (= 200 (:code o2))
                              (= "a" (or (:guest-text (:parsed o2)) ""))
                              (str/includes? (str (:guest-log (:parsed o2))) "ka"))
                 green? (and spa? api-ok? on-k on-a on-commit (not leaked?) off-ok?)]
             (println (str "AIUEOS_COMPOSITOR_URL=" base "/#desktop"))
             (println (str "AIUEOS_COMPOSITOR_IME_SPA=" (if spa? "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_IME_ON="
                           (if (and on-k on-a on-commit (not leaked?))
                             "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_IME_BYPASS="
                           (if off-ok? "refused-as-required" "falsely-consumed")))
             (println (str "AIUEOS_COMPOSITOR_IME_LEFTOVER=:native-compositor-absent"))
             (when green? (println "AIUEOS_COMPOSITOR_IME_OK"))
             (when-not green?
               (println (str "AIUEOS_COMPOSITOR_IME_FAIL"
                             " spa=" spa?
                             " api=" api-ok?
                             " on-k=" on-k
                             " on-a=" on-a
                             " commit=" on-commit
                             " leaked=" (boolean leaked?)
                             " off=" off-ok?
                             " k1=" (pr-str (:parsed k1))
                             " k2=" (pr-str (:parsed k2))
                             " ent=" (pr-str (:parsed ent))
                             " o1=" (pr-str (:parsed o1))
                             " o2=" (pr-str (:parsed o2)))))
             {:exit (if green? 0 1)
              :green? green?})
           (finally
             (when-let [rt @http] (pb/stop-http! rt))))))

     (defn run-kanji
       "Hosted kanji. No QEMU. Red if Space commits kana while か is in
       the dictionary, or if the SPA has no #ime-candidates."
       [{:keys [dir]}]
       (let [dir (io/file (or dir (str (System/getProperty "java.io.tmpdir")
                                       "/aiueos-compositor-kanji")))
             http (atom nil)]
         (try
           (io/make-parents (desktop-file dir))
           (when (.isFile (desktop-file dir))
             (.delete (desktop-file dir)))
           (let [d (desktop/boot-desktop)
                 red (desktop/kana-only-desktop)
                 _ (persist! dir d)
                 rt (pb/start-http! (make-compositor-runtime dir))
                 _ (reset! http rt)
                 base (pb/base-url rt)
                 page (pb/http-get (str base "/"))
                 html (:body page)
                 spa? (and (= 200 (:code page)) (html-has-kanji-face? html))
                 api (pb/http-get (str base "/api/compositor/desktop"))
                 api-ok? (boolean
                          (and (= 200 (:code api))
                               (str/includes? (:body api) "\"ime?\":true")
                               (str/includes? (:body api) "native-compositor-absent")))
                 red-path (let [d1 (first (desktop/route-key red "k"))
                                d2 (first (desktop/route-key d1 "a"))
                                [d3 e3] (desktop/route-key d2 "Space")]
                            (and (= "か" (get-in d2 [:ime :preedit]))
                                 (= "か" (:guest-text e3))
                                 (= :kanji-absent (:reason e3))
                                 (not (desktop/kanji-admitted? d3))))
                 k1 (pb/http-post (str base "/api/compositor/key") {:key "k"})
                 k2 (pb/http-post (str base "/api/compositor/key") {:key "a"})
                 sp (pb/http-post (str base "/api/compositor/key") {:key "Space"})
                 ent (pb/http-post (str base "/api/compositor/key") {:key "Enter"})
                 on-k (and (= 200 (:code k1))
                           (true? (:consumed? (:parsed k1)))
                           (= "" (or (:guest-text (:parsed k1)) "")))
                 on-a (and (= 200 (:code k2))
                           (= "か" (or (:preedit (:parsed k2)) "")))
                 on-convert (and (= 200 (:code sp))
                                 (= "加" (or (:preedit (:parsed sp)) ""))
                                 (= "" (or (:guest-text (:parsed sp)) "")))
                 on-commit (and (= 200 (:code ent))
                                (= "加" (or (:guest-text (:parsed ent)) ""))
                                (str/includes? (str (:committed (:parsed ent))) "加"))
                 leaked? (or (true? (:latin-leaked? (:parsed k1)))
                             (true? (:latin-leaked? (:parsed k2)))
                             (true? (:latin-leaked? (:parsed sp)))
                             (re-find #"[a-zA-Z]" (str (:guest-text (:parsed k1))))
                             (re-find #"[a-zA-Z]" (str (:guest-text (:parsed k2))))
                             (re-find #"[a-zA-Z]" (str (:guest-text (:parsed sp)))))
                 green? (and spa? api-ok? red-path on-k on-a on-convert on-commit
                             (not leaked?))]
             (println (str "AIUEOS_COMPOSITOR_URL=" base "/#desktop"))
             (println (str "AIUEOS_COMPOSITOR_KANJI_SPA="
                           (if spa? "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_KANJI_ON="
                           (if (and on-k on-a on-convert on-commit (not leaked?))
                             "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_KANJI_ABSENT="
                           (if red-path "refused-as-required" "falsely-converted")))
             (println (str "AIUEOS_COMPOSITOR_KANJI_LEFTOVER=:native-compositor-absent"))
             (when green? (println "AIUEOS_COMPOSITOR_KANJI_OK"))
             (when-not green?
               (println (str "AIUEOS_COMPOSITOR_KANJI_FAIL"
                             " spa=" spa?
                             " api=" api-ok?
                             " red=" red-path
                             " on-k=" on-k
                             " on-a=" on-a
                             " convert=" on-convert
                             " commit=" on-commit
                             " leaked=" (boolean leaked?)
                             " k1=" (pr-str (:parsed k1))
                             " k2=" (pr-str (:parsed k2))
                             " sp=" (pr-str (:parsed sp))
                             " ent=" (pr-str (:parsed ent)))))
             {:exit (if green? 0 1)
              :green? green?})
           (finally
             (when-let [rt @http] (pb/stop-http! rt))))))

     (defn- presenter-js
       []
       (let [f (io/file "apps/session/kami-presenter.js")]
         (when (.isFile f) (slurp f))))

     (defn- frame-field
       [frame k]
       (let [r (or (:result frame) frame)]
         (cond
           (nil? r) nil
           (map? r) (or (get r k) (get r (keyword k)))
           (instance? java.util.Map r) (.get ^java.util.Map r k)
           :else nil)))

     (defn- frame-admitted?
       [frame]
       (let [presenter (str (or (frame-field frame "presenter") ""))
             executor (str (or (frame-field frame "executor") ""))
             engine (str (or (frame-field frame "engine") ""))
             outcome (str (or (frame-field frame "outcome") ""))
             instances (frame-field frame "instances")
             n (if (number? instances) (long instances) 0)]
         (and (= presenter "function")
              (= executor "kami.webgpu")
              (= engine "kami.webgpu")
              (= outcome "admitted")
              (>= n 1))))

     (defn run-kami
       "Hosted kami.webgpu presenter. No QEMU. Clear-only sky fill is red.
       Guest virtio-gpu 2D stays `gpu`. Guest IME leftover."
       [{:keys [dir]}]
       (let [dir (io/file (or dir (str (System/getProperty "java.io.tmpdir")
                                       "/aiueos-compositor-kami")))
             http (atom nil)
             js (presenter-js)
             ir-ok? (desktop/kami-admitted? desktop/kami-session-ir)
             red-ok? (not (desktop/kami-admitted? desktop/clear-only-ir))
             bundle-ok? (presenter-is-kami-webgpu? js)]
         (try
           (io/make-parents (desktop-file dir))
           (when (.isFile (desktop-file dir))
             (.delete (desktop-file dir)))
           (let [d (desktop/boot-desktop)
                 _ (persist! dir d)
                 rt (pb/start-http! (make-compositor-runtime dir))
                 _ (reset! http rt)
                 base (pb/base-url rt)
                 page (pb/http-get (str base "/"))
                 html (:body page)
                 spa? (and (= 200 (:code page)) (html-has-kami-face? html))
                 bundle (pb/http-get (str base "/kami-presenter.js")
                                     {:read-timeout-ms 30000})
                 bundle-served? (and (= 200 (:code bundle))
                                     (presenter-is-kami-webgpu? (:body bundle)))
                 api (pb/http-get (str base "/api/compositor/desktop"))
                 api-ok? (boolean
                          (and (= 200 (:code api))
                               (re-find #"kami.webgpu.ir" (:body api))
                               (re-find #"instances" (:body api))))
                 floor? (and spa? bundle-ok? bundle-served? ir-ok? red-ok? api-ok?)
                 frame (when floor?
                         (try
                           (let [eval-frame (requiring-resolve
                                             'aiueos.compositor.kami-browser/eval-frame)]
                             (eval-frame base))
                           (catch Throwable e
                             {:unmeasured true :reason (str (.getMessage e))})))
                 measured? (and (map? frame) (not (:unmeasured frame)) (:ok frame))
                 drawn? (and measured? (frame-admitted? frame))
                 outcome (str (or (frame-field frame "outcome") ""))
                 leftover (cond
                            (not floor?) :clear-only-desktop
                            (not measured?) :unmeasured
                            (= outcome "unmeasured") :unmeasured
                            (not drawn?) :clear-only-desktop
                            :else :native-compositor-absent)
                 exit (cond
                        (not floor?) 1
                        (or (not measured?) (= leftover :unmeasured)) 3
                        (not drawn?) 1
                        :else 0)
                 green? (zero? exit)]
             (println (str "AIUEOS_COMPOSITOR_URL=" base "/#desktop"))
             (println (str "AIUEOS_COMPOSITOR_KAMI_SPA="
                           (if spa? "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_KAMI_BUNDLE="
                           (if (and bundle-ok? bundle-served?) "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_KAMI_IR="
                           (if (and ir-ok? red-ok?) "admitted" "refused")))
             (println (str "AIUEOS_COMPOSITOR_KAMI_FRAME="
                           (cond
                             drawn? "admitted"
                             (not measured?) "unmeasured"
                             :else "refused")))
             (println (str "AIUEOS_COMPOSITOR_KAMI_LEFTOVER=:" (name leftover)))
             (when green? (println "AIUEOS_COMPOSITOR_KAMI_OK"))
             (when-not green?
               (println (str "AIUEOS_COMPOSITOR_KAMI_FAIL"
                             " spa=" spa?
                             " bundle=" bundle-ok?
                             " served=" bundle-served?
                             " ir=" ir-ok?
                             " red=" red-ok?
                             " api=" api-ok?
                             " frame=" (pr-str frame))))
             {:exit exit :green? green? :leftover leftover})
           (finally
             (when-let [rt @http] (pb/stop-http! rt))))))

     (defn -main
       [& args]
       (let [cmd (or (first args) "smoke")
             dir (or (System/getenv "AIUEOS_COMPOSITOR_DIR")
                     (.getPath (java.io.File. (System/getProperty "java.io.tmpdir")
                                              "aiueos-compositor")))]
         (case cmd
           "serve"
           (let [rt (pb/start-http! (make-compositor-runtime dir))]
             (persist! dir @(:desktop rt))
             (pb/print-chassis! rt)
             (println "listening" (pb/base-url rt) "#desktop compositor Ctrl-C to stop")
             (.addShutdownHook (Runtime/getRuntime)
                               (Thread. (fn []
                                          (when-let [q @(:qemu rt)] (pb/stop-qemu! q))
                                          (pb/stop-http! rt))))
             @(promise))

           ("smoke" "check")
           (let [r (run-smoke {:dir dir})]
             (flush)
             (System/exit (int (:exit r))))

           "gpu"
           (let [r (run-gpu)]
             (flush)
             (System/exit (int (:exit r))))

           "wm"
           (let [r (run-wm {:dir (or (System/getenv "AIUEOS_COMPOSITOR_WM_DIR")
                                     (.getPath (java.io.File.
                                                (System/getProperty "java.io.tmpdir")
                                                "aiueos-compositor-wm")))})]
             (flush)
             (System/exit (int (:exit r))))

           "ime"
           (let [r (run-ime {:dir (or (System/getenv "AIUEOS_COMPOSITOR_IME_DIR")
                                      (.getPath (java.io.File.
                                                 (System/getProperty "java.io.tmpdir")
                                                 "aiueos-compositor-ime")))})]
             (flush)
             (System/exit (int (:exit r))))

           "kanji"
           (let [r (run-kanji {:dir (or (System/getenv "AIUEOS_COMPOSITOR_KANJI_DIR")
                                        (.getPath (java.io.File.
                                                   (System/getProperty "java.io.tmpdir")
                                                   "aiueos-compositor-kanji")))})]
             (flush)
             (System/exit (int (:exit r))))

           "kami"
           (let [r (run-kami {:dir (or (System/getenv "AIUEOS_COMPOSITOR_KAMI_DIR")
                                       (.getPath (java.io.File.
                                                  (System/getProperty "java.io.tmpdir")
                                                  "aiueos-compositor-kami")))})]
             (flush)
             (System/exit (int (:exit r))))

           "guest-ime"
           (let [r (run-guest-ime)]
             (flush)
             (System/exit (int (:exit r))))

           "guest-wm"
           (let [r (run-guest-wm)]
             (flush)
             (System/exit (int (:exit r))))

           "guest-paint"
           (let [r (run-guest-paint)]
             (flush)
             (System/exit (int (:exit r))))

           "guest-input"
           (let [r (run-guest-input)]
             (flush)
             (System/exit (int (:exit r))))

           "guest-gpu-two"
           (let [r (run-guest-gpu-two)]
             (flush)
             (System/exit (int (:exit r))))

           "guest-scanout-two"
           (let [r (run-guest-scanout-two)]
             (flush)
             (System/exit (int (:exit r))))

           (do (println "usage: clojure -M:compositor [smoke|gpu|wm|ime|kanji|kami|guest-ime|guest-wm|guest-paint|guest-input|guest-gpu-two|guest-scanout-two|serve]")
               (System/exit 3)))))))
