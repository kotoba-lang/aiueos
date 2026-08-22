(ns aiueos.compositor
  "Named partial desktop face (root ADR-2608221625 compositor unit).

  The same `apps/session` DADS SPA is the shell. This process owns
  window-session-state surfaces and persists them. QEMU is started with
  `-device virtio-gpu-pci` so a display session exists; `-display none`
  keeps P1b's no-keyboard bind path. This is not a window manager
  (no IME, no virtio-gpu 2D create/flush).

  Exit 0 = SPA served, compositor owns surfaces, restore admitted,
  wiped restore refused, QEMU virtio-gpu present.
  Exit 1 = refused (HTTP-only `#session` with no surfaces is red).
  Exit 3 = QEMU/firmware could not be answered.

  Command: `clojure -M:compositor smoke`"
  (:require [aiueos.compositor.desktop :as desktop]
            [aiueos.phone-bind :as pb]
            [clojure.string :as str]
            #?(:clj [clojure.edn :as edn])
            #?(:clj [clojure.java.io :as io])))

(defn gpu-argv?
  [argv]
  (boolean (and (some #{"virtio-gpu-pci"} argv)
                (pb/headless-argv? argv))))

(defn pci-names-gpu?
  "QEMU query-pci does not echo the qdev name virtio-gpu-pci.
  Measured on qemu-system-aarch64 10.1 virt + virtio-gpu-pci: class 896
  desc Display controller, virtio device id 4176 (0x1050)."
  [qmp-body]
  (boolean
   (and (string? qmp-body)
        (or (str/includes? qmp-body "virtio-gpu")
            (str/includes? qmp-body "virtio-vga")
            (str/includes? qmp-body "Display controller")
            (str/includes? qmp-body "VGA controller")
            (str/includes? qmp-body "\"device\": 4176")
            (str/includes? qmp-body "\"device\":4176")))))

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
           (edn/read-string (slurp f))
           (desktop/empty-desktop))))

     (defn boot-or-restore
       "Restore when a desktop file exists; otherwise open the default surfaces.
       A missing file is a wipe, not a successful empty session."
       [dir]
       (if (.isFile (desktop-file dir))
         (load-persisted dir)
         (desktop/boot-desktop)))

     (defn start-gpu-qemu!
       "Firmware-only aarch64 virt with virtio-gpu-pci. Guest 2D resource
       create/flush is still not this unit (ADR-0009 / ADR-0013 Phase 6)."
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

           (do (println "usage: clojure -M:compositor [smoke|serve]")
               (System/exit 3)))))))
