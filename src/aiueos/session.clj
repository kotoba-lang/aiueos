(ns aiueos.session
  "P1 hosted daily shell gate. Boots the DADS SPA over HTTP and, from that
  same process, reads a kotobase CID and completes a murakumo infer.

  Exit 0 = SPA served and both live legs admitted.
  Exit 1 = a leg was refused, or the document is not the DADS SPA.
  Exit 3 = a leg could not be answered.

  QEMU is not this gate. Phone bind is `clojure -M:phone-bind smoke`."
  (:require [aiueos.phone-bind :as pb])
  (:import [java.io File]))

(defn- html-admitted?
  [html]
  (and (re-find #"dads-button" html)
       (re-find #"jp-go-dds" html)
       (re-find #"href=\"#setup\"" html)
       (re-find #"href=\"#session\"" html)
       (re-find #"href=\"#manage\"" html)
       (re-find #"window.__aiueosSessionAlive" html)
       (not (re-find #"liquid-glass" html))))

(defn- outcome-of-http
  [{:keys [code parsed]}]
  (or (:outcome parsed)
      (cond
        (= 200 code) "admitted"
        (= 503 code) "unmeasured"
        :else "refused")))

(defn run-smoke
  "HTTP only. No guest display. Hits the SPA, then the two live legs through
  the session process — not `clojure -M:cloud-live check`."
  [{:keys [dir]}]
  (let [dir (File. (or dir (str (System/getProperty "java.io.tmpdir")
                                "/aiueos-session-p1")))
        rt (pb/start-http! (pb/make-runtime {:dir dir :listen-port 0}))
        base (pb/base-url rt)]
    (try
      (let [page (pb/http-get (str base "/"))
            html (:body page)
            spa? (and (= 200 (:code page)) (html-admitted? html))
            setup (pb/http-get (str base "/#setup"))
            read (pb/http-post (str base "/api/session/read-cid") {}
                               {:read-timeout-ms 60000})
            infer (pb/http-post (str base "/api/session/infer") {}
                                {:read-timeout-ms 180000})
            read-out (outcome-of-http read)
            infer-out (outcome-of-http infer)
            read-ok? (and (= "admitted" read-out)
                          (= "session-process" (get-in read [:parsed :via])))
            infer-ok? (and (= "admitted" infer-out)
                           (string? (get-in infer [:parsed :completion]))
                           (= "murakumo-main" (get-in infer [:parsed :alias])))]
        (println (str "AIUEOS_SESSION_URL=" base "/#session"))
        (println (str "AIUEOS_SESSION_SPA=" (if spa? "admitted" "refused")))
        (println (str "AIUEOS_SESSION_SETUP_DOC=" (:code setup)))
        (println (str "AIUEOS_SESSION_KOTOBASE=" (:body read)))
        (println (str "AIUEOS_SESSION_INFER=" (:body infer)))
        (when (and spa? read-ok? infer-ok?)
          (println "AIUEOS_SESSION_OK"))
        {:exit (cond
                 (not spa?) 1
                 (or (= "refused" read-out) (= "refused" infer-out)) 1
                 (or (= "unmeasured" read-out) (= "unmeasured" infer-out)) 3
                 (not (and read-ok? infer-ok?)) 1
                 :else 0)
         :read read :infer infer :spa? spa?})
      (finally
        (pb/stop-http! rt)))))

(defn -main
  [& args]
  (let [cmd (or (first args) "smoke")
        dir (or (System/getenv "AIUEOS_SESSION_DIR")
                (.getPath (File. (System/getProperty "java.io.tmpdir")
                                 "aiueos-session-p1")))]
    (case cmd
      "serve"
      (let [rt (pb/start-http! (pb/make-runtime {:dir dir :listen-port 0}))]
        (pb/print-chassis! rt)
        (println "listening" (pb/base-url rt) "#session Ctrl-C to stop")
        (.addShutdownHook (Runtime/getRuntime)
                          (Thread. #(pb/stop-http! rt)))
        @(promise))

      ("smoke" "check")
      (let [r (run-smoke {:dir dir})]
        (flush)
        (System/exit (int (:exit r))))

      (do (println "usage: clojure -M:session [smoke|serve]")
          (System/exit 3)))))
