(ns aiueos.kotoba-browser-smoke
  "Render the generated aiueos session through kotoba-lang/browser itself.

  This is an integration smoke for a west workspace where browser and its
  local dependencies are checked out. It deliberately does not use Chrome or
  Playwright as the engine."
  (:require [browser.core :as browser]
            [browser.desktop-backend :as desktop]
            [clojure.java.io :as io]
            [clojure.string :as str]))

(defn -main [& [html-path]]
  (let [html-file (io/file (or html-path "apps/session/index.html"))
        html (slurp html-file)
        page (browser/load-html {:url "kotoba://aiueos/session"
                                 :html html
                                 :viewport [1280 800]})
        frame {:frame/contract-version desktop/contract-version
               :frame/workspace-id "main"
               :frame/viewport [1280 800]
               :frame/draw-ops (vec (:browser/draw-ops page))}
        result (desktop/present (desktop/empty-desktop {}) frame)
        ok? (and (seq (:browser/draw-ops page))
                 (= :frame/present (get-in result [:effects 0 :backend/op]))
                 (str/includes? html "aiueos-ui-engine")
                 (str/includes? html "kotoba-lang/browser"))]
    (println (pr-str {:outcome (if ok? :admitted :refused)
                      :engine :kotoba-lang/browser
                      :runtime :kotoba-clj/wasm
                      :url (:browser/url page)
                      :title (:browser/title page)
                      :draw-ops (count (:browser/draw-ops page))
                      :frame-sequence (get-in result [:desktop :desktop/frame-sequence])
                      :hosted-html-js-counts-as-native? false}))
    (when-not ok? (System/exit 1))))
