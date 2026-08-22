(ns aiueos.session.page
  "SSR the hosted daily shell as one DADS document.

  `:app-css` is `tokens/skin-css` (bridge `--hig-*` onto DADS primitives, plus
  tap-target / safe-area) then a short unlayered phone shell. Not liquid-glass.
  Not a second design system."
  (:require [jp-go-dds.page :as page]
            [jp-go-dds.tokens :as tokens]
            [aiueos.session.views :as views]))

(def app-css
  "Phone-sized daily chrome. Token contract only — no raw hex, no px type."
  (str
   ".session-shell{max-width:28rem;margin:0 auto;"
   "padding:var(--hig-spacing-4);"
   "color:var(--hig-color-label);"
   "background:var(--hig-color-system-background)}\n"
   ".session-shell:has(#desktop:not([hidden])){max-width:48rem}\n"
   ".kami-viewport{display:block;width:100%;height:12rem;"
   "background:var(--hig-color-secondary-system-background);"
   "border-radius:var(--hig-radius-xs);"
   "margin:var(--hig-spacing-3) 0}\n"
   ".session-chrome{position:sticky;top:0;z-index:1;"
   "padding-bottom:var(--hig-spacing-4);"
   "margin-bottom:var(--hig-spacing-4);"
   "border-bottom:var(--hig-hairline,1px) solid var(--hig-color-separator);"
   "background:var(--hig-color-system-background)}\n"
   ".session-chrome h1{margin:0 0 var(--hig-spacing-3)}\n"
   ".session-nav{flex-wrap:wrap;gap:var(--hig-spacing-2)}\n"
   ".session-lede{color:var(--hig-color-secondary-label);"
   "font-size:var(--hig-text-body-font-size,1rem);"
   "line-height:var(--hig-text-body-line-height,1.5)}\n"
   ".session-out{white-space:pre-wrap;overflow:auto;"
   "min-height:var(--hig-spacing-4);"
   "padding:var(--hig-spacing-3);"
   "background:var(--hig-color-secondary-system-background);"
   "border-radius:var(--hig-radius-xs);"
   "font-size:var(--hig-text-footnote-font-size)}\n"
   ".session-view[hidden]{display:none!important}\n"
   ".dds-ext-card{margin:var(--hig-spacing-4) 0}\n"
   ".session-main .dads-button{width:100%;margin:var(--hig-spacing-3) 0}\n"))

(defn document
  "Full HTML string. `dds-css` is the vendored DADS stylesheet; `client-js`
  is the fragment-router + session-process client, inlined so `/` is one
  HTML document."
  [{:keys [dds-css client-js]}]
  (when (or (nil? dds-css) (zero? (count dds-css)))
    (throw (ex-info "dds.css is required; generate with --dds-css" {})))
  (page/->page
   {:title "aiueos session"
    :description "Hosted daily shell. DADS SPA. kotobase.net and murakumo-main from this document."
    :lang "ja"
    :css dds-css
    :app-css (str tokens/skin-css "\n" app-css)
    :head [[:meta {:name "aiueos-design" :content "jp-go-dds"}]]}
   (views/shell)
   [:script (or client-js "")]))
