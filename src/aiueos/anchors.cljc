(ns aiueos.anchors
  "How the pin set gets to a device, and how it changes.

  Contract: `resources/aiueos/anchors_contract.edn`. Pure decisions; fetching
  and signature verification are provider mechanism, as in `aiueos.publisher`.

  ## The hole this namespace closes

  ADR-0044 made an https peer trustworthy only if its key is in
  `:aiueos.cloud/trust-anchors`. It recorded no pin for anything real, which
  left the machine strictly worse off than before: **a pinning client with no
  pin distribution is a client that cannot connect.** This is the distribution.

  ## It is a release, so it is admitted like one

  An anchor set is a signed, sequenced document that changes what the machine
  will trust — the same shape as a release, with the same four attacks against
  it (one stolen key, a key stolen before it was known to be, rollback to a set
  whose key is now public, and freeze). `aiueos.publisher` already answers all
  four, so this namespace **composes** it rather than growing a second, weaker
  copy. Publisher reasons pass through unchanged, for the reason `aiueos.ota`
  gives: a caller reading a verdict should not have to guess which layer
  refused.

  ## What is true of anchor sets and of nothing else

  **An empty set is not a permissive set, it is a brick.** A machine whose pin
  set is empty reaches nothing, so a perfectly signed empty set is refused —
  the one denial that protects the fleet from its own publisher.

  **Replacing every anchor at once is a one-way door.** If the new keys are
  wrong, the machine cannot reach anything to be told so, including the
  correction. So an ordinary rotation must **overlap**: keep at least one key
  that works today, ship the new one alongside it, switch the server, retire
  the old one. Three admitted sets, none of which can strand a device.

  A compromise cannot overlap — dropping the stolen key is the entire point.
  That path exists, is called `:break-glass?`, needs **every** root key rather
  than the threshold, and the verdict says `:one-way? true` so the operator is
  told what they are doing. Break-glass also drops the previous set
  *immediately* rather than keeping it through the overlap window, because a
  window in which the stolen key still works is the thing being escaped.

  ## Trust on first use is not available

  A device with no anchors cannot be handed one over a connection it has no way
  to judge. The first set ships **in the image**, covered by the release
  signature that already exists, and is marked `:bootstrap?`. Anything else
  arriving at a device with no current set is refused with `:no-current-set`.

  ## A machine that cannot fetch keeps what it has

  `keep-using?` mirrors `aiueos.publisher/keep-running?` and for the same
  reason: expiring the pin set because an anchor server is unreachable would
  turn a network outage into a fleet outage. Freshness gates **admission of a
  new set**, never continued use of the current one."
  (:require [aiueos.publisher :as publisher]
            [clojure.set :as set]))

(def deny-reasons
  "Reasons this namespace produces. Reasons from `aiueos.publisher` pass
  through unchanged."
  #{:set-id-missing :anchor-set-empty :no-current-set
    :disjoint-without-break-glass :break-glass-below-threshold})

(def default-policy
  "`:overlap-window-ms` is how long the previous set stays usable after an
  ordinary rotation — long enough for a fleet to converge, short enough that a
  retired key is actually retired. `:break-glass-threshold` defaults to *every*
  key the root names, because break-glass is the one operation that can strand
  a device."
  {:overlap-window-ms (* 1000 60 60 24 7)
   :break-glass-threshold nil})

(defn- deny [reason extra]
  (merge {:aiueos/decision :deny :aiueos.anchors/reason reason} extra))

(defn- admit [extra]
  (merge {:aiueos/decision :grant} extra))

(defn admitted? [verdict] (= :grant (:aiueos/decision verdict)))

(defn usable-anchors
  "The pins this machine will accept right now — what belongs in
  `:aiueos.cloud/trust-anchors`.

  During an ordinary rotation's overlap window that is the union of the current
  and previous sets, so the switchover is not a flag day. After the window it is
  the current set alone: a previous set that never expires is a key that was
  never retired."
  [{:keys [current-anchors previous-anchors accept-previous-until-ms now-ms]}]
  (let [current (set current-anchors)
        previous (set previous-anchors)]
    (if (and (seq previous) accept-previous-until-ms now-ms
             (<= now-ms accept-previous-until-ms))
      (set/union current previous)
      current)))

