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
  the focused guest. IME is leftover. Guest 2D stays `gpu`.

  Exit 0 = the named command admitted. Exit 1 = refused. Exit 3 =
  QEMU/firmware/serial could not be answered.

  Commands: `clojure -M:compositor smoke` | `gpu` | `wm` | `serve`"
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

     (defn write-gpu-receipt!
       [receipt]
       (let [f (gpu-receipt-file)]
         (io/make-parents f)
         (spit f (with-out-str (pprint/pprint receipt)))
         f))

     (defn serial-measured?
       [serial]
       (boolean (and (string? serial) (re-find #"AIUEOS_" serial))))

     (defn run-uefi-2d!
       "Existing UEFI QEMU smoke (virtio-vga = virtio-gpu protocol).
       No AIUEOS_TEST_NET — that is P2 and not this gate. No new .sh."
       []
       (let [script (uefi-smoke)]
         (if-not (.isFile script)
           {:ok false :unmeasured true :reason :smoke-script-missing
            :tried (.getPath script)}
           (let [pb (doto (ProcessBuilder. ^java.util.List
                                           ["sh" (.getPath script)])
                      (.directory (repo-root))
                      (.inheritIO))
                 proc (.start pb)
                 exit (.waitFor proc)
                 serial (when (.isFile (kernel-serial-file))
                          (slurp (kernel-serial-file)))]
             {:ok (zero? exit)
              :exit exit
              :serial serial
              :serial-path (.getPath (kernel-serial-file))}))))

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
                 ime (desktop/ime-leftover)
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

           (do (println "usage: clojure -M:compositor [smoke|gpu|wm|serve]")
               (System/exit 3)))))))
