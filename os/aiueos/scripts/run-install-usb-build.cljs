#!/usr/bin/env nbb
;; Build the install USB image and close its receipt chain (install-v1.edn
;; gate I1, root ADR adr-2608251418).
;;
;;   nbb os/aiueos/scripts/run-install-usb-build.cljs \
;;     [--intent build/aiueos/install-intent.json] [--node-binary <path>]
;;
;; Orchestration only: the release image must already exist with its receipt
;; (build-release-image.sh), and the intent must already exist -- an intent
;; authorizes erasing one named disk, so THIS script never invents one. It
;; refuses with the exact create command instead. The image assembly itself is
;; make-install-usb-image.py, sh+python-stdlib like the other image builders,
;; so Node stays out of the boot-evidence path.

(require '[clojure.string :as str])

(def fs (js/require "node:fs"))
(def path (js/require "node:path"))
(def cp (js/require "node:child_process"))

(def aiueos (.resolve path (.dirname path *file*) ".."))
(def repo (.resolve path aiueos ".." ".."))
(def out (or (.-AIUEOS_OUT js/process.env) (.join path repo "build" "aiueos")))

(defn- die [& msg]
  (binding [*out* *err*] (apply println (cons "error:" msg)))
  (.exit js/process 1))

(defn- arg [flag]
  (let [a (vec *command-line-args*)
        i (.indexOf (to-array a) flag)]
    (when (>= i 0) (nth a (inc i) nil))))

(def release-image (.join path out "aiueos-x86_64-gpt.img"))
(def release-receipt (.join path out "aiueos-x86_64-build-receipt.json"))
(def intent (or (arg "--intent") (.join path out "install-intent.json")))
(def usb-image (.join path out "aiueos-x86_64-install-usb.img"))
(def usb-receipt (.join path out "aiueos-x86_64-install-usb-receipt.json"))

(when-not (.existsSync fs release-image)
  (die "release image not built:" release-image
       "\nhint: run os/aiueos/scripts/build-release-image.sh first"))
(when-not (.existsSync fs release-receipt)
  (die "release receipt missing:" release-receipt))
(when-not (.existsSync fs intent)
  (die "install intent missing:" intent
       "\nAn intent names the ONE disk an unattended install may erase, so it"
       "\nis never auto-created. Create it deliberately:"
       "\n  nbb os/aiueos/scripts/install-intent.cljs create \\"
       "\n    --receipt" release-receipt "\\"
       "\n    --hostname <host> --confirm-create <host> \\"
       "\n    --target-model <model> --target-transport nvme \\"
       "\n    --target-min-gb <n> --target-max-gb <n> \\"
       "\n    --ssh-public-key-file ~/.ssh/<key>.pub \\"
       "\n    --out" intent))

(def builder-args
  (concat ["build"
           "--release-image" release-image
           "--release-receipt" release-receipt
           "--intent" intent
           "--installer-dir" (.join path aiueos "installer")
           "--output" (or (arg "--output") usb-image)
           "--receipt" (or (arg "--receipt") usb-receipt)]
          (when-let [node-bin (arg "--node-binary")]
            ["--node-binary" node-bin])
          (when-let [nbb-dir (arg "--nbb-dir")]
            ["--nbb-dir" nbb-dir])
          (when-let [uki (arg "--live-uki")]
            ["--live-uki" uki])))

(let [r (.spawnSync cp "python3"
                    (to-array (cons (.join path aiueos "scripts" "make-install-usb-image.py")
                                    builder-args))
                    #js {:encoding "utf8" :shell false :stdio "inherit"})]
  (when-not (zero? (or (.-status r) 1))
    (die "install USB image build failed")))

(println "AIUEOS_INSTALL_USB_BUILD_OK"
         (str "image=" (or (arg "--output") usb-image))
         (str "receipt=" (or (arg "--receipt") usb-receipt)))
