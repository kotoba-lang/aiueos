#!/usr/bin/env nbb
;; The guided aiueos installer -- the ubuntu-server installer's shape, on this
;; repository's admission rules (install-v1.edn, root ADR adr-2608251418,
;; aiueos ADR-0208).
;;
;;   ask + write the intent, nothing else:
;;     nbb os/aiueos/installer/live/guided-install.cljs \
;;       --release-receipt release-receipt.json \
;;       --out-intent install-intent.json --out-answers install-answers.json
;;
;;   replay a previous run without a person (subiquity: autoinstall.yaml):
;;     nbb os/aiueos/installer/live/guided-install.cljs \
;;       --answers install-answers.json --release-receipt release-receipt.json \
;;       --out-intent install-intent.json
;;
;;   ask only the screens named, take the rest from the answer file:
;;     ... --answers install-answers.json --interactive-sections storage,confirm
;;
;; What this program does NOT do: open a block device, write a partition
;; table, or erase anything. It probes read-only and produces one
;; `aiueos.install-intent.v1` -- the artifact that already exists, verified by
;; the same install-intent.cljs, consumed by the same install-live.cljs and
;; install-to-disk.cljs, sealed into the same USB receipt chain. The guided
;; path is a way to AUTHOR an intent at the machine instead of ahead of time
;; on another host. It adds no authority: every device-level guard still runs
;; below it.
;;
;; Order matters and is deliberate: the answer file is validated BEFORE the
;; first probe, so an unattended run with a malformed answer file fails while
;; the target disk is still untouched. That is subiquity's property too --
;; autoinstall is schema-checked before curtin is invoked.
;;
;; Exit codes: 0 wrote an intent / 2 refused with named reasons / 3
;; could-not-answer (no probe, stdin ended mid-question, missing input file).
;; 3 is neither 0 nor 1 on purpose: a run that could not ask must not look
;; like a run that asked and got answers.

