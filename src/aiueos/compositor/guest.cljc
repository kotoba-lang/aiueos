(ns aiueos.compositor.guest
  "KERNEL.ELF guest compositor serial classifiers (ADR-0100).

  Portable. nbb loads this namespace without kami / phone-bind / JVM.
  Hosted `clojure -M:compositor wm` / `ime` remain the named reds.
  Evidence command is `nbb --classpath src scripts/compositor-guest.cljs`
  plus a guest profile. QEMU/firmware/serial unanswered is exit 3."
  (:require [clojure.string :as str]))

(def gate-prefix
  "nbb --classpath src scripts/compositor-guest.cljs")

(def guest-profiles
  ["guest-ime" "guest-wm" "guest-paint" "guest-input"
   "guest-gpu-two" "guest-scanout-two" "guest-broker" "guest-session"])

(defn gate-cmd
  "SPA-visible evidence command for a guest profile. Not clojure -M."
  [profile]
  (str gate-prefix " " profile))

(defn serial-measured?
  [serial]
  (boolean (and (string? serial) (re-find #"AIUEOS_" serial))))

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

(defn guest-broker-ok?
  "True only when KERNEL.ELF serial says clipboard was admitted and
  file-picker refused after Kotoba. Hosted JVM wm serial alone does
  not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_BROKER_OK clipboard=1 picker=0 kotoba-clip=1 kotoba-pick=0" serial)
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-broker-result
  "Classify a KERNEL.ELF boot for compositor `guest-broker`.
  Hosted `clojure -M:compositor wm` is the named red. Picker admitted
  on a clipboard-only grant is leftover `:always-grant`."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-broker-ok? serial)
    {:green? true :exit 0 :reason :guest-broker-admitted :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_BROKER leftover=always-grant" (or serial ""))
    {:green? false :exit 1 :reason :always-grant :leftover [:always-grant]}

    (re-find #"(?m)^AIUEOS_GUEST_BROKER leftover=deny-all" (or serial ""))
    {:green? false :exit 1 :reason :deny-all :leftover [:deny-all]}

    :else
    {:green? false :exit 1 :reason :guest-broker-absent
     :leftover [:guest-broker-absent]}))

(defn guest-session-ok?
  "True only when KERNEL.ELF serial says packed front 2 restored and
  wm-hit used that front. Hosted JVM wm serial alone does not count."
  [serial]
  (boolean
   (and (string? serial)
        (re-find #"(?m)^AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2" serial)
        (not (re-find #"AIUEOS_COMPOSITOR_WM_OK" serial)))))

(defn guest-session-result
  "Classify a KERNEL.ELF boot for compositor `guest-session`.
  Hosted `clojure -M:compositor wm` is the named red. Restore that
  returns 2 for packed 0 is leftover `:always-front`."
  [{:keys [serial qemu-unmeasured? hosted-wm?]}]
  (cond
    qemu-unmeasured?
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}

    (or hosted-wm?
        (re-find #"AIUEOS_COMPOSITOR_WM_OK" (or serial "")))
    {:green? false :exit 1 :reason :hosted-wm-does-not-count
     :leftover [:hosted-wm-does-not-count]}

    (guest-session-ok? serial)
    {:green? true :exit 0 :reason :guest-session-admitted :leftover []}

    (re-find #"(?m)^AIUEOS_GUEST_SESSION leftover=always-front" (or serial ""))
    {:green? false :exit 1 :reason :always-front :leftover [:always-front]}

    (re-find #"(?m)^AIUEOS_GUEST_SESSION leftover=empty-session" (or serial ""))
    {:green? false :exit 1 :reason :empty-session :leftover [:empty-session]}

    (re-find #"(?m)^AIUEOS_GUEST_SESSION leftover=unknown-surface" (or serial ""))
    {:green? false :exit 1 :reason :unknown-surface :leftover [:unknown-surface]}

    (re-find #"(?m)^AIUEOS_GUEST_SESSION leftover=restore-ignored" (or serial ""))
    {:green? false :exit 1 :reason :restore-ignored :leftover [:restore-ignored]}

    :else
    {:green? false :exit 1 :reason :guest-session-absent
     :leftover [:guest-session-absent]}))


(defn extra-env
  "Env merged into smoke-qemu-uefi.sh. Default UEFI smoke is empty."
  [profile]
  (case profile
    "guest-input" {"AIUEOS_GUEST_INPUT" "1"}
    "guest-scanout-two" {"AIUEOS_GUEST_SCANOUT_TWO" "1"
                         "AIUEOS_QEMU_TIMEOUT" "90"}
    nil))

(defn classify
  [profile opts]
  (case profile
    "guest-ime" (guest-ime-result opts)
    "guest-wm" (guest-wm-result opts)
    "guest-paint" (guest-paint-result opts)
    "guest-input" (guest-input-result opts)
    "guest-gpu-two" (guest-gpu-two-result opts)
    "guest-scanout-two" (guest-scanout-two-result opts)
    "guest-broker" (guest-broker-result opts)
    "guest-session" (guest-session-result opts)
    {:green? false :exit 3 :reason :unmeasured :leftover [:unmeasured]}))

(defn serial-marker
  [profile]
  (case profile
    "guest-ime" "AIUEOS_GUEST_IME"
    "guest-wm" "AIUEOS_GUEST_WM"
    "guest-paint" "AIUEOS_GUEST_PAINT"
    "guest-input" "AIUEOS_GUEST_INPUT"
    "guest-gpu-two" "AIUEOS_GUEST_GPU_TWO"
    "guest-scanout-two" "AIUEOS_GUEST_SCANOUT_TWO"
    "guest-broker" "AIUEOS_GUEST_BROKER"
    "guest-session" "AIUEOS_GUEST_SESSION"
    nil))

(defn ok-print
  [profile]
  (case profile
    "guest-ime" "AIUEOS_COMPOSITOR_GUEST_IME_OK"
    "guest-wm" "AIUEOS_COMPOSITOR_GUEST_WM_OK"
    "guest-paint" "AIUEOS_COMPOSITOR_GUEST_PAINT_OK"
    "guest-input" "AIUEOS_COMPOSITOR_GUEST_INPUT_OK"
    "guest-gpu-two" "AIUEOS_COMPOSITOR_GUEST_GPU_TWO_OK"
    "guest-scanout-two" "AIUEOS_COMPOSITOR_GUEST_SCANOUT_TWO_OK"
    "guest-broker" "AIUEOS_COMPOSITOR_GUEST_BROKER_OK"
    "guest-session" "AIUEOS_COMPOSITOR_GUEST_SESSION_OK"
    nil))
