(ns aiueos.session.views
  "DADS hiccup for each fragment of the hosted daily shell.

  Markup only. Live kotobase / murakumo bytes leave through the session
  process (`/api/session/*`), not from this namespace."
  (:require [jp-go-dds.core :as dds]
            [aiueos.session.route :as route]))

(defn- view-section
  [{:keys [id hidden?]} & children]
  (into [:section (cond-> {:id (name id)
                           :class "dds-ext-section session-view"
                           :data-view (name id)}
                    hidden? (assoc :hidden true))]
        children))

(defn session-view
  "Daily driver: read a CID, complete an inference, from this shell."
  []
  (view-section {:id :session}
    (dds/heading 1 "今日のシェル" {:size "32"})
    [:p {:class "session-lede"}
     "ホストされた aiueos セッションです。CID は "
     [:code "https://kotobase.net"]
     " から、推論は "
     [:code "murakumo-main"]
     " 経由で、この文書のボタンがセッション過程に頼んで動かします。"
     " `clojure -M:cloud-live check` は別の CLI で、この gate ではありません。"]
    (dds/card
     (dds/heading 2 "kotobase を読む" {:size "24"})
     [:p "既定 CID は空バイト列の raw CIDv1。ゲートウェイが 200 と 0 バイトを返し、セッションがハッシュを CID と突き合わせます。"]
     (dds/button "CID を読む" {:id "read-cid" :size "md"})
     [:pre {:id "cid-out" :class "session-out"}])
    (dds/card
     (dds/heading 2 "murakumo で推論する" {:size "24"})
     [:p "モデル id は焼きません。alias は "
      [:code "murakumo-main"]
      "。POST はセッション過程が "
      [:code "infer.murakumo.cloud"]
      " へ送ります。"]
     (dds/button "推論する" {:id "run-infer" :size "md"})
     [:pre {:id "infer-out" :class "session-out"}])
    (dds/card
     (dds/heading 2 "notes guest" {:size "24"})
     [:p "Kotoba guest "
      [:code "app/notes"]
      " は grant を通ったときだけこのシェルに現れます。"
      " 拒否は "
      [:code "unresolved-capability"]
      " など grant の理由で、汎用 500 ではありません。"]
     (dds/button "grant して走らせる" {:id "run-guest" :size "md"})
     (dds/button "grant を拒否する" {:id "deny-guest" :size "md"})
     [:pre {:id "guest-out" :class "session-out"}])))


(defn- wm-titlebar
  [id title]
  [:header {:class "wm-titlebar dds-ext-row"
            :data-raise (str id)}
   (dds/heading 2 title {:size "16" :id (str "wm-title-" id)})
   (dds/chip-label "フォーカス" {:color "blue" :style "filled-1"})
   (dds/button "前面へ" {:id (str "wm-raise-" id)
                        :size "sm"
                        :type :outline
                        :attrs {:data-raise (str id)}})])

(defn- wm-guest-body
  [id title guest-kind src]
  (if (= guest-kind :kami)
    [:canvas {:id "kami-viewport"
              :class "kami-viewport wm-guest"
              :width "640"
              :height "360"
              :data-engine "kami.webgpu.ir"
              :data-executor "kami.webgpu"
              :tabindex "0"
              :aria-label "kami WebGPU guest surface"}]
    [:div {:class "wm-guest"
           :id (str "wm-guest-" id)
           :tabindex "0"
           :data-src (or src "")
           :aria-label title}
     [:p {:class "session-lede"}
      "guest "
      [:code (or src title)]]]))

(defn- wm-window
  "One stacked surface with a DADS title bar. Guest body is not a nested
  copy of this SPA. Kami content is the WebGPU canvas."
  [{:keys [id title guest-kind src]}]
  [:article {:class "wm-window"
             :id (str "wm-window-" id)
             :data-window-id (str id)
             :data-guest (name guest-kind)}
   (wm-titlebar id title)
   [:div {:class "wm-body"}
    (wm-guest-body id title guest-kind src)]])

