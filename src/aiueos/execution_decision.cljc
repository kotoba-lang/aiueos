(ns aiueos.execution-decision
  "Bridge an agent-submitted portable plan to aiueos's deny-by-default
  admission decision.

  This is deliberately data-only: storage owns CID resolution and basis
  selection, while this namespace turns an already selected immutable plan,
  policy CID, and database basis into the one closed decision descriptor that
  a runtime may use to issue host-managed capability resources."
  (:require [aiueos.broker :as broker]
            [kotoba.abi.contract :as abi]))

(def decision-input-keys
  #{:plan :decision-cid :policy-cid :db-basis :issued-at :expires-at
    :manifest :graph :policy})

(defn- reject [reason]
  (throw (ex-info "portable policy decision rejected"
                  {:phase :portable-policy-decision :reason reason})))

(defn- reasons [broker-decision]
  (if (= :grant (:aiueos/decision broker-decision))
    [:aiueos/admission-granted]
    (mapv (fn [{:aiueos/keys [kind message]}]
            {:kind kind :message message})
          (:aiueos/violations broker-decision))))

(defn decide-plan!
  "Evaluate PLAN with aiueos's agent-admission path and return a closed,
  basis-bound `kotoba.policy-decision/v1` descriptor.

  The caller supplies identities resolved by its immutable fact store.  It
  cannot select `:result` or `:reasons`: those are derived from
  `broker/verify-admission`, which floors submitted trust before evaluation.
  A new capability request therefore requires a new plan and decision rather
  than a runtime permission prompt."
  [{:keys [plan decision-cid policy-cid db-basis issued-at expires-at
           manifest graph policy] :as input}]
  (when-not (and (map? input) (= decision-input-keys (set (keys input))))
    (reject :invalid-input))
  (when-not (abi/valid-plan? plan)
    (reject :invalid-plan))
  (let [broker-decision (broker/verify-admission manifest graph policy)
        decision {:format :kotoba.policy-decision/v1
                  :decision-cid decision-cid
                  :plan-cid (:plan-cid plan)
                  :policy-cid policy-cid
                  :db-basis db-basis
                  :result (if (= :grant (:aiueos/decision broker-decision))
                            :permit :deny)
                  :reasons (reasons broker-decision)
                  :issued-at issued-at
                  :expires-at expires-at}]
    (when-not (abi/valid-policy-decision? decision)
      (reject :invalid-decision))
    decision))
