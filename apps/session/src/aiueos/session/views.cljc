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
     [:pre {:id "infer-out" :class "session-out"}])))


(defn desktop-view
  "Compositor face: HTML chrome lists window-session-state surfaces.
  The kami WebGPU canvas is the guest scanout host — not CSS 3D, not a
  second engine. Overlay chrome stays DADS."
  []
  (view-section {:id :desktop :hidden? true}
    (dds/heading 1 "デスクトップ" {:size "32"})
    [:p {:class "session-lede"}
     "compositor 過程が "
     [:code "window-session-state"]
     " の surface を持ちます。これはウィンドウマネージャではありません。"
     " IME も virtio-gpu の 2D create/flush もまだです。"
     " 下のキャンバスは "
     [:code "kami.webgpu.ir"]
     " の scanout です。"]
    (dds/card
     (dds/heading 2 "compositor surfaces" {:size "24"})
     [:pre {:id "compositor-out" :class "session-out"}])
    (dds/card
     (dds/heading 2 "kami viewport" {:size "24"})
     [:canvas {:id "kami-viewport"
               :class "kami-viewport"
               :width "640"
               :height "360"
               :data-engine "kami.webgpu.ir"
               :aria-label "kami WebGPU guest surface"}]
     [:pre {:id "kami-out" :class "session-out"}])))

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
    (devices-view)]])