(defn desktop-view
  "Window manager face: two overlapping surfaces, DADS title bars,
  z-order from window-session-state. Not a JSON dump of one iframe."
  []
  (view-section {:id :desktop :hidden? true}
    (dds/heading 1 "デスクトップ" {:size "32"})
    [:p {:class "session-lede"}
     "compositor 過程が "
     [:code "window-session-state"]
     " の 2 枚を重ねます。タイトルバーは DADS（"
     [:code "jp-go-dds"]
     "）です。前面へで z-order が変わります。"
     " virtio-gpu 2D は "
     [:code "clojure -M:compositor gpu"]
     "。IME は romaji からかなへ（"
     [:code "clojure -M:compositor ime"]
     "）。IME はかなと漢字（"
     [:code "clojure -M:compositor kanji"]
     "）。ゲスト側 IME は "
     [:code "clojure -M:compositor guest-ime"]
     "（KERNEL.ELF の Kotoba）。ゲスト側 WM は "
     [:code "clojure -M:compositor guest-wm"]
     "（KERNEL.ELF の Kotoba が z-hit）。ゲスト側 paint は "
     [:code "clojure -M:compositor guest-paint"]
     "（KERNEL.ELF が z-order で 2 枚を塗る）。ゲスト側 input は "
     [:code "clojure -M:compositor guest-input"]
     "（KERNEL.ELF が virtio-keyboard の used-ring を読む）。ゲスト側 gpu-two は "
     [:code "clojure -M:compositor guest-gpu-two"]
     "（KERNEL.ELF が Kotoba の n=2 で 2 枚の virtio-gpu 2D resource を flush）。native compositor は leftover です。描画は "
     [:code "kami.webgpu"]
     " の "
     [:code "clojure -M:compositor kami"]
     " です。"]
    [:div {:id "ime-bar"
           :class "ime-bar dds-ext-row"
           :data-ime "on"
           :aria-label "input method"}
     (dds/chip-label "かな" {:color "blue" :style "filled-1"})
     (dds/heading 2 "変換中" {:size "16" :id "ime-preedit-label"})
     [:p {:id "ime-preedit" :class "session-lede" :aria-live "polite"}]
     [:p {:id "ime-buf" :class "session-lede"}]
     [:div {:id "ime-candidates" :aria-label "kanji candidates"}]
     (dds/button "IME 切" {:id "ime-toggle" :size "sm" :type :outline})]
    [:div {:id "wm-stage"
           :class "wm-stage"
           :tabindex "0"
           :aria-label "window-session-state"}
     (wm-window {:id 1 :title "session" :guest-kind :session :src "/#session"})
     (wm-window {:id 2 :title "guest-surface" :guest-kind :kami :src "kami.webgpu.ir"})]
    [:pre {:id "compositor-out" :class "session-out" :hidden true}]
    [:pre {:id "guest-desktop-out" :class "session-out"}]
    [:pre {:id "kami-out" :class "session-out"}]
    [:pre {:id "wm-input-out" :class "session-out"}]))

(defn setup-view
  []
  (view-section {:id :setup :hidden? true}
    (dds/heading 1 "機械を紐づける" {:size "32"})
    [:p {:class "session-lede"}
     "モニタもキーボードも使いません。筐体 QR の代わりにホストが印刷した payload です。"]
    [:pre {:id "qr" :class "session-out"}]
    (dds/form-field {:label "名前 / 所有者" :for "owner"}
                    (dds/input-text {:id "owner" :name "owner"
                                     :value "acct:local-demo"
                                     :autocomplete "username"}))
    (dds/button "電話から紐づける" {:id "bind" :size "md"})
    [:pre {:id "bind-out" :class "session-out"}]))

(defn manage-view
  []
  (view-section {:id :manage :hidden? true}
    (dds/heading 1 "管理" {:size "32"})
    [:p {:class "session-lede"}
     "電源サイクルは電話面から。ゲスト VGA は使いません。"]
    [:pre {:id "status" :class "session-out"}]
    (dds/button "電源サイクル" {:id "cycle" :size "md"})
    [:pre {:id "cycle-out" :class "session-out"}]))

(defn devices-view
  []
  (view-section {:id :devices :hidden? true}
    (dds/heading 1 "機械" {:size "32"})
    [:p {:class "session-lede"}
     "このホストの非権威 ledger。本番の check-in は kotobase.net。D1 ではない。"]
    [:pre {:id "devices-out" :class "session-out"}]))

(defn operator-view
  "P4: live itonami.cloud from this same document. Consumer kotobase
  / murakumo legs do not use this fragment."
  []
  (view-section {:id :operator :hidden? true}
    (dds/heading 1 "運用" {:size "32"})
    [:p {:class "session-lede"}
     "産業側は "
     [:code "itonami.cloud"]
     "（network-awai/cloud-itonami）です。kotobase でも murakumo でもありません。"
     " grant が無い消費者セッションはここへ HTTP しません。"
     " `#itonami` も同じ面です。"]
    (dds/card
     (dds/heading 2 "itonami に聞く" {:size "24"})
     [:p "この過程が "
      [:code "/api/health"]
      " と "
      [:code "/api/fleet/metrics"]
      " を "
      [:code "itonami.cloud"]
      " へ GET します。拒否は "
      [:code "operator-grant-required"]
      " です。"]
     (dds/button "運用 grant して読む" {:id "run-operator" :size "md"})
     (dds/button "運用 grant を拒否する" {:id "deny-operator" :size "md"})
     [:pre {:id "operator-out" :class "session-out"}])))

(defn shell
  "The one document body. Header nav is generated from `route/views`."
  []
  [:div {:class "session-shell"}
   [:header {:class "session-chrome"}
    (dds/heading 1 "aiueos" {:size "20" :id "session-brand"})
    (route/nav :session)]
   [:main {:class "session-main"}
    (session-view)
    (desktop-view)
    (setup-view)
    (manage-view)
    (devices-view)
    (operator-view)]])