(defn keep-using?
  "May a machine that cannot reach the anchor publisher keep using the set it
  has? Yes, whenever it has one. This never admits anything new."
  [state]
  (boolean (seq (:current-anchors state))))

(defn admit-set
  "Decide whether PROPOSED may become this machine's pin set.

  `proposed` — `{:set-id :sequence :anchors #{spki-hex} :signatures
                 :timestamp-ms :digest-matches? :break-glass? :bootstrap?}`
  `state`    — `aiueos.publisher`'s state plus `:current-anchors`.

  Structural problems first, then publisher trust, then the two questions only
  an anchor set raises."
  ([proposed state] (admit-set proposed state default-policy))
  ([proposed state policy]
   (let [policy (merge default-policy policy)
         anchors (set (:anchors proposed))
         current (set (:current-anchors state))
         root-keys (count (:keys (:root state)))
         glass-threshold (or (:break-glass-threshold policy) root-keys)]
     (cond
       (nil? (:set-id proposed))
       (deny :set-id-missing {})

       (empty? anchors)
       (deny :anchor-set-empty {:aiueos.anchors/set-id (:set-id proposed)})

       :else
       (let [verdict (publisher/admit-release
                      {:sequence (:sequence proposed)
                       :signatures (:signatures proposed)
                       :artifact-digests-match? (:digest-matches? proposed)
                       :timestamp-ms (:timestamp-ms proposed)}
                      state
                      policy)]
         (if-not (publisher/admitted? verdict)
           verdict
           (let [live (count (:aiueos.publisher/live verdict))
                 overlap (set/intersection anchors current)]
             (cond
               (and (empty? current) (not (true? (:bootstrap? proposed))))
               (deny :no-current-set {:aiueos.anchors/set-id (:set-id proposed)})

               (and (seq current) (empty? overlap) (not (true? (:break-glass? proposed))))
               (deny :disjoint-without-break-glass
                     {:aiueos.anchors/set-id (:set-id proposed)
                      :aiueos.anchors/current (vec (sort current))
                      :aiueos.anchors/proposed (vec (sort anchors))})

               (and (seq current) (empty? overlap) (< live glass-threshold))
               (deny :break-glass-below-threshold
                     {:aiueos.anchors/set-id (:set-id proposed)
                      :aiueos.anchors/live live
                      :aiueos.anchors/required glass-threshold})

               :else
               (admit {:aiueos.anchors/set-id (:set-id proposed)
                       :aiueos.anchors/sequence (:sequence proposed)
                       :aiueos.anchors/anchors anchors
                       :aiueos.anchors/overlap (vec (sort overlap))
                       :aiueos.anchors/one-way? (boolean (and (seq current) (empty? overlap)))
                       :aiueos.anchors/bootstrap? (true? (:bootstrap? proposed))
                       :aiueos.publisher/live live})))))))))

(defn apply-set
  "The state after VERDICT admits a set. Pure: persisting it is the provider's.

  An ordinary rotation keeps the previous set usable for the overlap window. A
  break-glass rotation drops it at once — a window in which the stolen key
  still works is the thing being escaped."
  ([state verdict] (apply-set state verdict default-policy))
  ([state verdict policy]
   (if-not (admitted? verdict)
     state
     (let [policy (merge default-policy policy)
           one-way? (true? (:aiueos.anchors/one-way? verdict))
           now (:now-ms state)]
       (merge state
              {:current-anchors (:aiueos.anchors/anchors verdict)
               :installed-sequence (:aiueos.anchors/sequence verdict)
               :previous-anchors (if one-way? #{} (set (:current-anchors state)))
               :accept-previous-until-ms (when (and (not one-way?) now
                                                    (seq (:current-anchors state)))
                                           (+ now (:overlap-window-ms policy)))})))))
