(ns aiueos.session.route
  "Addressable views of the hosted daily shell.

  One document, fragment routes (root ADR-2608080100 / adr-2608221625).
  The nav is generated from `views` so a view cannot exist without being
  reachable. Unknown, empty and nil fragments land on the default rather
  than rendering nothing."
  (:require [jp-go-dds.core :as dds]
            [clojure.string :as str]))

(def views
  "Nav order. `:session` is the daily-driver default; `#setup` is the
  phone-first bind (P1b); `#manage` / `#devices` are day-2."
  [{:id :session :fragment "#session" :label "セッション"}
   {:id :setup :fragment "#setup" :label "セットアップ"}
   {:id :manage :fragment "#manage" :label "管理"}
   {:id :devices :fragment "#devices" :label "機械"}])

(def default-view (first views))

(defn- normalize-fragment
  [fragment]
  (let [raw (or fragment "")
        cut (first (str/split raw #"\?"))
        with-hash (cond
                    (str/blank? cut) ""
                    (str/starts-with? cut "#") cut
                    :else (str "#" cut))
        stripped (str/replace with-hash #"^#/" "#")]
    (if (or (str/blank? stripped) (= "#" stripped) (= "#/" stripped))
      (:fragment default-view)
      stripped)))

(defn fragment->view
  [fragment]
  (let [f (normalize-fragment fragment)]
    (or (first (filter #(= (:fragment %) f) views))
        (first (filter #(str/starts-with? f (str "#" (name (:id %)))) views))
        default-view)))

(defn nav
  "Real links that are still DADS controls. No app CSS for the switcher."
  [active-id]
  (into [:nav {:class "dds-ext-row session-nav" :aria-label "Views"}]
        (for [{:keys [id fragment label]} views
              :let [active? (= id active-id)]]
          (dds/button label {:type (if active? :solid-fill :text)
                             :size "sm"
                             :href fragment
                             :attrs (cond-> {}
                                      active? (assoc :aria-current "page"))}))))
