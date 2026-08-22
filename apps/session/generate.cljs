(ns generate
  "SSR `apps/session/index.html` through jp-go-dds. nbb, not sh.
  Invoked from a tree that can see jp-go-dds on the classpath:

    nbb --classpath <session-src>:<dds/src>:<dds/resources>:<html/src>:<css/src> \\
      apps/session/generate.cljs -- --repo <aiueos> --dds-css <dds.css>"
  (:require ["fs" :as fs]
            ["path" :as path]
            [aiueos.session.page :as session-page]
            [aiueos.session.route :as route]))

(defn- arg [flag]
  (second (drop-while #(not= % flag) *command-line-args*)))

(defn -main [& _args]
  (let [repo (or (arg "--repo") ".")
        dds-css-path (or (arg "--dds-css")
                         (path/join "orgs" "kotoba-lang"
                                    "jp-go-digital-design-system"
                                    "resources" "jp_go_dds" "dds.css"))
        client-path (or (arg "--client")
                        (path/join repo "apps" "session" "client.js"))
        out-path (or (arg "--out")
                     (path/join repo "apps" "session" "index.html"))
        resource-path (path/join repo "resources" "aiueos" "session" "index.html")
        dds-css (.readFileSync fs dds-css-path "utf8")
        client (.readFileSync fs client-path "utf8")
        html (session-page/document {:dds-css dds-css :client-js client})
        dir (path/dirname out-path)]
    (when-not (.existsSync fs dir) (.mkdirSync fs dir #js {:recursive true}))
    (.writeFileSync fs out-path html)
    (let [rdir (path/dirname resource-path)]
      (when-not (.existsSync fs rdir) (.mkdirSync fs rdir #js {:recursive true}))
      (.writeFileSync fs resource-path html))
    (println "wrote" out-path "bytes" (count html)
             "views" (pr-str (mapv :fragment route/views)))))

(apply -main *command-line-args*)
