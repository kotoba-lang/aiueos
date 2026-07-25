(ns aiueos.component-abi
  "Authority-side translation for the portable Kotoba Component ABI.

   This namespace has no provider implementation. It only says which named
   Component imports Aiueos may decide and which existing Aiueos capability a
   successful decision must contain. Kototama still owns engine-specific
   bindings."
  (:require [kotoba.abi.contract :as abi]))

(def component-import->capability
  {(abi/component-import-key 6) :log/write
   (abi/component-import-key 7) :clock/monotonic})

(defn capability-for-import [component-import]
  (get component-import->capability component-import))

(defn requested-capabilities!
  "Translate a closed set of ABI imports into Aiueos capabilities. An unknown
  import is denied before policy evaluation, so it cannot become ambient host
  authority through a fallback mapping."
  [imports]
  (when-not (set? imports)
    (throw (ex-info "Component imports must be a set"
                    {:phase :aiueos-component-abi})))
  (let [capabilities (mapv capability-for-import imports)]
    (when (some nil? capabilities)
      (throw (ex-info "Component import has no Aiueos authority mapping"
                      {:phase :aiueos-component-abi
                       :imports imports})))
    (set capabilities)))

(defn decision-grants-imports?
  "True only when a grant decision contains every authority needed by IMPORTS.
  Extra Aiueos capabilities do not become Component bindings; Kototama still
  binds exactly the artifact's declared import set."
  [decision imports]
  (and (= :grant (:aiueos/decision decision))
       (let [requested (requested-capabilities! imports)
             granted (set (:aiueos/capabilities decision))]
         (every? granted requested))))
