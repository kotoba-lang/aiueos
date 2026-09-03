(ns aiueos.device-auth
  "Pure OS account and device-claim ceremony.

  Passkeys and phone scans are two front doors into the same state machine.
  The verifier is an injected authority port: this namespace never trusts a
  browser supplied `verified=true`, never handles a private passkey, and never
  copies a passkey private key onto the node.  A claim becomes a node plan only
  after both account authentication and proof by the device-owned key."
  (:require [kotoba.security.information-flow :as sec-info-flow]))

(def version 1)

(def supported-methods #{:passkey :phone-scan})

(defn factory
  [{:keys [device-did model rp-id origin authority]}]
  {:aiueos.device-auth/version version
   :aiueos.device-auth/state :factory
   :device/did device-did
   :device/model model
   :auth/rp-id rp-id
   :auth/origin origin
   :auth/authority authority
   :auth/challenge nil
   :auth/challenge-expires-at-ms nil
   :auth/challenge-consumed? false
   :auth/method nil
   :account/principal-id nil
   :account/did nil
   :device/key-proved? false})

(defn issue-challenge
  "Issue one bounded, single-use challenge. `challenge` is public entropy, not
  an account credential or an enrollment bearer token."
  [state {:keys [challenge now-ms ttl-ms]}]
  (if (not= :factory (:aiueos.device-auth/state state))
    (assoc state :aiueos.device-auth/decision :deny
                 :aiueos.device-auth/reason :challenge/already-issued)
    (assoc state
           :aiueos.device-auth/state :challenge-issued
           :aiueos.device-auth/decision :continue
           :auth/challenge challenge
           :auth/challenge-expires-at-ms (+ now-ms ttl-ms)
           :auth/challenge-consumed? false)))

(defn- deny [state reason]
  (assoc state
         :aiueos.device-auth/decision :deny
         :aiueos.device-auth/reason reason))

(defn- proof-problem
  [state proof now-ms]
  (cond
    (not= :challenge-issued (:aiueos.device-auth/state state))
    :challenge/not-issued

    (:auth/challenge-consumed? state)
    :challenge/already-consumed

    (>= now-ms (:auth/challenge-expires-at-ms state))
    :challenge/expired

    (not= (:auth/challenge state) (:auth/challenge proof))
    :challenge/mismatch

    (not (contains? supported-methods (:auth/method proof)))
    :method/unsupported

    (not (:authority/verified? proof))
    :authority/not-verified

    (not (:auth/user-present? proof))
    :passkey/user-not-present

    (not (:auth/user-verified? proof))
    :passkey/user-not-verified

    (not= (:auth/rp-id state) (:auth/rp-id proof))
    :passkey/rp-id-mismatch

    (not= (:auth/origin state) (:auth/origin proof))
    :passkey/origin-mismatch

    (and (= :passkey (:auth/method proof))
         (not (:passkey/sign-count-ok? proof)))
    :passkey/clone-signal

    (and (= :phone-scan (:auth/method proof))
         (not (:phone/passkey-verified? proof)))
    :phone/passkey-required

    (and (= :phone-scan (:auth/method proof))
         (not (:phone/approved? proof)))
    :phone/not-approved

    (and (= :phone-scan (:auth/method proof))
         (not (string? (:phone/device-did proof))))
    :phone/device-identity-required

    (not (string? (:account/did proof)))
    :account/did-required

    (not (string? (:account/principal-id proof)))
    :account/principal-id-required

    :else nil))

(defn authenticate-account
  "Accept the result of a cryptographic authority adapter.

  The adapter must bind the verified assertion to this challenge, RP, origin,
  and account DID. A phone scan is not a weaker password path: the phone must
  itself have completed a passkey ceremony before approving the device."
  [state proof now-ms]
  (if-let [problem (proof-problem state proof now-ms)]
    (deny state problem)
    (assoc state
           :aiueos.device-auth/state :account-authenticated
           :aiueos.device-auth/decision :continue
           :auth/method (:auth/method proof)
           :account/principal-id (:account/principal-id proof)
           :account/did (:account/did proof)
           :passkey/credential-id (:passkey/credential-id proof)
           :passkey/sign-count (:passkey/sign-count proof)
           :phone/device-did (:phone/device-did proof)
           :auth/authenticated-at-ms now-ms)))

(defn prove-device
  "Bind the authenticated account to the key owned by the device being added.
  `proof-valid?` is produced by the device-key verifier port."
  [state {:keys [device-did public-key proof-valid?]} now-ms]
  (cond
    (not= :account-authenticated (:aiueos.device-auth/state state))
    (deny state :account/not-authenticated)

    (not= (:device/did state) device-did)
    (deny state :device/did-mismatch)

    (not proof-valid?)
    (deny state :device/possession-proof-invalid)

    (not (string? public-key))
    (deny state :device/public-key-required)

    :else
    (assoc state
           :aiueos.device-auth/state :claimed
           :aiueos.device-auth/decision :grant
           :auth/challenge-consumed? true
           :device/key-proved? true
           :device/public-key public-key
           :device/claimed-at-ms now-ms)))

(defn claimed? [state]
  (and (= :grant (:aiueos.device-auth/decision state))
       (= :claimed (:aiueos.device-auth/state state))
       (:auth/challenge-consumed? state)
       (:device/key-proved? state)))

(defn node-plan
  "Project a completed claim into bounded intents. This does not report the
  native Kekkai adapter, SSD storage, or Murakumo workload as live before each
  has its own runtime proof."
  [state]
  (if-not (claimed? state)
    {:aiueos.device-auth/decision :deny
     :aiueos.device-auth/reason :device/not-claimed}
    {:aiueos.device-auth/decision :grant
     :account {:principal-id (:account/principal-id state)
               :did (:account/did state)
               :sync :ready-to-append
               :authority (:auth/authority state)
               :private-key-copied? false}
     :device {:did (:device/did state)
              :model (:device/model state)
              :public-key (:device/public-key state)
              :auth-method (:auth/method state)}
     :ui {:engine :kotoba-lang/browser
          :surface :aiueos/session
          :hosted-html-js :verification-adapter-only}
     :murakumo {:node-state :pending-runtime-proof
                :ready? false
                :required-proof [:authenticated-heartbeat
                                 :real-job-result
                                 :reboot-recovery]}
     :kekkai {:node-state :pending-native-adapter
              :transport :noise-ik
              :required-proof [:device-key-custody :netmap-signature
                               :peer-handshake]}
     :storage {:local-cache :pending-ssd-install
               :replication :opt-in-not-started
               :contracts [:kotobase-storage/cid-block
                           :kotobase-storage-pack/car-v2]
               :plaintext-cloud-copy? false}}))
