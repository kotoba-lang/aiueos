(ns aiueos.session.kami-presenter
  "Browser presenter for the session SPA kami viewport.

  The daily face stays one DADS document. This namespace is the only
  place the SPA is allowed to talk to the GPU: `kami.webgpu/init!` then
  `kami.webgpu/draw!`. A canvas that only `beginRenderPass`+clears is
  the named red (`clear-only-desktop`), not this presenter."
  (:require [kami.webgpu :as gpu]))

(defn ^:export present
  "Draw one kami.webgpu.ir frame onto the canvas. Returns a Promise of a
  plain JS result the vanilla client prints. Does not invent a second
  compositor."
  [canvas ir-js]
  (let [ir (js->clj ir-js :keywordize-keys true)]
    (-> (gpu/init! canvas)
        (.then (fn [ctx]
                 (gpu/draw! ctx ir)
                 (let [backend (if (= :webgl2 (:backend ctx)) "webgl2" "webgpu")]
                   (.setAttribute canvas "data-backend" backend)
                   (.setAttribute canvas "data-executor" "kami.webgpu")
                   #js {:ok true
                        :outcome "admitted"
                        :engine "kami.webgpu"
                        :backend backend
                        :instances (count (:instances ir))}))))))

(defn ^:export install!
  []
  (aset js/window "aiueosKamiPresent" present)
  true)

(defonce installed? (install!))
