(ns aiueos.session-test
  "Network-free checks that the hosted daily shell is one DADS document.

  Live kotobase/murakumo is `clojure -M:session smoke`, not this ns."
  (:require [aiueos.phone-bind :as pb]
            [aiueos.session :as session]
            [clojure.test :refer [deftest is]]))

(deftest p1-document-is-one-dads-spa
  (let [html (pb/session-html)]
    (is (#'session/html-admitted? html))
    (is (re-find #"href=\"#setup\"" html))
    (is (re-find #"href=\"#session\"" html))
    (is (re-find #"href=\"#manage\"" html))
    (is (re-find #"href=\"#devices\"" html))
    (is (re-find #"id=\"?read-cid\"?|id=\"read-cid\"" html))
    (is (re-find #"id=\"run-infer\"" html))
    (is (re-find #"murakumo-main" html))
    (is (re-find #"kotobase.net" html))
    (is (re-find #"window.__aiueosSessionAlive" html))
    (is (re-find #"dads-button" html))
    (is (re-find #"--hig-spacing-4" html)
        "app CSS must speak the --hig-* contract, not a second palette")
    (is (not (re-find #"liquid-glass" html)))
    (is (= 1 (count (re-seq #"<html" html)))
        "one document")))

(deftest p1-live-legs-are-not-the-cli-gate
  (is (re-find #"session-process"
               (slurp "src/aiueos/session/live.clj")))
  (is (not (re-find #"qwen|gemma|claude-|gpt-"
                    (slurp "src/aiueos/session/live.clj")))
      "no hardcoded model id; alias murakumo-main only"))
