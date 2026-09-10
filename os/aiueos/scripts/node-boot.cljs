#!/usr/bin/env nbb
;; What a booted node USB does after the network is up: hold an identity, say
;; what it is, and answer its enrolment challenge.
;;
;; This is the payload entry `/init` hands over to when the stick carries a
;; NODE.JSN. It is not the installer -- an install USB writes a system to a
;; disk and stops; this one boots a box that is trying to become a node.
;;
;; It composes rather than reimplements:
;;   sekisho.didkey        the one definition of what a did:key is
;;   grant.device-attest   the one definition of what a device signs
;;   device-attest-agent   the loop itself, spawned as its own process
;;
;; ⚠ THE KEY IS EPHEMERAL. It is generated into /run, which is a tmpfs, so a
;; reboot produces a different key and therefore a different did. That is
;; honest for a live stick and useless for a durable node: a claim binds an
;; owner to a did, and a did that changes every boot cannot stay claimed.
;; Making it durable means writing the key somewhere that survives, which is a
;; custody decision (kagi's), not something to settle by picking a path here.
;; The marker says which one this boot had.

(require '[sekisho.didkey :as didkey]
         '[clojure.string :as str]
         '["node:crypto" :as crypto]
         '["node:fs" :as fs]
         '["node:path" :as path]
         '["node:child_process" :as cp])

(def state-dir
  ;; /run on the booted stick; overridable so this can be driven on a
  ;; developer machine, where /run does not exist and debugging inside a QEMU
  ;; boot costs minutes per attempt.
  (or (.-AIUEOS_NODE_STATE_DIR js/process.env) "/run/aiueos-node"))
(def key-path (path/join state-dir "device.pem"))
(def config-path
  (or (first *command-line-args*)
      (str (or (.-AIUEOS_LIVE_MEDIA js/process.env) "/payload") "/NODE.JSN")))

(defn- say [& parts] (println (str/join " " parts)))

(defn- die! [code marker detail]
  (say marker detail)
  (js/process.exit code))

;; ── the node's own configuration ──────────────────────────────────────────

(def config
  (try
    (js->clj (js/JSON.parse (fs/readFileSync config-path "utf8")))
    (catch :default e
      (die! 2 "AIUEOS_NODE_CONFIG_UNREADABLE" (str config-path ": " (.-message e))))))

(def endpoint (str/replace (str (get config "endpoint" "")) #"/+$" ""))
(when (str/blank? endpoint)
  (die! 2 "AIUEOS_NODE_CONFIG_NO_ENDPOINT" config-path))

;; ── identity ──────────────────────────────────────────────────────────────

(defn- load-or-generate []
  (fs/mkdirSync state-dir #js {:recursive true})
  (if (fs/existsSync key-path)
    {:key (crypto/createPrivateKey (fs/readFileSync key-path "utf8")) :fresh? false}
    (let [kp (crypto/generateKeyPairSync "ed25519")]
      (fs/writeFileSync key-path (.export (.-privateKey kp) #js {:format "pem" :type "pkcs8"})
                        #js {:mode 0600})   ; 0o600 is JS octal; the Clojure reader wants a leading zero
      {:key (.-privateKey kp) :fresh? true})))

(def loaded (load-or-generate))

(def raw-public
  ;; JWK `x` is the raw 32-byte Ed25519 public key, which is what a did:key
  ;; wraps. Going through the JWK rather than slicing DER keeps this from
  ;; being a second opinion about key encoding.
  (-> (crypto/createPublicKey (:key loaded))
      (.export #js {:format "jwk"})
      (.-x)
      (js/Buffer.from "base64url")
      (js/Array.from)
      vec))

(def did (didkey/from-public-key raw-public))

(when-not (didkey/valid? did)
  (die! 2 "AIUEOS_NODE_DID_INVALID" (str did)))

(say "AIUEOS_NODE_KEY" (if (:fresh? loaded) "generated-ephemeral" "reused-this-boot"))
(say "AIUEOS_NODE_DID" did)
(say "AIUEOS_NODE_ENDPOINT" endpoint)

;; ── answer the challenge ──────────────────────────────────────────────────
;;
;; Spawned rather than required: the agent owns its own exit codes, and a node
;; that reports "the plane refused my signature" differently from "I could not
;; reach the plane" is the whole point of having them.

(def here
  ;; /init `cd`s into the extracted bundle before handing over, so the bundle
  ;; root is the working directory. Resolving it from the script's own module
  ;; path would be a second answer to the same question, and one that depends
  ;; on how nbb was invoked.
  (or (.-AIUEOS_NODE_BUNDLE js/process.env) (js/process.cwd)))

(defn- run-agent []
  ;; stdio is INHERITED, not piped. Piping it and printing after the child
  ;; exits means a child that does not exit prints nothing at all -- which is
  ;; exactly what a boot looks like when it is stuck, and is how this first
  ;; failed: fifteen minutes of serial that ended at the endpoint line.
  (say "AIUEOS_NODE_ATTEST_START")
  (let [r (.spawnSync cp (path/join here "bin" "nbb")
                      (clj->js ["--classpath" (path/join here "cp")
                                (path/join here "device-attest-agent.cljs")
                                "--did" did
                                "--endpoint" endpoint
                                "--key" key-path
                                "--watch"
                                "--interval" (str (get config "intervalSeconds" 5))
                                ;; bounded: a boot has to end with an answer
                                "--attempts" (str (get config "attempts" 10))])
                      #js {:encoding "utf8" :stdio "inherit"})]
    {:code (.-status r) :out ""}))

(let [{:keys [code]} (run-agent)]
  (say "AIUEOS_NODE_ATTEST_EXIT" (str code))
  (say (case code
         0 "AIUEOS_NODE_CLAIMABLE"
         1 "AIUEOS_NODE_SIGNATURE_REFUSED"
         3 "AIUEOS_NODE_PLANE_UNREACHABLE"
         "AIUEOS_NODE_ATTEST_OTHER")
       (str "exit=" code))
  (js/process.exit (if (= 0 code) 0 code)))