(require '[kotoba.lang.text :as str]
         '[aiueos.installer.guided :as g])

(def fs (js/require "node:fs"))
(def path (js/require "node:path"))
(def cp (js/require "node:child_process"))
(def crypto (js/require "node:crypto"))

(defn- die [code & msg]
  (binding [*out* *err*] (apply println (cons "error:" msg)))
  (.exit js/process code))

(def args (vec *command-line-args*))
(defn- arg [flag]
  (let [i (.indexOf (to-array args) flag)]
    (when (>= i 0) (nth args (inc i) nil))))
(defn- flag? [f] (>= (.indexOf (to-array args) f) 0))

;; ------------------------------------------------------------------- host io

(defn- sha256-hex [buf]
  (-> (.createHash crypto "sha256") (.update buf) (.digest "hex")))

(defn- sha256-file [p] (sha256-hex (.readFileSync fs p)))

(defn- read-json [p]
  (js->clj (.parse js/JSON (.readFileSync fs p "utf8")) :keywordize-keys true))

(defn- write-json! [p m]
  (let [dir (.dirname path p)]
    (when (seq dir) (.mkdirSync fs dir #js {:recursive true})))
  (.writeFileSync fs p (str (.stringify js/JSON (clj->js m) nil 2) "\n")))

(defn- ssh-fingerprint
  "OpenSSH fingerprint of an authorized_keys line, computed the same way
  install-intent.cljs computes it -- the two must agree byte for byte or the
  same key would produce two different intents."
  [pubkey-line]
  (let [fields (str/split (str/trim pubkey-line) #"\s+")
        blob (js/Buffer.from (nth fields 1) "base64")]
    (str "SHA256:"
         (-> (.createHash crypto "sha256") (.update blob) (.digest "base64")
             (str/replace #"=+$" "")))))

(defn- serial-digest [salt-hex serial]
  (sha256-hex (js/Buffer.from (str salt-hex ":" (str/trim (str serial))) "utf8")))

;; ------------------------------------------------------------------ terminal
;;
;; Synchronous, one byte at a time from fd 0. The whole installer chain in
;; this repository is synchronous (spawnSync, readFileSync), and a readline
;; interface would make this the only file with a promise in it. Reading fd 0
;; directly also means a piped answer script and a real terminal take exactly
;; the same code path, which is what lets the test drive the real program
;; rather than a test-only branch of it.

(defn- sleep-ms! [ms]
  (let [sab (js/SharedArrayBuffer. 4)]
    (js/Atomics.wait (js/Int32Array. sab) 0 0 ms)))

(defn- read-line!
  "One line from stdin, or nil at end of input. `\\r` is stripped so an answer
  file written on another machine still answers."
  []
  (let [buf (js/Buffer.alloc 1)
        acc (atom "")
        got (atom false)]
    (loop []
      (let [n (try (.readSync fs 0 buf 0 1 nil)
                   (catch :default e
                     (case (.-code e)
                       "EAGAIN" :again
                       ("EOF" "ENXIO") 0
                       (throw e))))]
        (cond
          (= :again n) (do (sleep-ms! 20) (recur))
          (or (nil? n) (zero? n)) (when @got @acc)
          :else (let [c (.toString buf "utf8" 0 1)]
                  (reset! got true)
                  (cond
                    (= c "\n") @acc
                    (= c "\r") (recur)
                    :else (do (swap! acc str c) (recur)))))))))

(def max-attempts 3)

(defn- ask
  "Print a prompt and read one answer. `default` is returned for an empty
  line. End of input is not a blank answer -- it is `could-not-answer`, and it
  exits 3 rather than quietly accepting a default nobody typed."
  ([prompt] (ask prompt nil))
  ([prompt default]
   (.write js/process.stdout (str prompt (when default (str " [" default "]")) ": "))
   (let [line (read-line!)]
     (cond
       (nil? line) (die 3 "stdin ended while asking:" prompt)
       (and (str/blank? line) default) default
       :else (str/trim line)))))

(defn- ask-validated [prompt default valid? reason]
  (loop [attempt 1]
    (let [v (ask prompt default)]
      (cond
        (valid? v) v
        (>= attempt max-attempts) (do (println "AIUEOS_GUIDED_REFUSE" reason)
                                      (.exit js/process 2))
        :else (do (println (str "  not accepted (" reason "); "
                                (- max-attempts attempt) " attempt(s) left"))
                  (recur (inc attempt)))))))

(defn- yes? [s] (contains? #{"y" "yes"} (str/lower (str s))))

(defn- banner [step]
  (println)
  (println (str "== " (:title step) " =="))
  (doseq [l (str/split-lines (:help step))]
    (println (str "   " (str/trim l)))))

;; --------------------------------------------------------------- probe disks

(def fake-probe
  "A probe fixture may be injected only under the same two-key gate the ported
  installer uses for its fake block device: an env flag AND a test marker.
  This program never writes to a disk, so an injected probe cannot bypass a
  destructive guard -- the gate is here so that a fixture can never be reached
  by an operator who did not deliberately ask for one."
  (when (and (= "1" (.-AIUEOS_GUIDED_ALLOW_FAKE_PROBE js/process.env))
             (= "test" (.-NODE_ENV js/process.env)))
    (arg "--probe-file")))

(defn- probe-disks!
  "Whole-disk inventory, read-only. `lsblk --nodeps` on Linux; on any other
  host there is no equivalent that reports transport and removability the same
  way, so the answer is `could-not-answer` rather than a guess."
  []
  (if fake-probe
    (do (println "AIUEOS_GUIDED_PROBE fake" fake-probe)
        (:blockdevices (read-json fake-probe)))
    (let [r (.spawnSync cp "lsblk"
                        #js ["--json" "--bytes" "--nodeps" "--output"
                             "PATH,TYPE,SIZE,MODEL,TRAN,SERIAL,RM"]
                        #js {:encoding "utf8" :shell false})]
      (when-not (zero? (or (.-status r) 1))
        (die 3 "lsblk could not enumerate disks:"
             (str/trim (or (.-stderr r) "no stderr"))))
      (:blockdevices (js->clj (.parse js/JSON (.-stdout r)) :keywordize-keys true)))))

(defn- boot-disk!
  "The disk this installer booted from, which is never offered as a target.
  Supplied by /init as AIUEOS_LIVE_PAYLOAD_DEV (a partition), resolved to its
  parent disk; --boot-disk overrides for a host run."
  []
  (or (arg "--boot-disk")
      (when-let [dev (.-AIUEOS_LIVE_PAYLOAD_DEV js/process.env)]
        (let [r (.spawnSync cp "lsblk" #js ["-rno" "PKNAME" dev]
                            #js {:encoding "utf8" :shell false})
              pk (str/trim (or (.-stdout r) ""))]
          (if (seq pk) (str "/dev/" pk) dev)))))

(defn- gb [bytes] (str/format "%.1f GB" (/ bytes 1e9)))

;; --------------------------------------------------------------- the screens

(defn- screen-network [answers]
  (let [step (g/step-by-id "network")]
    (banner step)
    (println (str "   admitted policies: " (str/join ", " (sort g/network-policies))))
    (let [p (ask-validated (:prompt step) "wired-dhcp"
                           #(contains? g/network-policies %)
                           "answers-network-policy-unsupported")]
      (assoc answers :network {:policy p}))))

(defn- screen-storage [answers disks boot-disk image-bytes]
  (let [step (g/step-by-id "storage")
        candidates (g/candidate-disks disks {:boot-disk boot-disk
                                             :image-bytes image-bytes})]
    (banner step)
    (println (str "   probed " (count disks) " whole disk(s); "
                  (count candidates) " may be offered"
                  (when boot-disk (str "; boot media " boot-disk " excluded"))))
    (when (zero? (count candidates))
      (println "AIUEOS_GUIDED_REFUSE no-candidate-disks")
      (.exit js/process 2))
    (println)
    (doseq [[i d] (map-indexed vector candidates)]
      (println (str "   " (inc i) ") " (:path d)
                    "  " (gb (:size d))
                    "  model=" (pr-str (str/trim (str (:model d))))
                    "  transport=" (or (:tran d) "unmeasured")
                    "  serial=" (if (seq (str/trim (str (:serial d)))) "present" "unmeasured"))))
    (println)
    (let [pick (ask-validated "Disk to erase (number)" (when (= 1 (count candidates)) "1")
                              (fn [s] (let [n (js/parseInt s 10)]
                                        (and (not (js/isNaN n)) (<= 1 n (count candidates)))))
                              "guided-storage-pick-out-of-range")
          disk (nth candidates (dec (js/parseInt pick 10)))
          bind-serial? (and (seq (str/trim (str (:serial disk))))
                            (yes? (ask "Bind the intent to this disk's serial (its digest, never the serial itself)? y/n" "y")))
          match (cond-> (g/disk->match disk)
                  (not bind-serial?) (dissoc :serial))]
      (println)
      (println "   the intent will record this MATCH, not the device path:")
      (println (str "     model      " (pr-str (:model match))))
      (println (str "     transport  " (:transport match)))
      (println (str "     capacity   " (:min-gb match) " GB .. " (:max-gb match) " GB (inclusive)"))
      (println (str "     serial     " (if (:serial match) "bound by digest" "not bound")))
      (println "   on the target machine the installer re-derives the device from")
      (println "   this match and refuses unless exactly one disk matches.")
      (when-not (yes? (ask "Accept this match? y/n" "y"))
        (println "AIUEOS_GUIDED_REFUSE guided-storage-match-rejected")
        (.exit js/process 2))
      (assoc answers :storage match))))

(defn- screen-identity [answers]
  (let [step (g/step-by-id "identity")]
    (banner step)
    (let [h (ask-validated (:prompt step) nil g/hostname-ok? "answers-hostname-invalid")
          m (ask "Machine model, for the receipt" "unspecified")]
      (assoc answers :identity {:hostname h :machine-model m}))))

(defn- screen-ssh [answers]
  (let [step (g/step-by-id "ssh")]
    (banner step)
    (println "   paste the key line, or give @/path/to/key.pub to read one")
    (let [raw (ask-validated
               (:prompt step) nil
               (fn [s]
                 (let [v (if (str/starts-with? s "@")
                           (let [p (subs s 1)]
                             (when (.existsSync fs p) (str/trim (.readFileSync fs p "utf8"))))
                           s)]
                   (g/openssh-public-key? v)))
               "answers-ssh-key-not-openssh")
          key-line (if (str/starts-with? raw "@")
                     (str/trim (.readFileSync fs (subs raw 1) "utf8"))
                     raw)
          principal (ask "Principal that key logs in as" "aiueos")]
      (println (str "   fingerprint " (ssh-fingerprint key-line)))
      (assoc answers :ssh {:principal principal :public-key key-line}))))

(defn- screen-confirm [answers]
  (let [step (g/step-by-id "confirm")
        host (get-in answers [:identity :hostname])
        st (:storage answers)]
    (banner step)
    (println)
    (println "   this intent authorizes ERASING one whole disk on the machine")
    (println "   it names. Nothing is erased by this program; the erase happens")
    (println "   when the installer runs with this intent and its own guards pass.")
    (println)
    (println (str "     hostname   " host))
    (println (str "     disk       " (pr-str (:model st)) " / " (:transport st)
                  " / " (:min-gb st) "-" (:max-gb st) " GB"
                  (when (:serial st) " / serial bound")))
    (println (str "     ssh        " (get-in answers [:ssh :principal])
                  " " (str/truncate (get-in answers [:ssh :public-key]) 40)))
    (println (str "     mode       " (:mode answers)))
    (println)
    (let [repeated (ask-validated (:prompt step) nil
                                  #(= host %)
                                  "answers-confirm-mismatch")]
      (assoc answers :confirm {:hostname repeated}))))

(defn- run-screen [answers step ctx]
  (case (:id step)
    "network" (screen-network answers)
    "storage" (screen-storage answers (:disks ctx) (:boot-disk ctx) (:image-bytes ctx))
    "identity" (screen-identity answers)
    "ssh" (screen-ssh answers)
    "confirm" (screen-confirm answers)
    (die 3 "unknown screen:" (:id step))))

;; ------------------------------------------------------------------ the walk

(def receipt-path
  (or (arg "--release-receipt") (die 3 "--release-receipt is required")))
(def out-intent (or (arg "--out-intent") (die 3 "--out-intent is required")))
(def out-answers (arg "--out-answers"))

(when-not (.existsSync fs receipt-path)
  (die 3 "release receipt not found:" receipt-path))

(def release
  (let [r (read-json receipt-path)
        disk (:disk r)]
    (when-not (and (:bytes disk) (:sha256 disk))
      (die 3 "release receipt has no disk.bytes/disk.sha256:" receipt-path))
    {:receipt-sha256 (sha256-file receipt-path)
     :disk {:bytes (:bytes disk) :sha256 (:sha256 disk)}}))

(def seed-answers
  (let [p (arg "--answers")]
    (cond
      (nil? p) {:schema g/answers-schema}
      (not (.existsSync fs p)) (die 3 "answer file not found:" p)
      :else (let [a (read-json p)]
              (when-not (= g/answers-schema (:schema a))
                (die 3 "not an" g/answers-schema ":" p))
              a))))

(def answers0
  (cond-> seed-answers
    (arg "--mode") (assoc :mode (arg "--mode"))
    (nil? (:mode seed-answers)) (assoc :mode (or (arg "--mode") "interactive"))
    (arg "--expires-days") (assoc :expires-days (js/parseInt (arg "--expires-days") 10))
    (arg "--interactive-sections")
    (assoc :interactive-sections (str/split (arg "--interactive-sections") #","))))

;; Validate what the answer file already says BEFORE probing anything. A
;; malformed unattended answer file must fail here, with the target disk
;; untouched and lsblk not yet run.
(let [{:keys [ok reasons]} (g/validate-answers answers0 {:complete? false})]
  (when-not ok
    (doseq [r reasons] (println "AIUEOS_GUIDED_REFUSE" r))
    (.exit js/process 2)))

(def pending (g/pending-steps answers0))

(println "AIUEOS_GUIDED_START"
         (str "mode=" (:mode answers0))
         (str "screens=" (count g/flow))
         (str "pending=" (count pending))
         (str "asked=" (str/join "," (map :id pending))))

;; The probe runs only when a screen needs it. An unattended replay of a
;; complete answer file asks nothing and probes nothing: target selection
;; belongs to install-live.cljs on the target machine, and probing here would
;; be a second, weaker copy of that decision.
(def ctx
  (if (some #(= "storage" (:id %)) pending)
    (let [bd (boot-disk!)]
      {:disks (probe-disks!) :boot-disk bd
       :image-bytes (get-in release [:disk :bytes])})
    {}))

(def answers
  (reduce (fn [a step] (run-screen a step ctx)) answers0 pending))

(let [{:keys [ok reasons]} (g/validate-answers answers)]
  (when-not ok
    (println)
    (doseq [r reasons] (println "AIUEOS_GUIDED_REFUSE" r))
    (.exit js/process 2)))

;; --------------------------------------------------------------- the artifact

(def salt (when (get-in answers [:storage :serial])
            (.toString (.randomBytes crypto 16) "hex")))

(def intent
  (g/answers->intent
   {:answers answers
    :release release
    :ssh-fingerprint (ssh-fingerprint (get-in answers [:ssh :public-key]))
    :serial-sha256 (when salt (serial-digest salt (get-in answers [:storage :serial])))
    :serial-salt salt
    :now-ms (js/Date.now)}))

(write-json! out-intent intent)

;; The answer file that reproduces this run unattended -- subiquity writes the
;; same thing at the end of an interactive install. It is rendered FROM the
;; intent, not from the answers map in memory, so what it reproduces is what
;; was actually authored: a field the render dropped would be visibly absent
;; here instead of silently present in a file nobody re-reads.
(when out-answers
  (write-json! out-answers (g/intent->answers intent)))

(println)
(println "AIUEOS_GUIDED_INTENT" out-intent
         (str "sha256=" (sha256-file out-intent))
         (str "hostname=" (:hostname intent))
         (str "mode=" (:mode intent))
         (str "expires=" (:expires intent))
         (str "fingerprint=" (get-in intent [:ssh :fingerprint])))
(when out-answers
  (println "AIUEOS_GUIDED_ANSWERS" out-answers
           (str "sha256=" (sha256-file out-answers))))
(println "AIUEOS_GUIDED_NEXT"
         "nothing has been erased. On the target machine, the installer runs:")
(println "  AIUEOS_LIVE_PAYLOAD_DEV=<usb-payload-part>"
         "nbb os/aiueos/installer/live/install-live.cljs")
(println "  (interactive intents report only; unattended intents install after")
(println "   install-to-disk.cljs re-verifies this intent against its own probe)")
