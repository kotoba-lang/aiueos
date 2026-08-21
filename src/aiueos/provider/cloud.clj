(ns aiueos.provider.cloud
  "The hosted-profile mechanism behind `grant.cloud`: the part that actually
  opens the connection.

  `grant.cloud` decides which request this policy would allow and whether a
  response is the one that was asked for. This namespace does neither. It takes
  an allowed plan, performs it, and reports **what arrived** — status, byte
  count, and the SHA-256 of the bytes it received. The comparison against the
  digest the CID commits to stays in `grant.cloud/admit-block`, because the
  question \"what did I get\" and the question \"is that what was asked for\"
  have different answers and must not be answered by the same code.

  JVM-only, like `aiueos.provider.device`. No new dependency: `java.net.http`
  is the platform's, and TLS comes with it.

  ## Three things that are mechanism here and would be holes anywhere else

  **Redirects are not followed.** The allowlist checked the URL *we* chose, not
  the one the server names next. A followed 302 would leave the allowlist behind
  while still looking like a successful fetch — the same shape as the murakumo
  alias redirect that `grant.cloud/admit-model` re-checks, arriving one layer
  lower. A redirect is returned as its status and refused by `admit-block`.

  **The body is read against a ceiling.** A hostile or broken endpoint that
  never stops sending would otherwise decide how much memory this machine
  spends. The default ceiling is kotobase's own 4 MiB block limit: a response
  above it cannot be a block this store would have accepted.

  **A request that could not complete is not a response.** Timeouts and I/O
  failures produce `:aiueos.provider.cloud/error`, never a `:digest-hex` and
  never a status. `admit-block` must not be handed something it could mistake
  for a measurement.

  ## Credentials arrive; they are never held

  Any authorization header is an **injected value** in `opts`. This namespace
  has no default, reads no environment variable, and holds no credential of its
  own. Headers are applied to the request and are not carried onto the result,
  so a credential cannot reach a receipt, a log line or an exception message by
  travelling with the thing that was measured. `:post` and `:put` exist here
  because plans emit them; the authority to use them is somebody else's to
  supply.

  ## The platform trust store is replaced, not extended

  An https connection is trusted because its leaf key is one the policy named,
  and for no other reason. The JDK's default trust store — several hundred
  anchors chosen by whoever packaged the runtime — is not consulted: it is a
  reasonable default for a browser reaching hosts nobody enumerated in advance,
  and the wrong one for a machine that talks to two authorities known before it
  boots. The measurement is this namespace's; the verdict is
  `grant.cloud/admit-peer`'s.

  ## What this does not do

  It is the *hosted* profile only; the bare-metal profile still has no TLS or
  HTTP client (ADR-0041 gap ledger, steps 1–5).

  `write-block!` performs the `PUT` that `grant.cloud/plan-block-write`
  plans, and the credential it needs is a **bearer token**, not a CACAO:
  kotobase's block plane admits `Authorization: Bearer <token>` against a
  static operator secret, while CACAO belongs to its tenant datom plane. This
  machine holds no such token, and the live endpoint answers **401** without
  one — measured 2026-08-21. The mechanism exists and is proved against a
  loopback server in both directions; the live write is unauthorised, which is
  a different sentence from unimplemented."
  (:require [grant.cloud :as cloud]
            [grant.json :as json]
            [clojure.string :as str])
  (:import [java.io InputStream]
           [java.net URI]
           [java.net.http HttpClient HttpClient$Redirect HttpClient$Version
                          HttpRequest HttpRequest$BodyPublisher
                          HttpRequest$BodyPublishers HttpRequest$Builder
                          HttpResponse$BodyHandlers]
           [java.security MessageDigest]
           [java.security.cert CertificateException X509Certificate]
           [java.time Duration]
           [javax.net.ssl SSLContext TrustManager X509TrustManager]))

(def default-limits
  "`:max-bytes` is kotobase's own block ceiling: a larger response cannot be a
  block that store would have accepted, so reading further only spends memory.
  The timeouts bound a connection that is never refused and never answered."
  {:max-bytes (* 4 1024 1024)
   :connect-timeout-ms 5000
   :request-timeout-ms 15000})

(def errors
  "Faults this namespace produces. They are distinct from
  `grant.cloud/deny-reasons` on purpose: a decision refused the request, a
  fault means there was nothing to decide about. The peer refusals are
  `grant.cloud` reasons reported through a fault, because a TLS handshake that
  is refused mid-flight surfaces as an I/O failure and would otherwise be
  reported as one."
  #{:plan-not-allowed :net-denied :response-too-large :request-failed
    :method-unsupported :body-unencodable
    :no-trust-anchors :peer-not-pinned :peer-unmeasured})

