(ns aiueos.compositor.desktop
  "Window surfaces the compositor process owns.

  State is `window-session-state` (kami-os portable compositor: z-stack,
  focus, window lifecycle). This namespace does not invent a second
  compositor and does not draw with CSS 3D. GPU pixels for a guest
  surface are a kami.webgpu.ir-shaped EDN frame hosted in a window's
  `:kami` content; the browser WebGPU canvas is the scanout host."
  (:require [window-session-state :as wss]
            [window-session-state.compositor :as compositor]
            [window-session-state.window :as window]))

(def kami-session-ir
  "A kami.webgpu.ir frame (documented shape in kotoba-lang/webgpu).
  Built as data here so aiueos does not pull the full render-graph
  constructor graph (terrain/building) as a runtime premise. The
  executor remains kami.webgpu in a browser; this is the IR."
  {:engine "kami.webgpu.ir"
   :globals {:sky {:horizon [0.12 0.18 0.28]
                   :sun-dir [-0.4 -0.85 -0.35]
                   :sun [1.0 0.96 0.85]}
             :eye [8.0 6.0 10.0]
             :target [0.0 1.0 0.0]}
   :instances [{:pos [0.0 0.0 0.0]
                :color [0.20 0.45 0.85]
                :size [2.0 3.0 2.0]}
               {:pos [3.2 0.0 1.4]
                :color [0.28 0.55 0.30]
                :size [1.1 2.6 1.1]}]})

(defn empty-desktop
  "A compositor with no surfaces. Restore of a wiped file lands here.
  `restore-admitted?` is false — that is the named red, not a comment."
  []
  (assoc (wss/window-session-state-desktop)
         :kind :empty
         :wiped? true
         :session-fragment nil
         :windows {}))

(defn boot-desktop
  "One session chrome window (the DADS SPA fragment) and one kami guest
  surface. This is a compositor, not a WM: no IME, no decoration protocol."
  []
  (let [base (assoc (wss/window-session-state-desktop)
                    :kind :window-session-state
                    :wiped? false
                    :session-fragment "#session")
        [d _] (wss/open-window
               base
               (window/window-config
                {:app-id "aiueos.session"
                 :title "session"
                 :x 32 :y 32 :w 720 :h 540
                 :content (window/iframe-content "/#session")}))
        [d _] (wss/open-window
               d
               (window/window-config
                {:app-id "aiueos.surface"
                 :title "guest-surface"
                 :x 96 :y 72 :w 640 :h 480
                 :content (window/kami-content kami-session-ir)}))]
    d))

(defn persistable
  [desktop]
  (select-keys desktop [:kind :wiped? :session-fragment :windows
                        :next-window-id :clock :compositor
                        :input-router :taskbar :launcher :notifications]))

(defn restore-admitted?
  "True only when the restored value still owns at least one windowed
  surface under window-session-state. An empty/wiped desktop is red."
  [desktop]
  (boolean
   (and (map? desktop)
        (not (:wiped? desktop))
        (= :window-session-state (:kind desktop))
        (seq (:windows desktop))
        (seq (compositor/z-stack (:compositor desktop))))))

(defn public-snapshot
  "JSON-facing view: surfaces the compositor owns, plus the kami IR.
  Phone bind does not need this; it is the desktop face."
  [desktop]
  (let [windows (:windows desktop)]
    {:kind (name (or (:kind desktop) :empty))
     :admitted? (restore-admitted? desktop)
     :wiped? (boolean (:wiped? desktop))
     :session-fragment (or (:session-fragment desktop) "")
     :focused (compositor/focused-window (:compositor desktop))
     :z-stack (vec (compositor/z-stack (:compositor desktop)))
     :surface-count (count windows)
     :windows (mapv (fn [[id w]]
                      {:id id
                       :app-id (get-in w [:component :app-id])
                       :title (get-in w [:component :title])
                       :state (name (get-in w [:component :state] :normal))
                       :content (let [c (get-in w [:component :content])]
                                  (cond
                                    (vector? c) {:tag (name (first c))
                                                 :data (second c)}
                                    (keyword? c) {:tag (name c)}
                                    :else {:tag "unknown"}))
                       :rect (:rect w)})
                    windows)
     :kami-ir kami-session-ir
     :engine "window-session-state"
     :gpu-viewport "kami.webgpu.ir"
     :note "Named partial: compositor process owns surfaces. Not a WM."}))
