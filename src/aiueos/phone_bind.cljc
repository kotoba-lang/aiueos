(ns aiueos.phone-bind
  "Hosted proving slice for root ADR-2608221625 P1b / P1c.

  A Mac VM has no chassis sticker and no product device-agent in the guest
  (the bare-metal kernel cannot yet speak HTTP — that is P2). This helper is
  the **hosted stand-in**: it *is* the chassis printer, the NVRAM, and the
  device agent. It is not a second identity stack. Claims go through
  `grant.enroll`. Durable check-in in this demo writes a local ledger labelled
  `:non-authoritative`; production still names `https://kotobase.net`.

  The guest framebuffer is not an operator console. QEMU is started with
  `-display none`. The setup URL and QR payload are printed on the **host**
  terminal and written next to the VM as `setup.json`.

  GET / is the DADS document at apps/session (P1), not a temporary --hig-*
  face. Live kotobase/murakumo legs leave this process when the SPA posts
  /api/session/*. `clojure -M:cloud-live check` is a different gate.

  User-mode/slirp networking stands in for Ethernet DHCP. Wi-Fi Easy Connect
  is not this slice. Compositor and real-machine qualification are later
  units. Consumer smoke does not require itonami; the operator fragment is
  `/api/session/operator` and a separate command."
  (:require [clojure.string :as str]
            [grant.enroll :as enroll]
            #?(:clj [grant.signing :as signing])
            #?(:clj [clojure.edn :as edn])
            #?(:clj [clojure.java.io :as io])
            #?(:clj [aiueos.session.live :as session-live])
            #?(:clj [aiueos.session.guest :as session-guest])
            #?(:clj [aiueos.session.operator :as session-operator])
            #?(:clj [aiueos.compositor.desktop :as compositor-desktop]))
  #?(:clj
     (:import [com.sun.net.httpserver HttpExchange HttpHandler HttpServer]
              [java.net InetSocketAddress StandardProtocolFamily UnixDomainSocketAddress]
              [java.nio ByteBuffer]
              [java.nio.channels SocketChannel]
              [java.nio.charset StandardCharsets]
              [java.security KeyFactory KeyPairGenerator SecureRandom Signature]
              [java.security.spec PKCS8EncodedKeySpec X509EncodedKeySpec]
              [java.time Instant]
              [java.util.concurrent Executors TimeUnit])))

;; ── tiny JSON (closed SPA wire; no extra dep) ──────────────────────────────

(defn- json-escape [s]
  (-> (str s)
      (str/replace "\\" "\\\\")
      (str/replace "\"" "\\\"")
      (str/replace "\n" "\\n")))

(defn- json-key [k]
  (if (keyword? k) (subs (str k) 1) (str k)))

(defn ->json
  "Encode a closed set of EDN values the phone SPA understands.
  Namespaced keywords keep their namespace so `:aiueos/decision` and
  `:decision` do not collapse."
  [x]
  (cond
    (nil? x) "null"
    (true? x) "true"
    (false? x) "false"
    (number? x) (str x)
    (keyword? x) (str "\"" (json-escape (json-key x)) "\"")
    (string? x) (str "\"" (json-escape x) "\"")
    (map? x) (str "{"
                  (->> x
                       (map (fn [[k v]]
                              (str "\"" (json-escape (json-key k)) "\":"
                                   (->json v))))
                       (str/join ","))
                  "}")
    (or (sequential? x) (set? x)) (str "[" (str/join "," (map ->json x)) "]")
    :else (str "\"" (json-escape (str x)) "\"")))

(defn parse-flat-json
  "Parse a JSON object whose values are strings, numbers, or booleans.
  Enough for the SPA's POST bodies. Not a general parser."
  [s]
  (when (and (string? s) (re-find #"^\s*\{" s))
    (into {}
          (concat
           (for [[_ k v] (re-seq #"\"([^\"]+)\"\s*:\s*\"((?:\\.|[^\"\\])*)\"" s)]
             [(keyword k) #?(:clj (.replace ^String v "\\\"" "\"")
                             :cljs (.replace v "\\\"" "\""))])
           (for [[_ k v] (re-seq #"\"([^\"]+)\"\s*:\s*(true|false)" s)]
             [(keyword k) (= v "true")])
           (for [[_ k v] (re-seq #"\"([^\"]+)\"\s*:\s*(-?\d+)" s)]
             [(keyword k) #?(:clj (Long/parseLong v)
                             :cljs (js/parseInt v 10))])))))

(defn deny [reason extra]
  (merge {:aiueos/decision :deny
          :aiueos.enroll/reason reason}
         extra))

(defn bind-via
  "The only P1b-green path is `:phone-http`. A guest VGA/keyboard attempt is
  `:local-console-required` — that is the named red for 'operator used the
  guest keyboard'."
  [path]
  (case path
    :phone-http {:aiueos.phone-bind/path :phone-http}
    :guest-vga-keyboard
    (deny :local-console-required
          {:aiueos.phone-bind/path :guest-vga-keyboard
           :aiueos.phone-bind/note
           "P1b is red if the operator used the guest keyboard or VGA. Headless bind is HTTP on the host helper."})
    (deny :local-console-required
          {:aiueos.phone-bind/path path})))

(defn apply-phone-claim
  "Run `grant.enroll/claim` after the helper has checked possession."
  [device req]
  (let [path-v (bind-via (:path req :phone-http))]
    (if (= :deny (:aiueos/decision path-v))
      path-v
      (enroll/claim device
                    (select-keys req [:did :token :owner :now-ms :possession-proof-valid?])
                    enroll/default-policy))))

(defn consume-pre-grant
  "P1c: first check-in consumes a single-use nonce in the shared ledger, then
  `grant.enroll/claim` binds the machine. A copied grant file still has
  uses-remaining 1; the ledger is what stops the second VM."
  [ledger grant device]
  (cond
    (not= :pre-grant (:aiueos.enroll/kind grant))
    {:ledger ledger :grant grant :device device
     :verdict (deny :not-a-pre-grant {})}

    (contains? (:consumed-grant-nonces ledger) (:nonce grant))
    {:ledger ledger :grant grant :device device
     :verdict (deny :already-consumed
                    {:aiueos.enroll/nonce (:nonce grant)
                     :aiueos.enroll/consumed-by
                     (get-in ledger [:consumed-grant-nonces (:nonce grant)])})}

    (not (pos? (or (:uses-remaining grant) 0)))
    {:ledger ledger :grant grant :device device
     :verdict (deny :already-consumed {:aiueos.enroll/nonce (:nonce grant)})}

    :else
    (let [now-ms (or (:first-seen-ms device) 0)
          claim-v (enroll/claim
                   (assoc device :state :factory)
                   {:did (:did device)
                    :token (:token device)
                    :owner (:tenant grant)
                    :now-ms now-ms
                    :possession-proof-valid? true}
                   enroll/default-policy)]
      (if-not (enroll/granted? claim-v)
        {:ledger ledger :grant grant :device device :verdict claim-v}
        (let [now #?(:clj (str (java.time.Instant/now)) :cljs "now")
              device' (assoc device
                             :state :claimed
                             :owner (:tenant grant)
                             :bound-via :pre-grant
                             :bound-at now)
              grant' (assoc grant :uses-remaining 0
                            :consumed-by (:did device')
                            :consumed-at now)
              ledger' (-> ledger
                          (assoc-in [:consumed-grant-nonces (:nonce grant)] (:did device'))
                          (assoc-in [:devices (:did device')]
                                    {:did (:did device')
                                     :owner (:tenant grant)
                                     :model (:model device')
                                     :via :pre-grant
                                     :qr? false}))]
          {:ledger ledger'
           :grant grant'
           :device device'
           :verdict (assoc claim-v :aiueos.enroll/via :pre-grant)})))))

(defn headless-argv?
  "True only when the operator console is not a guest display."
  [argv]
  (let [v (vec argv)
        after-display (rest (drop-while #(not= % "-display") v))]
    (boolean
     (and (seq after-display)
          (= "none" (first after-display))
          (not (some #{"cocoa" "gtk" "sdl" "vnc"} v))))))

(defn chassis-argv-shape
  "Pure argv tail used by tests. Firmware/qemu paths are filled on the JVM.
  P1b default is `-display none` with no GPU. The compositor unit adds
  `-device virtio-gpu-pci` via `:graphics \"virtio-gpu\"` without cocoa."
  [{:keys [qemu firmware qmp serial accel memory graphics display]}]
  (let [accel (or accel "hvf")
        display (or display "none")
        graphics (or graphics "none")]
    (cond-> [(or qemu "qemu-system-aarch64")
             "-machine" (str "virt,accel=" accel)
             "-cpu" (if (= accel "tcg") "max" "host")
             "-m" (or memory "128")
             "-smp" "1"
             "-display" display
             "-serial" (str "file:" (or serial "guest-serial.log"))
             "-monitor" "none"
             "-qmp" (str "unix:" (or qmp "qemu.qmp") ",server,nowait")
             "-bios" (or firmware "edk2-aarch64-code.fd")
             "-netdev" "user,id=n0"
             "-device" "virtio-net-pci,netdev=n0"]
      (= graphics "virtio-gpu") (into ["-device" "virtio-gpu-pci"]))))

(defn session-html-source
  "apps/session/index.html (generated DADS document) or the classpath copy."
  []
  (let [here #?(:clj (io/file "apps/session/index.html") :cljs nil)
        res #?(:clj (io/resource "aiueos/session/index.html") :cljs nil)]
    #?(:clj (cond
              (and here (.isFile here)) here
              res res
              :else nil)
       :cljs nil)))

(defn session-html
  "The DADS SPA. Generated by apps/session/generate.cljs. Temporary
  proving-slice HTML is gone — this is P1."
  []
  #?(:clj
     (let [src (session-html-source)]
       (when-not src
         (throw (ex-info "apps/session/index.html missing; generate the DADS document first"
                         {:aiueos.session/error :document-missing})))
       (slurp src))
     :cljs ""))

(defn kami-presenter-js-source
  "apps/session/kami-presenter.js — kami.webgpu init!/draw!, not inlined."
  []
  (let [here #?(:clj (io/file "apps/session/kami-presenter.js") :cljs nil)
        res #?(:clj (io/resource "aiueos/session/kami-presenter.js") :cljs nil)]
    #?(:clj (cond
              (and here (.isFile here)) here
              res res
              :else nil)
       :cljs nil)))

(defn kami-presenter-js
  []
  #?(:clj
     (when-let [src (kami-presenter-js-source)]
       (slurp src))
     :cljs nil))


#?(:clj
   (do
     (defn- rand-hex [n-bytes]
       (let [buf (byte-array n-bytes)]
         (.nextBytes (SecureRandom.) buf)
         (signing/hex-encode buf)))

     (defn generate-device-keys
       "On-device operational key, hosted file-backed (ADR-2608221625). Not a
       cloud credential. JDK Ed25519, the same algorithm grant.signing verifies."
       []
       (let [kp (.generateKeyPair (KeyPairGenerator/getInstance "Ed25519"))]
         {:public-hex (signing/hex-encode (.getEncoded (.getPublic kp)))
          :private-hex (signing/hex-encode (.getEncoded (.getPrivate kp)))}))

     (defn sign-nonce
       [private-hex nonce]
       (let [kf (KeyFactory/getInstance "Ed25519")
             pk (.generatePrivate kf (PKCS8EncodedKeySpec.
                                      (byte-array (signing/hex-decode private-hex))))
             sig (doto (Signature/getInstance "Ed25519")
                   (.initSign pk)
                   (.update (.getBytes (str nonce) StandardCharsets/UTF_8)))]
         (signing/hex-encode (.sign sig))))

     (defn verify-nonce
       [public-hex nonce signature-hex]
       (try
         (let [kf (KeyFactory/getInstance "Ed25519")
               pub (.generatePublic kf (X509EncodedKeySpec.
                                        (byte-array (signing/hex-decode public-hex))))
               sig (doto (Signature/getInstance "Ed25519")
                     (.initVerify pub)
                     (.update (.getBytes (str nonce) StandardCharsets/UTF_8)))]
           (.verify sig (byte-array (signing/hex-decode signature-hex))))
         (catch Exception _ false)))

     (defn new-device-id []
       (str "did:aiueos:vm:" (rand-hex 16)))

     (defn empty-ledger
       []
       {:kotobase/authority "https://kotobase.net"
        :kotobase/path :non-authoritative
        :fixture? true
        :note "Mac VM demo ledger. Production check-in is kotobase.net. Not D1."
        :consumed-grant-nonces {}
        :devices {}})

     (defn load-edn [file default]
       (let [f (io/file file)]
         (if (.isFile f)
           (edn/read-string (slurp f))
           default)))

     (defn- spit-edn [file value]
       (let [f (io/file file)]
         (io/make-parents f)
         (spit f (pr-str value))))

     (defn load-ledger [path]
       (load-edn path (empty-ledger)))

     (defn save-ledger [path ledger]
       (spit-edn path (merge (empty-ledger) ledger)))

     (defn device-file [dir] (io/file dir "state" "device.edn"))
     (defn receipt-file [dir] (io/file dir "state" "bind-receipt.edn"))
     (defn pre-grant-file [dir] (io/file dir "image" "enroll-grant.edn"))
     (defn setup-json-file [dir] (io/file dir "setup.json"))
     (defn qmp-file [dir] (io/file dir "qemu.qmp"))
     (defn serial-file [dir] (io/file dir "guest-serial.log"))

     (defn issue-pre-grant
       "Attenuated enrollment capability written by the imager. No password, no
       long-lived cloud token. Single-use, tenant + allowed cloud only."
       [{:keys [tenant allowed-clouds]}]
       {:aiueos.enroll/kind :pre-grant
        :nonce (rand-hex 16)
        :tenant tenant
        :allowed-clouds (vec (or allowed-clouds ["https://kotobase.net"]))
        :uses-remaining 1
        :consumed-by nil
        :consumed-at nil})

     (defn write-pre-grant! [dir grant]
       (spit-edn (pre-grant-file dir) grant)
       grant)

     (defn mint-unbound-device
       "Factory-state device. Operational key is generated on first attest/check-in,
       not baked as a finished account key (ADR-2608221625)."
       [{:keys [did token model]}]
       {:did (or did (new-device-id))
        :state :factory
        :token (or token (rand-hex 16))
        :model (or model "aiueos-qemu-hosted")
        :attested? false
        :first-seen-ms (System/currentTimeMillis)
        :owner nil
        :public-hex nil
        :private-hex nil})

     (defn save-device [dir device]
       (spit-edn (device-file dir) device)
       device)

     (defn load-device [dir]
       (load-edn (device-file dir) nil))

     (defn ensure-operational-keys [device]
       (if (and (:public-hex device) (:private-hex device))
         device
         (merge device (generate-device-keys))))

     (defn setup-fields
       [device endpoint]
       {:did (:did device)
        :model (:model device)
        :endpoint endpoint
        :token (:token device)})

     (defn chassis-qr
       [device endpoint]
       (enroll/qr-payload (setup-fields device endpoint)))

     (defn setup-url [listen-port]
       (str "http://127.0.0.1:" listen-port "/#setup"))

     (defn write-setup-json!
       [dir {:keys [device endpoint setup-url qr]}]
       (let [body (->json {:did (:did device)
                           :model (:model device)
                           :endpoint endpoint
                           :setup_url setup-url
                           :qr qr
                           :token (:token device)
                           :chassis "host-helper"
                           :guest_framebuffer "not-an-operator-console"
                           :kotobase "https://kotobase.net"
                           :ledger "non-authoritative"})]
         (io/make-parents (setup-json-file dir))
         (spit (setup-json-file dir) body)
         body))

     (defn complete-bind
       [device {:keys [owner token nonce signature now-ms path]}]
       (let [device (ensure-operational-keys device)
             proof? (boolean (and nonce signature
                                  (verify-nonce (:public-hex device) nonce signature)))
             verdict (apply-phone-claim
                      device
                      {:path (or path :phone-http)
                       :did (:did device)
                       :token token
                       :owner owner
                       :now-ms (or now-ms (System/currentTimeMillis))
                       :possession-proof-valid? proof?})]
         {:device (cond-> device
                    (enroll/granted? verdict)
                    (assoc :state :claimed
                           :owner owner
                           :bound-at (str (Instant/now))))
          :verdict verdict
          :possession-proof-valid? proof?}))

     (defn record-claimed-device
       [ledger device]
       (assoc-in ledger [:devices (:did device)]
                 {:did (:did device)
                  :owner (:owner device)
                  :model (:model device)
                  :via :phone-http
                  :qr? true}))

     (defn bind-receipt
       [device verdict]
       {:did (:did device)
        :owner (:owner device)
        :state (:state device)
        :decision (:aiueos/decision verdict)
        :trust (:aiueos.enroll/trust verdict)
        :via (:aiueos.enroll/via verdict)
        :at (str (Instant/now))
        :kotobase "https://kotobase.net"
        :ledger :non-authoritative})

     (def firmware-candidates
       ["/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
        "/usr/share/qemu/edk2-aarch64-code.fd"])

     (defn find-firmware
       []
       (or (System/getenv "AIUEOS_QEMU_BIOS")
           (some #(when (.isFile (io/file %)) %) firmware-candidates)))

     (defn find-qemu
       []
       (or (System/getenv "AIUEOS_QEMU")
           (let [p "/opt/homebrew/bin/qemu-system-aarch64"]
             (when (.canExecute (io/file p)) p))
           "qemu-system-aarch64"))

     (defn chassis-argv
       "Headless aarch64 virt. `-display none` is the P1b no-monitor gate.
       Serial goes to a file so the host terminal stays the chassis printer.
       User-mode net stands in for Ethernet DHCP."
       [{:keys [qemu firmware qmp serial accel memory]}]
       (let [accel (or accel (if (= "Mac OS X" (System/getProperty "os.name")) "hvf" "tcg"))]
         (chassis-argv-shape {:qemu qemu :firmware firmware :qmp qmp
                              :serial serial :accel accel :memory memory})))

     (defn- qmp-read [^SocketChannel ch]
       (let [buf (ByteBuffer/allocate 8192)
             n (.read ch buf)]
         (when (pos? n)
           (String. (.array buf) 0 n StandardCharsets/UTF_8))))

     (defn- qmp-write [^SocketChannel ch s]
       (let [bytes (.getBytes (str s "\n") StandardCharsets/UTF_8)]
         (.write ch (ByteBuffer/wrap bytes))))

     (defn qmp-eval
       [qmp-path cmd]
       (let [ch (SocketChannel/open StandardProtocolFamily/UNIX)]
         (try
           (.connect ch (UnixDomainSocketAddress/of (.getPath (io/file qmp-path))))
           (qmp-read ch)
           (qmp-write ch "{\"execute\":\"qmp_capabilities\"}")
           (qmp-read ch)
           (qmp-write ch cmd)
           (qmp-read ch)
           (finally
             (.close ch)))))

     (defn qemu-running?
       [qmp-path]
       (try
         (let [r (qmp-eval qmp-path "{\"execute\":\"query-status\"}")]
           (boolean (and r (str/includes? r "\"running\": true"))))
         (catch Exception _ false)))

     (defn wait-qmp
       [qmp-path timeout-ms]
       (let [deadline (+ (System/currentTimeMillis) timeout-ms)]
         (loop []
           (cond
             (qemu-running? qmp-path) true
             (> (System/currentTimeMillis) deadline) false
             :else (do (Thread/sleep 100) (recur))))))

     (defn start-qemu!
       [dir {:keys [accel] :as opts}]
       (let [firmware (find-firmware)
             qemu (find-qemu)
             qmp (.getPath (qmp-file dir))
             serial (.getPath (serial-file dir))]
         (cond
           (nil? firmware)
           {:ok false :unmeasured true :reason :firmware-missing
            :tried firmware-candidates}

           :else
           (do
             (io/make-parents (io/file dir "qemu.stdout"))
             (.delete (io/file qmp))
             (let [argv (chassis-argv {:qemu qemu :firmware firmware :qmp qmp
                                       :serial serial :accel accel})
                   pb (doto (ProcessBuilder. ^java.util.List argv)
                        (.redirectOutput (io/file dir "qemu.stdout"))
                        (.redirectError (io/file dir "qemu.stderr")))
                   proc (.start pb)
                   ready (wait-qmp qmp 8000)
                   alive (.isAlive proc)]
               (spit (io/file dir "qemu.pid") (str (.pid proc)))
               (spit (io/file dir "qemu.argv.edn") (pr-str argv))
               (cond
                 (and ready alive (headless-argv? argv))
                 {:ok true :process proc :argv argv :qmp qmp :serial serial
                  :firmware firmware :qemu qemu}

                 (and (not alive) (not= accel "tcg") (nil? (:no-fallback opts)))
                 (do (try (.destroyForcibly proc) (catch Exception _))
                     (start-qemu! dir (assoc opts :accel "tcg" :no-fallback true)))

                 :else
                 {:ok false
                  :reason (if alive :qmp-timeout :qemu-exited)
                  :exit (when-not alive (.exitValue proc))
                  :argv argv
                  :stderr (when (.isFile (io/file dir "qemu.stderr"))
                            (slurp (io/file dir "qemu.stderr")))}))))))

     (defn stop-qemu!
       [{:keys [process qmp]}]
       (when qmp
         (try (qmp-eval qmp "{\"execute\":\"quit\"}") (catch Exception _)))
       (when process
         (when (.isAlive ^Process process)
           (.destroy ^Process process)
           (when-not (.waitFor ^Process process 3 TimeUnit/SECONDS)
             (.destroyForcibly ^Process process)))))

     (defn power-cycle!
       "Quit and restart the same workdir. Bind state lives in files, so a claimed
       device stays claimed — that is the P1b 'second boot still bound' check."
       [dir qemu-state]
       (stop-qemu! qemu-state)
       (start-qemu! dir {}))

     (defn- read-body [^HttpExchange ex]
       (slurp (.getRequestBody ex)))

     (defn- send-bytes!
       [^HttpExchange ex code content-type ^String body]
       (let [bs (.getBytes (or body "") StandardCharsets/UTF_8)]
         (.set (.getResponseHeaders ex) "Content-Type" content-type)
         (.sendResponseHeaders ex code (alength bs))
         (with-open [out (.getResponseBody ex)]
           (.write out bs))))

     (defn- json-req [ex]
       (or (parse-flat-json (read-body ex)) {}))

     (defn make-runtime
       [{:keys [dir ledger-path listen-port pre-enroll? tenant]}]
       (io/make-parents (device-file dir))
       (let [ledger-path (or ledger-path (.getPath (io/file dir ".." "phone-bind-ledger.edn")))
             device (or (load-device dir) (mint-unbound-device {}))
             _ (save-device dir device)
             grant (when pre-enroll?
                     (or (load-edn (pre-grant-file dir) nil)
                         (write-pre-grant! dir (issue-pre-grant {:tenant (or tenant "acct:local-demo")}))))]
         {:dir (io/file dir)
          :ledger-path ledger-path
          :listen-port (or listen-port 0)
          :device (atom device)
          :qemu (atom nil)
          :server (atom nil)
          :pre-grant (atom grant)
          :nonce (atom nil)
          :guest (atom nil)
          :operator (atom nil)}))

     (defn public-status [rt]
       (let [d @(:device rt)
             q @(:qemu rt)]
         {:did (:did d)
          :state (name (:state d))
          :owner (:owner d)
          :model (:model d)
          :qemu (boolean (:ok q))
          :bound? (= :claimed (:state d))
          :path "phone-http"}))

     (defn handle-challenge [rt]
       (let [n (rand-hex 16)]
         (reset! (:nonce rt) n)
         {:nonce n}))

     (defn handle-attest [rt req]
       (let [d (swap! (:device rt) ensure-operational-keys)
             _ (save-device (:dir rt) d)
             nonce (or (:nonce req) @(:nonce rt))]
         {:did (:did d)
          :nonce nonce
          :signature (sign-nonce (:private-hex d) nonce)
          :agent "hosted-helper"
          :note "Device agent is the hypervisor helper until P2 puts it in the guest."}))

     (defn handle-bind [rt req]
       (let [path (keyword (or (:path req) "phone-http"))
             d @(:device rt)
             {:keys [device verdict possession-proof-valid?]}
             (complete-bind d (assoc req :path path :token (or (:token req) (:token d))))]
         (reset! (:device rt) device)
         (save-device (:dir rt) device)
         (when (enroll/granted? verdict)
           (save-ledger (:ledger-path rt)
                        (record-claimed-device (load-ledger (:ledger-path rt)) device))
           (spit-edn (receipt-file (:dir rt)) (bind-receipt device verdict)))
         (merge (bind-receipt device verdict) verdict
                {:possession-proof-valid? possession-proof-valid?})))

     (defn handle-check-in
       "P1c first boot: consume the imager grant with zero QR."
       [rt]
       (let [grant @(:pre-grant rt)
             d (swap! (:device rt) ensure-operational-keys)
             ledger (load-ledger (:ledger-path rt))]
         (if-not grant
           (deny :no-pre-grant {})
           (let [{:keys [ledger grant device verdict]} (consume-pre-grant ledger grant d)]
             (reset! (:device rt) device)
             (reset! (:pre-grant rt) grant)
             (save-device (:dir rt) device)
             (write-pre-grant! (:dir rt) grant)
             (save-ledger (:ledger-path rt) ledger)
             (when (enroll/granted? verdict)
               (spit-edn (receipt-file (:dir rt)) (bind-receipt device verdict)))
             (merge (bind-receipt device verdict) verdict)))))

     (defn handle-power [rt req]
       (let [action (keyword (or (:action req) "cycle"))
             q @(:qemu rt)]
         (case action
           :cycle (let [next (power-cycle! (:dir rt) q)]
                    (reset! (:qemu rt) next)
                    {:action "cycle"
                     :ok (boolean (:ok next))
                     :bound? (= :claimed (:state @(:device rt)))
                     :state (name (:state @(:device rt)))
                     :reason (:reason next)})
           :off (do (stop-qemu! q)
                    (reset! (:qemu rt) nil)
                    {:action "off" :ok true})
           {:ok false :reason :unknown-action})))

     (defn- endpoint-for [rt port]
       (str "http://127.0.0.1:" port "/enroll"))

     (defn start-http!
       [rt]
       (let [server (HttpServer/create (InetSocketAddress. "127.0.0.1" (int (:listen-port rt))) 0)
             rt (assoc rt :listen-port (.getPort (.getAddress server)))
             port (:listen-port rt)
             endpoint (endpoint-for rt port)
             d @(:device rt)]
         (write-setup-json! (:dir rt) {:device d :endpoint endpoint
                                       :setup-url (setup-url port)
                                       :qr (chassis-qr d endpoint)})
         (.createContext
          server "/"
          (reify HttpHandler
            (handle [_ ex]
              (try
                (let [path (.getPath (.getRequestURI ex))
                      method (.getRequestMethod ex)]
                  (cond
                    (and (= "GET" method) (or (= path "/") (= path "/index.html")))
                    (send-bytes! ex 200 "text/html; charset=utf-8" (session-html))

                    (and (= "GET" method) (= path "/kami-presenter.js"))
                    (if-let [js (kami-presenter-js)]
                      (send-bytes! ex 200 "text/javascript; charset=utf-8" js)
                      (send-bytes! ex 404 "text/plain; charset=utf-8"
                                   "kami-presenter.js missing"))

                    (and (= "GET" method) (= path "/setup.json"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (slurp (setup-json-file (:dir rt))))

                    (and (= "GET" method) (= path "/api/status"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (public-status rt)))

                    (and (= "GET" method) (= path "/api/devices"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json {:devices (vec (vals (:devices (load-ledger (:ledger-path rt)))))
                                          :ledger "non-authoritative"}))

                    (and (= "POST" method) (= path "/api/challenge"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (handle-challenge rt)))

                    (and (= "POST" method) (= path "/api/attest"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (handle-attest rt (json-req ex))))

                    (and (= "POST" method) (= path "/api/bind"))
                    (let [body (handle-bind rt (json-req ex))
                          code (if (= :grant (:aiueos/decision body)) 200 400)]
                      (send-bytes! ex code "application/json; charset=utf-8" (->json body)))

                    (and (= "POST" method) (= path "/api/check-in"))
                    (let [body (handle-check-in rt)
                          code (if (= :grant (:aiueos/decision body)) 200 400)]
                      (send-bytes! ex code "application/json; charset=utf-8" (->json body)))

                    (and (= "POST" method) (= path "/api/power"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (handle-power rt (json-req ex))))

                    (and (= "POST" method) (= path "/api/session/read-cid"))
                    (let [req (json-req ex)
                          body (session-live/read-cid (:cid req))
                          code (case (:outcome body)
                                 "admitted" 200
                                 "unmeasured" 503
                                 400)]
                      (send-bytes! ex code "application/json; charset=utf-8" (->json body)))

                    (and (= "POST" method) (= path "/api/session/infer"))
                    (let [body (session-live/infer)
                          code (case (:outcome body)
                                 "admitted" 200
                                 "unmeasured" 503
                                 400)]
                      (send-bytes! ex code "application/json; charset=utf-8" (->json body)))

                    (and (= "GET" method) (= path "/api/compositor/desktop")
                         (:desktop rt))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (compositor-desktop/public-snapshot
                                          @(:desktop rt))))

                    (and (= "POST" method) (= path "/api/compositor/raise")
                         (:desktop rt))
                    (let [id (:id (json-req ex))
                          d (swap! (:desktop rt) compositor-desktop/raise id)
                          f (io/file (:dir rt) "state" "desktop.edn")]
                      (io/make-parents f)
                      (spit f (pr-str (compositor-desktop/persistable d)))
                      (send-bytes! ex 200 "application/json; charset=utf-8"
                                   (->json (compositor-desktop/wm-event
                                            d :raise
                                            {:id (or (compositor-desktop/parse-window-id id) 0)}))))

                    (and (= "POST" method) (= path "/api/compositor/pointer")
                         (:desktop rt))
                    (let [req (json-req ex)
                          px (long (or (:x req) 0))
                          py (long (or (:y req) 0))
                          [d ev] (compositor-desktop/route-pointer
                                  @(:desktop rt) px py)
                          f (io/file (:dir rt) "state" "desktop.edn")]
                      (reset! (:desktop rt) d)
                      (io/make-parents f)
                      (spit f (pr-str (compositor-desktop/persistable d)))
                      (send-bytes! ex 200 "application/json; charset=utf-8"
                                   (->json (compositor-desktop/wm-event
                                            d :pointer
                                            {:x px :y py
                                             :hit (or (:hit ev) 0)
                                             :title-bar? (boolean (:title-bar? ev))}))))

                    (and (= "POST" method) (= path "/api/compositor/key")
                         (:desktop rt))
                    (let [req (json-req ex)
                          k (or (:key req) "")
                          [d ev] (compositor-desktop/route-key @(:desktop rt) k)
                          f (io/file (:dir rt) "state" "desktop.edn")]
                      (reset! (:desktop rt) d)
                      (io/make-parents f)
                      (spit f (pr-str (compositor-desktop/persistable d)))
                      (send-bytes! ex 200 "application/json; charset=utf-8"
                                   (->json (compositor-desktop/wm-event
                                            d :key
                                            {:key (str k)
                                             :consumed? (boolean (:consumed? ev))
                                             :reason (name (:reason ev))
                                             :guest-text (or (:guest-text ev) "")
                                             :latin-leaked? (boolean (:latin-leaked? ev))}))))

                    (and (= "POST" method) (= path "/api/compositor/ime")
                         (:desktop rt))
                    (let [req (json-req ex)
                          on? (boolean (:on? req))
                          d (compositor-desktop/set-ime @(:desktop rt) on?)
                          f (io/file (:dir rt) "state" "desktop.edn")]
                      (reset! (:desktop rt) d)
                      (io/make-parents f)
                      (spit f (pr-str (compositor-desktop/persistable d)))
                      (send-bytes! ex 200 "application/json; charset=utf-8"
                                   (->json (compositor-desktop/wm-event
                                            d :ime
                                            {:on? on?}))))

                    (and (= "GET" method) (= path "/api/session/guests"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (session-guest/public-list
                                          (some-> (:guest rt) deref))))

                    (and (= "POST" method) (= path "/api/session/guest"))
                    (let [req (json-req ex)
                          mode (session-guest/parse-grant-mode (:grant req))
                          result (session-guest/run mode)
                          _ (when-let [g (:guest rt)] (reset! g result))
                          snap (session-guest/public-snapshot result)
                          code (session-guest/http-code result)]
                      (send-bytes! ex code "application/json; charset=utf-8"
                                   (->json snap)))

                    (and (= "GET" method) (= path "/api/session/operator"))
                    (send-bytes! ex 200 "application/json; charset=utf-8"
                                 (->json (or (session-operator/public-snapshot
                                              (some-> (:operator rt) deref))
                                             {:component "operator/itonami"
                                              :visible false
                                              :called false
                                              :authority "itonami.cloud"
                                              :via "grant"
                                              :via_process "session-process"})))

                    (and (= "POST" method) (= path "/api/session/operator"))
                    (let [req (json-req ex)
                          mode (session-operator/parse-grant-mode (:grant req))
                          result (session-operator/run mode)
                          _ (when-let [o (:operator rt)] (reset! o result))
                          snap (session-operator/public-snapshot result)
                          code (session-operator/http-code result)]
                      (send-bytes! ex code "application/json; charset=utf-8"
                                   (->json snap)))

                    :else (send-bytes! ex 404 "text/plain; charset=utf-8" "not found")))
                (catch Exception e
                  (send-bytes! ex 500 "text/plain; charset=utf-8" (.getMessage e)))))))
         (.setExecutor server (Executors/newCachedThreadPool))
         (.start server)
         (reset! (:server rt) server)
         rt))

     (defn stop-http! [rt]
       (when-let [s @(:server rt)]
         (.stop ^HttpServer s 0)))

     (defn- http-conn
       ([url] (http-conn url nil))
       ([url {:keys [connect-timeout-ms read-timeout-ms]
              :or {connect-timeout-ms 3000 read-timeout-ms 8000}}]
        (doto ^java.net.HttpURLConnection
          (.openConnection (.toURL (java.net.URI/create url)))
          (.setConnectTimeout (int connect-timeout-ms))
          (.setReadTimeout (int read-timeout-ms)))))

     (defn http-get
       ([url] (http-get url nil))
       ([url opts]
        (let [conn (doto (http-conn url opts) (.setRequestMethod "GET"))
              code (.getResponseCode conn)
              body (slurp (or (.getErrorStream conn) (.getInputStream conn)))]
          {:code code :body body})))

     (defn http-post
       ([url json-map] (http-post url json-map nil))
       ([url json-map opts]
        (let [body (->json json-map)
              conn (doto (http-conn url opts)
                     (.setRequestMethod "POST")
                     (.setDoOutput true)
                     (.setRequestProperty "Content-Type" "application/json"))]
          (with-open [out (.getOutputStream conn)]
            (.write out (.getBytes body StandardCharsets/UTF_8)))
          (let [code (.getResponseCode conn)
                in (or (.getErrorStream conn) (.getInputStream conn))
                resp (slurp in)]
            {:code code :body resp :parsed (parse-flat-json resp)}))))

     (defn phone-bind-http
       "Simulated phone client. HTTP only — no guest VGA."
       [base owner]
       (let [setup (parse-flat-json (:body (http-get (str base "/setup.json"))))
             ch (:parsed (http-post (str base "/api/challenge") {}))
             at (:parsed (http-post (str base "/api/attest") {:nonce (:nonce ch)}))
             bind (http-post (str base "/api/bind")
                             {:owner owner
                              :token (:token setup)
                              :nonce (:nonce ch)
                              :signature (:signature at)
                              :path "phone-http"})]
         bind))

     (defn print-chassis! [rt]
       (let [port (:listen-port rt)
             d @(:device rt)
             url (setup-url port)
             qr (chassis-qr d (endpoint-for rt port))]
         (println (str "AIUEOS_SETUP_URL=" url))
         (println (str "AIUEOS_QR=" qr))
         (println (str "AIUEOS_SETUP_JSON=" (.getPath (setup-json-file (:dir rt)))))
         {:url url :qr qr}))

     (defn base-url [rt]
       (str "http://127.0.0.1:" (:listen-port rt)))

     (defn run-smoke
       "P1b positive + no-monitor + power-cycle. Optional P1c via :pre-enroll?"
       [{:keys [dir pre-enroll?] :as opts}]
       (let [dir (io/file (or dir (str (System/getProperty "java.io.tmpdir") "/aiueos-phone-bind-vm")))
             rt (start-http! (make-runtime (assoc opts :dir dir)))
             qemu (start-qemu! dir {})]
         (reset! (:qemu rt) qemu)
         (print-chassis! rt)
         (println (str "AIUEOS_QEMU_LOG=" (.getPath (io/file dir "qemu.stderr"))))
         (println (str "AIUEOS_QEMU_SERIAL=" (.getPath (serial-file dir))))
         (try
           (cond
             (not (:ok qemu))
             (do (println "AIUEOS_QEMU_FAIL" (pr-str (dissoc qemu :process)))
                 {:exit (if (:unmeasured qemu) 3 1)
                  :qemu qemu
                  :reason (:reason qemu)})

             (not (headless-argv? (:argv qemu)))
             {:exit 1 :reason :display-not-none :argv (:argv qemu)}

             pre-enroll?
             (let [check (http-post (str (base-url rt) "/api/check-in") {})
                   copied-dir (io/file dir "copied")
                   _ (do (io/make-parents (pre-grant-file copied-dir))
                         (spit (pre-grant-file copied-dir) (slurp (pre-grant-file dir))))
                   rt2 (make-runtime {:dir copied-dir
                                      :ledger-path (:ledger-path rt)
                                      :listen-port 0
                                      :pre-enroll? true})
                   second (handle-check-in rt2)
                   cycle (handle-power rt {:action "cycle"})
                   granted? (= 200 (:code check))]
               (when granted? (println "AIUEOS_BIND_OK"))
               {:exit (if (and granted?
                               (= :deny (:aiueos/decision second))
                               (= :already-consumed (:aiueos.enroll/reason second))
                               (:ok cycle)
                               (= :claimed (:state @(:device rt))))
                        0 1)
                :check check :second second :cycle cycle :qemu qemu})

             :else
             (let [page (http-get (str (base-url rt) "/"))
                   spa? (and (= 200 (:code page))
                             (re-find #"dads-button" (:body page))
                             (re-find #"jp-go-dds" (:body page))
                             (re-find #"href=\"#setup\"" (:body page)))
                   bind (phone-bind-http (base-url rt) "acct:local-demo")
                   granted? (= 200 (:code bind))
                   cycle (handle-power rt {:action "cycle"})
                   still (= :claimed (:state @(:device rt)))]
               (when-not spa?
                 (println "AIUEOS_SPA_FAIL not the DADS apps/session document"))
               (when (and spa? granted? still (:ok cycle))
                 (println "AIUEOS_BIND_OK"))
               (when-not granted?
                 (println "AIUEOS_BIND_FAIL" (:body bind)))
               {:exit (if (and spa? granted? still (:ok cycle) (headless-argv? (:argv qemu))) 0 1)
                :bind bind :cycle cycle :qemu qemu :spa? spa?
                :receipt (when (.isFile (receipt-file dir)) (slurp (receipt-file dir)))}))
           (finally
             (stop-qemu! @(:qemu rt))
             (stop-http! rt)))))

     (defn -main
       [& args]
       (let [cmd (or (first args) "smoke")
             pre? (some #{"--pre-enroll" "pre-enroll"} args)
             dir (or (System/getenv "AIUEOS_PHONE_BIND_DIR")
                     (.getPath (io/file (System/getProperty "java.io.tmpdir") "aiueos-phone-bind-vm")))]
         (case cmd
           "serve"
           (let [rt (start-http! (make-runtime {:dir dir :pre-enroll? pre?}))
                 qemu (start-qemu! dir {})]
             (reset! (:qemu rt) qemu)
             (print-chassis! rt)
             (println "listening" (base-url rt) "Ctrl-C to stop")
             (.addShutdownHook (Runtime/getRuntime)
                               (Thread. #(do (stop-qemu! @(:qemu rt)) (stop-http! rt))))
             @(promise))

           ("smoke" "pre-enroll")
           (let [r (run-smoke {:dir dir :pre-enroll? (or pre? (= cmd "pre-enroll"))})]
             (flush)
             (System/exit (int (:exit r))))

           (do (println "usage: clojure -M:phone-bind [smoke|serve|pre-enroll] [--pre-enroll]")
               (System/exit 3)))))))
