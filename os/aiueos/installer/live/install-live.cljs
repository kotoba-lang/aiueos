#!/usr/bin/env nbb
;; Decision layer of the live installer environment (install-v1.edn, root ADR
;; adr-2608251418). /init (mechanism) has already mounted the install-USB
;; payload, extracted the bundle to a tmpfs, and handed over. This script:
;;
;;   1. gets the install intent: reads the one shipped on the USB, or -- on a
;;      stick that carries none -- runs guided-install.cljs to author one at
;;      the machine (ADR-0210). Both end here with the same artifact, and
;;      everything after this step is the same path;
;;   2. enumerates whole disks and keeps those matching the intent's model /
;;      transport / capacity bounds, never the disk we booted from;
;;   3. refuses unless EXACTLY ONE candidate remains -- zero is a machine the
;;      intent does not describe, two or more is an ambiguity no unattended
;;      run may resolve on its own (decision 2);
;;   4. interactive intent: dry-run report only, nothing written;
;;      unattended intent: hands the one named device to install-to-disk.cljs,
;;      which re-verifies the intent against its own inspection, checks the
;;      target receipt (repeat safety), and drives the guarded install.mjs.
;;
;; The confirmations passed to install-to-disk are supplied here because in
;; unattended mode the INTENT is the owner's confirmation; every device-level
;; guard (whole/internal/empty/system-disk/identity) still runs below.

(require '[kotoba.lang.text :as str])

(def fs (js/require "node:fs"))
(def cp (js/require "node:child_process"))

(defn- die [code & msg]
  (binding [*out* *err*] (apply println (cons "error:" msg)))
  (.exit js/process code))

(def usb-payload-dev (or (.-AIUEOS_LIVE_PAYLOAD_DEV js/process.env)
                         (die 3 "AIUEOS_LIVE_PAYLOAD_DEV is not set; run from /init")))

;; A stick may carry an intent or ask for one. Both are products
;; (install-v1.edn :install-usb :authoring, ADR-0210): a stick that carries
;; one installs unattended, a stick that asks installs nothing without a
;; person. Which one this is, is decided by what is in the tmpfs -- and the
;; bundle only contains install-intent.json when the USB carried INTENT.JSN.
;;
;; The guided program runs with the console attached and writes the intent
;; here. It opens no block device; everything below this line is unchanged,
;; including the fresh-probe verification of the intent it just wrote. An
;; intent authored ten seconds ago is not more trusted than one authored last
;; week.
(def intent-file "install-intent.json")

(when-not (.existsSync fs intent-file)
  (println "AIUEOS_LIVE_NO_INTENT this stick asks; starting the guided installer")
  (let [r (.spawnSync cp "nbb"
                      #js ["guided-install.cljs"
                           "--release-receipt" "release-receipt.json"
                           "--out-intent" "install-intent.json"
                           "--out-answers" "install-answers.json"]
                      #js {:stdio "inherit" :shell false})
        status (or (.-status r) 3)]
    ;; The guided program's exit codes are kept, not flattened: 2 is a named
    ;; refusal and 3 is could-not-answer, and an installer that reported both
    ;; as "failed" would send the operator looking for the wrong thing.
    (when-not (zero? status)
      (println "AIUEOS_LIVE_GUIDED_ABORTED" (str "status=" status))
      (.exit js/process status))
    (when-not (.existsSync fs intent-file)
      (die 3 "guided installer exited 0 without writing" intent-file))))

(def intent
  (try (js->clj (.parse js/JSON (.readFileSync fs intent-file "utf8"))
                :keywordize-keys true)
       (catch :default e (die 3 "cannot read install-intent.json:" (.-message e)))))
(when-not (= "aiueos.install-intent.v1" (:schema intent))
  (die 3 "not an aiueos.install-intent.v1 intent"))

(defn- run-text [cmd args]
  (let [r (.spawnSync cp cmd (to-array args) #js {:encoding "utf8" :shell false})]
    (when-not (zero? (or (.-status r) 1))
      (die 3 cmd "failed:" (str/trim (or (.-stderr r) ""))))
    (or (.-stdout r) "")))

(def usb-disk
  (let [pk (str/trim (run-text "lsblk" ["-rno" "PKNAME" usb-payload-dev]))]
    (if (seq pk) (str "/dev/" pk) usb-payload-dev)))

(def disks
  (-> (run-text "lsblk" ["--json" "--bytes" "--nodeps" "--output"
                         "PATH,TYPE,SIZE,MODEL,TRAN,SERIAL,RM"])
      (as-> out (js->clj (.parse js/JSON out) :keywordize-keys true))
      :blockdevices))

(def td (:targetDisk intent))
(defn- matches? [d]
  (and (= "disk" (:type d))
       (not= (:path d) usb-disk)
       (not (contains? #{1 true "1"} (:rm d)))
       (let [want (str/lower (or (:model td) ""))
             got (str/lower (or (:model d) ""))]
         (and (seq want) (str/includes? got want)))
       (= (str/lower (or (:tran d) "")) (str/lower (:transport td)))
       (number? (:size d))
       (<= (:minBytes td) (:size d) (:maxBytes td))))

(def candidates (filterv matches? disks))
(println "AIUEOS_LIVE_DISKS"
         (str "total=" (count disks))
         (str "usb=" usb-disk)
         (str "candidates=" (count candidates)))

(cond
  (zero? (count candidates))
  (do (println "AIUEOS_INSTALL_REFUSED no-matching-target"
               (str "intent-model=" (:model td))
               (str "intent-transport=" (:transport td)))
      (.exit js/process 2))
  (> (count candidates) 1)
  (do (println "AIUEOS_INSTALL_REFUSED multiple-candidate-targets"
               (str "paths=" (str/join "," (map :path candidates))))
      (.exit js/process 2)))

(def target (first candidates))
(def device (:path target))
(println "AIUEOS_LIVE_TARGET" device
         (str "model=" (pr-str (:model target)))
         (str "bytes=" (:size target)))

(def base-args ["install-to-disk.cljs"
                "--intent" "install-intent.json"
                "--device" device
                "--image" (str (.-AIUEOS_LIVE_MEDIA js/process.env) "/RELEASE.IMG")
                "--receipt" "release-receipt.json"])
(def serial-args (when (and (:serialSha256 td) (:serial target))
                   ["--serial" (str/trim (:serial target))]))

(def unattended? (= "unattended" (:mode intent)))
(def args (concat base-args serial-args
                  (when unattended?
                    ["--install" "--confirm-device" device
                     "--destructive-phrase" (str "ERASE " device " FOR AIUEOS")])))

(when-not unattended?
  (println "AIUEOS_LIVE_INTERACTIVE dry-run only; no destructive action without an operator"))

(let [r (.spawnSync cp "nbb" (to-array args)
                    #js {:stdio "inherit" :shell false})]
  (.exit js/process (or (.-status r) 3)))
