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

  ## The platform trust store is replaced, not extended

  An https connection is trusted because its leaf key is one the policy named,
  and for no other reason. The JDK's default trust store — several hundred
  anchors chosen by whoever packaged the runtime — is not consulted: it is a
  reasonable default for a browser reaching hosts nobody enumerated in advance,
  and the wrong one for a machine that talks to two authorities known before it
  boots. The measurement is this namespace's; the verdict is
  `grant.cloud/admit-peer`'s.

  ## What this does not do

  It does not write: `PUT /ipfs/:cid` needs
  a CACAO-authenticated caller and no credential path exists here. And it is the
  *hosted* profile only; the bare-metal profile still has no TLS or HTTP client
  (ADR-0041 gap ledger, steps 1–5)."
  (:require [grant.cloud :as cloud]
            [clojure.string :as str])
  (:import [java.io InputStream]
           [java.net URI]
           [java.net.http HttpClient HttpClient$Redirect HttpClient$Version
                          HttpRequest HttpResponse$BodyHandlers]
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
                         (let [builder (-> (HttpRequest/newBuilder)
                                           (.uri (URI/create (:url request)))
                                           (.timeout (Duration/ofMillis (limit opts :request-timeout-ms)))
                                           (.GET))]
                           (send-request http (.build builder) opts verdict))))]
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
               (assoc :aiueos.provider.cloud/bytes (:bytes arrived)
                      :aiueos.provider.cloud/byte-count (:byte-count arrived))
               (:peer-spki arrived)
               (assoc :aiueos.provider.cloud/peer-spki (:peer-spki arrived))))))))))
