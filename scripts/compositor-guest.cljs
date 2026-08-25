#!/usr/bin/env nbb
;; Guest KERNEL.ELF compositor gates without a JVM (ADR-0100).
;;
;; Evidence:
;;   nbb --classpath src scripts/compositor-guest.cljs guest-session
;;
;; Same UEFI smoke as the old `clojure -M:compositor guest-*` path
;; (`os/aiueos/scripts/smoke-qemu-uefi.sh`). Classification is
;; `aiueos.compositor.guest` — one implementation. Hosted
;; `clojure -M:compositor wm` serial does not count. QEMU/firmware/serial
;; unanswered is exit 3, not a silent pass.
;;
;; Hosted leftover after a green guest gate is still
;; `:native-compositor-absent` (native component runtime, P5).
;; JVM `clojure -M:compositor guest-*` is leftover `:jvm-gate-runner`.

(ns compositor-guest
  (:require ["node:fs" :as fs]
            ["node:path" :as path]
            ["node:child_process" :as cp]
            [clojure.string :as str]
            [aiueos.compositor.guest :as guest]))

(def repo (.resolve path (.dirname path *file*) ".."))
(def out (or (.-AIUEOS_OUT js/process.env) (.join path repo "build" "aiueos")))
(def smoke (.join path repo "os" "aiueos" "scripts" "smoke-qemu-uefi.sh"))
(def serial-path (.join path out "kernel-serial.log"))

(defn- die3 [msg]
  (binding [*out* *err*] (println msg))
  (.exit js/process 3))

(defn- profile-arg []
  (let [args (vec *command-line-args*)
        args (if (= "--" (first args)) (subvec args 1) args)]
    (or (first args) "guest-session")))

(defn- write-receipt! [profile receipt]
  (when-not (.existsSync fs out) (.mkdirSync fs out #js {:recursive true}))
  (let [f (.join path out (str "compositor-" profile "-receipt.edn"))]
    (.writeFileSync fs f (pr-str receipt))
    f))

(defn- run-uefi! [profile]
  (when-not (.existsSync fs smoke)
    (die3 (str "AIUEOS_COMPOSITOR leftover=:unmeasured why=:smoke-script-missing path=" smoke)))
  (let [env (js/Object.assign #js {} js/process.env)
        _ (doseq [[k v] (or (guest/extra-env profile) {})]
            (aset env k v))
        r (.spawnSync cp smoke #js []
                      #js {:cwd repo :encoding "utf8" :env env :shell false
                           :stdio #js ["ignore" "inherit" "inherit"]})
        serial (when (.existsSync fs serial-path)
                 (.readFileSync fs serial-path "utf8"))]
    {:exit (or (.-status r) 1)
     :serial serial
     :serial-path serial-path}))

(let [profile (profile-arg)]
  (when-not (some #{profile} guest/guest-profiles)
    (binding [*out* *err*]
      (println "usage: nbb --classpath src scripts/compositor-guest.cljs"
               (str/join "|" guest/guest-profiles)))
    (.exit js/process 3))
  (let [boot (run-uefi! profile)
        measured? (guest/serial-measured? (:serial boot))
        r (guest/classify profile {:serial (:serial boot)
                                   :qemu-unmeasured? (not measured?)
                                   :hosted-wm? false
                                   :hosted-ime? false})
        receipt (assoc r
                       :aiueos.compositor/nbb-guest-receipt 1
                       :measured-at (.toISOString (js/Date.))
                       :command (guest/gate-cmd profile)
                       :profile (keyword (str "uefi-qemu-" profile))
                       :uefi-smoke-exit (:exit boot)
                       :serial-path (:serial-path boot)
                       :note (str "Green only on KERNEL.ELF serial. Hosted clojure -M:compositor wm does not count. Native component runtime remains. P5 UNVERIFIED."))]
    (write-receipt! profile receipt)
    (println (str "AIUEOS_COMPOSITOR_" (str/replace (str/upper-case profile) "-" "_")
                  "_PROFILE=uefi-qemu-nbb"))
    (println (str "AIUEOS_COMPOSITOR_" (str/replace (str/upper-case profile) "-" "_")
                  "_SERIAL=" (:serial-path boot)))
    (println (str "AIUEOS_COMPOSITOR_" (str/replace (str/upper-case profile) "-" "_")
                  "_LEFTOVER=" (pr-str (:leftover r))))
    (when (:serial boot)
      (let [marker (guest/serial-marker profile)]
        (doseq [line (str/split-lines (:serial boot))
                :when (and marker (str/includes? line marker))]
          (println (str/replace line #"\r$" "")))))
    (if (:green? r)
      (println (guest/ok-print profile))
      (println (str "AIUEOS_COMPOSITOR_" (str/replace (str/upper-case profile) "-" "_")
                    " not-green leftover=" (pr-str (:leftover r)))))
    (.exit js/process (int (:exit r)))))