(defn spki-sha256-hex
  "SHA-256 of a certificate's SubjectPublicKeyInfo — `PublicKey.getEncoded` is
  exactly that DER. The pin is over the key so certificate renewal with the
  same key is not an event, and a key change is."
  [^X509Certificate cert]
  (let [digest (.digest (MessageDigest/getInstance "SHA-256")
                        (.getEncoded (.getPublicKey cert)))]
    (apply str (map #(format "%02x" (bit-and (int %) 0xff)) digest))))

(defn- pinning-trust-manager
  "A trust manager whose only question is `grant.cloud/admit-peer`. The
  verdict is also recorded in VERDICT so the caller can report which refusal
  happened: the handshake failure it causes arrives as an I/O exception that
  says nothing about pins."
  ^TrustManager [policy verdict]
  (reify X509TrustManager
    (getAcceptedIssuers [_] (make-array X509Certificate 0))
    (checkClientTrusted [_ _ _]
      (throw (CertificateException. "aiueos is not a TLS server")))
    (checkServerTrusted [_ chain _]
      (let [leaf (first chain)
            peer {:spki-sha256 (when leaf (spki-sha256-hex leaf))}
            v (cloud/admit-peer policy peer)]
        (reset! verdict v)
        (when-not (cloud/allowed? v)
          (throw (CertificateException.
                  (str "peer refused: " (:aiueos.cloud/reason v)))))))))

(defn- pinning-ssl-context
  ^SSLContext [policy verdict]
  (doto (SSLContext/getInstance "TLS")
    (.init nil (into-array TrustManager [(pinning-trust-manager policy verdict)]) nil)))

(defn- limit [opts k]
  (get opts k (get default-limits k)))

(defn client
  "An HTTP client that does not follow redirects, and trusts exactly the peer
  keys POLICY names. `NEVER` is the builder's default; it is set explicitly
  because a reader cannot see a default, and this one is load-bearing."
  ^HttpClient [policy verdict opts]
  (-> (HttpClient/newBuilder)
      (.version HttpClient$Version/HTTP_1_1)
      (.followRedirects HttpClient$Redirect/NEVER)
      (.sslContext (pinning-ssl-context policy verdict))
      (.connectTimeout (Duration/ofMillis (limit opts :connect-timeout-ms)))
      (.build)))

(defn- read-capped
  "Read at most MAX bytes from STREAM. Returns the bytes, or `::too-large` when
  the stream still had more — the ceiling is refused, not silently truncated,
  because a truncated block hashes to something and that something would be
  reported as a measurement."
  [^InputStream stream ^long max]
  (let [buffer (byte-array 16384)
        out (java.io.ByteArrayOutputStream.)]
    (loop [total 0]
      (let [n (.read stream buffer)]
        (cond
          (neg? n) (.toByteArray out)
          (> (+ total n) max) ::too-large
          :else (do (.write out buffer 0 n)
                    (recur (+ total n))))))))

(defn sha256-hex
  "SHA-256 of BYTES as lowercase hex. The one thing this namespace measures."
  [^bytes bytes]
  (let [digest (.digest (MessageDigest/getInstance "SHA-256") bytes)]
    (apply str (map #(format "%02x" (bit-and (int %) 0xff)) digest))))

(defn- send-request
  "Perform one request and report what arrived. Never throws: a request that
  could not complete is reported as a fault, not as an empty response."
  [^HttpClient http request opts verdict]
  (let [max-bytes (limit opts :max-bytes)]
    (try
      (let [response (.send http request (HttpResponse$BodyHandlers/ofInputStream))]
        (with-open [^InputStream body (.body response)]
          (let [read (read-capped body max-bytes)]
            (if (= ::too-large read)
              {:aiueos.provider.cloud/error :response-too-large
               :max-bytes max-bytes}
              (cond-> {:status (.statusCode response)
                       :bytes read
                       :byte-count (alength ^bytes read)
                       :digest-hex (sha256-hex read)}
                ;; Only when the caller asked. A block is bytes and decoding
                ;; four megabytes of them as text to satisfy a key nobody reads
                ;; is work this machine does not need to do.
                (:decode-body? opts)
                (assoc :body (String. ^bytes read "UTF-8"))

                (:aiueos.cloud/peer-spki @verdict)
                (assoc :peer-spki (:aiueos.cloud/peer-spki @verdict)))))))
      (catch Exception e
        (let [v @verdict]
          (if (and v (not (cloud/allowed? v)))
            ;; The handshake failed because the peer was refused. Report the
            ;; refusal, not the I/O exception it turned into.
            (merge {:aiueos.provider.cloud/error (:aiueos.cloud/reason v)} v)
            {:aiueos.provider.cloud/error :request-failed
             :exception (.getSimpleName (class e))
             :message (.getMessage e)}))))))

(defn- body-publisher
  "The publisher for this request's body, or an error map.

  `:body-bytes` in OPTS is the caller's own bytes and is sent verbatim -- a
  block write must not be reshaped by a serialiser. Otherwise a plan's `:body`
  is data, and `grant.json` turns it into bytes; a value it refuses to encode
  is a fault, because a request whose body could not be built was never made."
  [request opts]
  (cond
    (contains? opts :body-bytes)
    {:publisher (HttpRequest$BodyPublishers/ofByteArray ^bytes (:body-bytes opts))
     :has-body? true}

    (contains? request :body)
    (let [encoded (json/write-json (:body request))]
      (if (json/failed? encoded)
        {:error (json/error-of encoded)}
        {:publisher (HttpRequest$BodyPublishers/ofString ^String encoded)
         :has-body? true}))

    :else {:publisher (HttpRequest$BodyPublishers/noBody) :has-body? false}))

(defn- build-request
  "An `HttpRequest` for REQUEST, or an error map.

  The method comes from the plan, so a plan this namespace has no verb for is
  a fault rather than a silent `GET` -- a write quietly performed as a read
  would report a 200 and store nothing."
  [request opts]
  (let [body (body-publisher request opts)]
    (if (:error body)
      {:error :body-unencodable :detail (:error body)}
      (let [^HttpRequest$BodyPublisher publisher (:publisher body)
            base (-> (HttpRequest/newBuilder)
                     (.uri (URI/create (:url request)))
                     (.timeout (Duration/ofMillis (limit opts :request-timeout-ms))))
            headers (:headers opts)
            with-type (if (and (:has-body? body)
                               (not (some #(= "content-type" (str/lower-case (str %)))
                                          (keys headers))))
                        (.header ^HttpRequest$Builder base "content-type" "application/json")
                        base)
            ^HttpRequest$Builder built
            (reduce (fn [^HttpRequest$Builder acc [k v]] (.header acc (str k) (str v)))
                    with-type
                    headers)]
        (case (:method request)
          :get {:request (.build (.GET built))}
          :post {:request (.build (.POST built publisher))}
          :put {:request (.build (.PUT built publisher))}
          {:error :method-unsupported :detail (:method request)})))))

(defn perform!
  "Execute an allowed PLAN and report what arrived.

  The URL is taken from `grant.cloud/perform`, which re-checks it against the
  allowlist at call time; a plan whose URL stopped being allowed never reaches
  the socket."
  [policy plan opts]
  (if-not (cloud/allowed? plan)
    {:aiueos.provider.cloud/error :plan-not-allowed
     :aiueos.cloud/reason (:aiueos.cloud/reason plan)}
    (let [url (get-in plan [:aiueos.cloud/request :url])
          https? (str/starts-with? (str url) "https://")]
      (if (and https? (not (cloud/anchors-declared? policy)))
        ;; No anchor means no verdict was possible, so there is nothing to
        ;; connect for: refused before the socket rather than after a handshake
        ;; this machine could not have judged.
        {:aiueos.provider.cloud/error :no-trust-anchors :aiueos.cloud/url url}
        (let [verdict (atom nil)
              http (client policy verdict opts)
              outcome (cloud/perform
                       policy plan
                       (fn [request]
                         (let [built (build-request request opts)]
                           (if-let [error (:error built)]
                             {:aiueos.provider.cloud/error error
                              :detail (:detail built)}
                             (send-request http (:request built) opts verdict)))))]
          (if (:ok? outcome)
            (:aiueos.net/result outcome)
            {:aiueos.provider.cloud/error :net-denied
             :aiueos.net/denied (:aiueos.net/denied outcome)
             :aiueos.net/url (:aiueos.net/url outcome)}))))))

(defn read-block!
  "Fetch CID from the storage authority and return `grant.cloud/admit-block`'s
  verdict about what came back, with the bytes attached when it allows.

  A fault short-circuits: if the request could not complete there is nothing to
  admit, and returning the fault is not the same as returning a response with
  no digest. Callers can tell those apart because they are different keys."
  ([policy cid] (read-block! policy cid {}))
  ([policy cid opts]
   (let [plan (cloud/plan-block-read policy cid)]
     (if-not (cloud/allowed? plan)
       plan
       (let [arrived (perform! policy plan opts)]
         (if-let [fault (:aiueos.provider.cloud/error arrived)]
           (assoc arrived :aiueos/decision :deny :aiueos.provider.cloud/error fault)
           (let [verdict (cloud/admit-block plan arrived)]
             (cond-> verdict
               (cloud/allowed? verdict)
               (assoc :aiueos.provider.cloud/bytes (:bytes arrived))

               ;; What arrived, on the refusals too. A receipt that shows the
               ;; status and the byte count only when the answer was yes makes
               ;; a refusal harder to read than a pass, which is backwards.
               (:status arrived) (assoc :aiueos.provider.cloud/status (:status arrived))
               (:byte-count arrived)
               (assoc :aiueos.provider.cloud/byte-count (:byte-count arrived))
               (:peer-spki arrived)
               (assoc :aiueos.provider.cloud/peer-spki (:peer-spki arrived))))))))))

;; ── the two clients ────────────────────────────────────────────────────────
;;
;; Each of these is the same three steps: `grant.cloud` plans, this namespace
;; performs, `grant.cloud` judges. The provider adds nothing to the verdict
;; except what it *observed* -- which peer key it saw, what status arrived, how
;; many bytes -- so a receipt can report the measurement next to the decision
;; without either one having been derived from the other.

(defn- judged
  "Perform PLAN and hand what arrived to ADMIT-FN, carrying this namespace's
  own observations onto the verdict.

  A fault reaches ADMIT-FN as a response with **no status**, which every
  admission in `grant.cloud` refuses as `:response-unmeasured`. That is the
  point: a request that could not complete must not arrive looking like one
  that completed and said nothing."
  [policy plan opts admit-fn]
  (if-not (cloud/allowed? plan)
    plan
    (let [arrived (perform! policy plan opts)
          verdict (admit-fn arrived)]
      (cond-> verdict
        (:aiueos.provider.cloud/error arrived)
        (assoc :aiueos.provider.cloud/error (:aiueos.provider.cloud/error arrived)
               :aiueos.provider.cloud/message (:message arrived))

        (:status arrived) (assoc :aiueos.provider.cloud/status (:status arrived))
        (:byte-count arrived) (assoc :aiueos.provider.cloud/byte-count (:byte-count arrived))
        (:peer-spki arrived) (assoc :aiueos.provider.cloud/peer-spki (:peer-spki arrived))))))

(defn resolve-model!
  "Resolve the model alias and return `grant.cloud/admit-resolution`'s verdict.

  What is admitted is an *endpoint*, not a model id. The alias is the thing
  this machine keeps saying; the id behind it is the fleet's business
  (root ADR-2607173100)."
  ([policy] (resolve-model! policy {}))
  ([policy opts]
   (let [plan (cloud/plan-model-resolve policy)]
     (judged policy plan (assoc opts :decode-body? true)
             #(cloud/admit-resolution policy plan %)))))

(defn ready!
  "Ask the inference authority whether it is answering. Liveness, not
  inference: the verdict carries `:aiueos.cloud/live?` and never a completion."
  ([policy] (ready! policy {}))
  ([policy opts]
   (let [plan (cloud/plan-liveness policy)]
     (judged policy plan opts #(cloud/admit-liveness plan %)))))

(defn infer!
  "POST an inference request for an already-admitted MODEL.

  OPTS is `grant.cloud/plan-inference`'s (`:messages`, `:max-tokens`,
  `:model-override`) plus this namespace's (`:headers` for the credential).
  The credential is the caller's to supply and is never defaulted here."
  ([policy model opts] (infer! policy model opts opts))
  ([policy model plan-opts opts]
   (let [plan (cloud/plan-inference policy model plan-opts)]
     (judged policy plan (assoc opts :decode-body? true)
             #(cloud/admit-inference plan %)))))

(defn write-block!
  "Write BYTES to the storage authority under CID.

  Two decisions, in this order and for a reason. `admit-write-payload` judges
  the bytes *before* the socket: a CID is a claim about bytes and this request
  is the machine making that claim, so bytes that hash to something else never
  leave. Only then does `admit-write` judge what the authority answered.

  The credential is `Authorization: Bearer <token>` and arrives in
  `(:headers opts)`. This namespace mints nothing and defaults nothing; with no
  header the live authority answers 401, which `admit-write` reports as
  `:write-unauthorized`."
  ([policy cid bytes] (write-block! policy cid bytes {}))
  ([policy cid bytes opts]
   (let [plan (cloud/plan-block-write policy cid)]
     (if-not (cloud/allowed? plan)
       plan
       (let [payload (cloud/admit-write-payload plan (sha256-hex bytes))]
         (if-not (cloud/allowed? payload)
           payload
           (judged policy plan (assoc opts :body-bytes bytes)
                   #(cloud/admit-write plan %))))))))
