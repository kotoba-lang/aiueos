(ns aiueos.provider.cloud-test
  "The first bytes this repository has taken off a socket and judged.

  A real HTTP server on loopback, a real request, real SHA-256 over what
  arrived. What is proved here is the seam: the provider reports what it
  received, and `aiueos.cloud` decides whether that was what was asked for —
  including when it was not."
  (:require [aiueos.cloud :as cloud]
            [aiueos.provider.cloud :as provider]
            [clojure.test :refer [deftest is testing]])
  (:import [com.sun.net.httpserver HttpExchange HttpHandler HttpServer]
           [java.net InetSocketAddress]))

;; Computed outside the code under test:
;;   sha256("hello world") = b94d27b9…cde9
(def hello "hello world")
(def hello-digest "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9")
(def raw-cid "bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e")
(def cid-path (str "/ipfs/" raw-cid))

(defn- respond! [^HttpExchange exchange status ^String body]
  (let [bytes (.getBytes body "UTF-8")]
    (.sendResponseHeaders exchange (int status) (alength bytes))
    (with-open [out (.getResponseBody exchange)]
      (.write out bytes))
    (.close exchange)))

(defn- start! [handler]
  (let [server (HttpServer/create (InetSocketAddress. "127.0.0.1" 0) 0)
        hits (atom [])]
    (.createContext server "/"
                    (reify HttpHandler
                      (handle [_ exchange]
                        (swap! hits conj (str (.getPath (.getRequestURI ^HttpExchange exchange))))
                        (handler exchange))))
    (.start server)
    {:server server
     :hits hits
     :origin (str "http://127.0.0.1:" (.getPort (.getAddress server)))}))

(defn- with-server
  "Run BODY against a freshly started loopback server, then stop it. A function
  rather than a macro, so the binding is plain to a reader and to the linter."
  [handler body]
  (let [s (start! handler)]
    (try (body s) (finally (.stop ^HttpServer (:server s) 0)))))

(defn- policy-for
  "The loopback exemption lives in the policy, not in the code: these tests
  prove the seam over plaintext and say so where a reader will see it."
  [origin]
  {:aiueos.policy/net-allow #{"127.0.0.1"}
   :aiueos.cloud/allow-insecure-origins #{"http://127.0.0.1"}
   :aiueos.cloud/storage-origin origin})

(defn- serve [body] (fn [ex] (respond! ex 200 body)))

;; ── the bytes that were asked for ─────────────────────────────────────────

(deftest a-block-that-hashes-to-its-cid-is-admitted-end-to-end
  (with-server (serve hello)
    (fn [s]
      (let [v (provider/read-block! (policy-for (:origin s)) raw-cid)]
        (is (cloud/allowed? v))
        (is (= hello-digest (:aiueos.cloud/digest v)))
        (is (= 11 (:aiueos.provider.cloud/byte-count v)))
        (is (= [cid-path] @(:hits s)) "one request, at the path the CID names")))))

(deftest a-gateway-that-serves-the-wrong-bytes-is-caught
  (with-server (serve "other bytes")
    (fn [s]
      (let [v (provider/read-block! (policy-for (:origin s)) raw-cid)]
        (is (= :digest-mismatch (:aiueos.cloud/reason v)))
        (is (= hello-digest (:aiueos.cloud/expect-digest v)))
        (is (not= hello-digest (:aiueos.cloud/observed-digest v))
            "the digest is of what arrived, not of what was asked for")
        (is (nil? (:aiueos.provider.cloud/bytes v))
            "refused bytes are not handed to the caller")))))

(deftest a-block-that-is-not-there-is-refused-as-a-status
  (with-server (fn [ex] (respond! ex 404 "no"))
    (fn [s]
      (let [v (provider/read-block! (policy-for (:origin s)) raw-cid)]
        (is (= :response-not-ok (:aiueos.cloud/reason v)))
        (is (= 404 (:aiueos.cloud/status v)))))))

;; ── the allowlist is not advisory ─────────────────────────────────────────

(deftest an-origin-outside-the-allowlist-is-never-contacted
  (with-server (serve hello)
    (fn [s]
      (let [policy (assoc (policy-for (:origin s)) :aiueos.policy/net-allow #{"kotobase.net"})
            v (provider/read-block! policy raw-cid)]
        (is (= :origin-not-allowed (:aiueos.cloud/reason v)))
        (is (= [] @(:hits s))
            "the refusal happened before the socket, not after the response")))))

(deftest plaintext-without-the-exemption-never-reaches-the-socket
  (with-server (serve hello)
    (fn [s]
      (let [policy (dissoc (policy-for (:origin s)) :aiueos.cloud/allow-insecure-origins)
            v (provider/read-block! policy raw-cid)]
        (is (= :insecure-transport (:aiueos.cloud/reason v)))
        (is (= [] @(:hits s)))))))

(deftest a-redirect-is-not-followed
  (with-server (fn [^HttpExchange ex]
                 (if (= "/elsewhere" (.getPath (.getRequestURI ex)))
                   (respond! ex 200 hello)
                   (do (.add (.getResponseHeaders ex) "Location" "/elsewhere")
                       (respond! ex 302 ""))))
    (fn [s]
      (let [v (provider/read-block! (policy-for (:origin s)) raw-cid)]
        (is (= :response-not-ok (:aiueos.cloud/reason v)))
        (is (= 302 (:aiueos.cloud/status v)))
        (is (= [cid-path] @(:hits s))
            "the allowlist checked the URL we chose, not the one the server named next")))))

;; ── a request that could not complete is not a response ───────────────────

(deftest a-body-past-the-ceiling-is-a-fault-not-a-measurement
  (with-server (serve (apply str (repeat 4096 "x")))
    (fn [s]
      (let [v (provider/read-block! (policy-for (:origin s)) raw-cid {:max-bytes 1024})]
        (is (= :response-too-large (:aiueos.provider.cloud/error v)))
        (is (nil? (:aiueos.cloud/reason v))
            "a fault is not a deny reason: there was nothing to decide about")
        (is (nil? (:aiueos.cloud/digest v))
            "a truncated block hashes to something, and that something would
             have been reported as a measurement")))))

(deftest a-request-that-times-out-is-a-fault
  (with-server (fn [ex] (Thread/sleep 2000) (respond! ex 200 hello))
    (fn [s]
      (let [v (provider/read-block! (policy-for (:origin s)) raw-cid
                                    {:request-timeout-ms 200})]
        (is (= :request-failed (:aiueos.provider.cloud/error v)))
        (is (nil? (:aiueos.cloud/digest v)))))))

;; ── the provider measures, it does not decide ─────────────────────────────

(deftest the-provider-reports-what-arrived-and-judges-nothing
  (with-server (serve "other bytes")
    (fn [s]
      (let [policy (policy-for (:origin s))
            plan (cloud/plan-block-read policy raw-cid)
            arrived (provider/perform! policy plan {})]
        (is (= 200 (:status arrived)))
        (is (string? (:digest-hex arrived)))
        (is (nil? (:aiueos/decision arrived))
            "perform! returns a measurement; the verdict comes from admit-block")
        (is (= :digest-mismatch (:aiueos.cloud/reason (cloud/admit-block plan arrived))))))))

(deftest sha256-of-known-bytes
  (is (= hello-digest (provider/sha256-hex (.getBytes hello "UTF-8")))
      "the measurement itself, against an oracle computed elsewhere"))

(deftest a-denied-plan-is-not-performed
  (testing "perform! refuses a plan that was never allowed"
    (let [plan (cloud/plan-block-read {:aiueos.policy/net-allow #{}} raw-cid)
          r (provider/perform! {} plan {})]
      (is (= :plan-not-allowed (:aiueos.provider.cloud/error r)))
      (is (= :origin-not-allowed (:aiueos.cloud/reason r))))))
