(ns aiueos.anchors-test
  (:require [aiueos.anchors :as anchors]
            [aiueos.cloud :as cloud]
            [clojure.test :refer [deftest is testing]]))

(def pin-a (apply str (repeat 64 "a")))
(def pin-b (apply str (repeat 64 "b")))
(def pin-c (apply str (repeat 64 "c")))

(def root {:keys #{"k1" "k2" "k3"} :threshold 2})
(def sigs [{:key-id "k1" :verified? true :status-index 0}
           {:key-id "k2" :verified? true :status-index 1}])
(def all-sigs (conj sigs {:key-id "k3" :verified? true :status-index 2}))

(defn- state [& {:as over}]
  (merge {:installed-sequence 6 :now-ms 1000 :root root
          :revocation-bits [0 0 0 0] :current-anchors #{pin-a}}
         over))

(defn- proposed [& {:as over}]
  (merge {:set-id "anchors-7" :sequence 7 :anchors #{pin-a pin-b}
          :signatures sigs :timestamp-ms 1000 :digest-matches? true}
         over))

;; ── an anchor set is a release, and is admitted like one ──────────────────

(deftest an-overlapping-set-at-a-higher-sequence-is-admitted
  (let [v (anchors/admit-set (proposed) (state))]
    (is (anchors/admitted? v))
    (is (= #{pin-a pin-b} (:aiueos.anchors/anchors v)))
    (is (= [pin-a] (:aiueos.anchors/overlap v)))
    (is (false? (:aiueos.anchors/one-way? v)))))

(deftest publisher-reasons-pass-through-unchanged
  (testing "one signature is below the root's threshold"
    (let [v (anchors/admit-set (proposed :signatures [(first sigs)]) (state))]
      (is (= :below-threshold (:aiueos.publisher/reason v)))
      (is (nil? (:aiueos.anchors/reason v)) "not relabelled")))
  (testing "a set at or below the installed sequence is a rollback"
    (is (= :sequence-not-monotonic
           (:aiueos.publisher/reason (anchors/admit-set (proposed :sequence 6) (state))))))
  (testing "a revoked signer does not count"
    (is (= :key-revoked
           (:aiueos.publisher/reason
            (anchors/admit-set (proposed) (state :revocation-bits [1 1 0 0]))))))
  (testing "freeze"
    (is (= :timestamp-expired
           (:aiueos.publisher/reason
            (anchors/admit-set (proposed) (state :now-ms (+ 1000 (* 1000 60 60 48)))))))))

;; ── what is true of anchor sets and of nothing else ───────────────────────

(deftest a-perfectly-signed-empty-set-is-refused
  (let [v (anchors/admit-set (proposed :anchors #{}) (state))]
    (is (= :anchor-set-empty (:aiueos.anchors/reason v))
        "an empty pin set is not permissive, it is a brick")))

(deftest a-set-with-no-identity-is-refused
  (is (= :set-id-missing (:aiueos.anchors/reason
                          (anchors/admit-set (proposed :set-id nil) (state))))))

(deftest replacing-every-anchor-at-once-needs-break-glass
  (let [v (anchors/admit-set (proposed :anchors #{pin-b pin-c}) (state))]
    (is (= :disjoint-without-break-glass (:aiueos.anchors/reason v)))
    (is (= [pin-a] (:aiueos.anchors/current v)))
    (is (= [pin-b pin-c] (:aiueos.anchors/proposed v))
        "the verdict shows both sets, because the operator has to see the gap")))

(deftest break-glass-needs-every-root-key-not-the-threshold
  (let [refused (anchors/admit-set (proposed :anchors #{pin-b pin-c} :break-glass? true)
                                   (state))]
    (is (= :break-glass-below-threshold (:aiueos.anchors/reason refused)))
    (is (= 3 (:aiueos.anchors/required refused)))
    (is (= 2 (:aiueos.anchors/live refused))))
  (let [admitted (anchors/admit-set (proposed :anchors #{pin-b pin-c} :break-glass? true
                                              :signatures all-sigs)
                                    (state))]
    (is (anchors/admitted? admitted))
    (is (true? (:aiueos.anchors/one-way? admitted))
        "the verdict says what this is, so nobody does it by accident")))

;; ── trust on first use is not available ───────────────────────────────────

(deftest a-device-with-no-anchors-refuses-a-set-off-the-network
  (let [v (anchors/admit-set (proposed) (state :current-anchors #{}))]
    (is (= :no-current-set (:aiueos.anchors/reason v))
        "it has no way to judge the connection that delivered it")))

(deftest the-image-carries-the-first-set
  (let [v (anchors/admit-set (proposed :bootstrap? true) (state :current-anchors #{}))]
    (is (anchors/admitted? v))
    (is (true? (:aiueos.anchors/bootstrap? v)))
    (is (false? (:aiueos.anchors/one-way? v))
        "there was nothing to strand")))

;; ── rotation is not a flag day ────────────────────────────────────────────

(deftest a-rotation-that-overlaps-can-retire-the-old-key
  (testing "ship the new key alongside the old one, then retire the old one"
    (let [s0 (state)
          add (anchors/admit-set (proposed :anchors #{pin-a pin-b}) s0)
          s1 (anchors/apply-set s0 add)
          retire (anchors/admit-set (proposed :set-id "anchors-8" :sequence 8
                                              :anchors #{pin-b})
                                    s1)
          s2 (anchors/apply-set s1 retire)]
      (is (anchors/admitted? add))
      (is (anchors/admitted? retire)
          "retiring pin-a overlaps with the set that introduced pin-b")
      (is (= #{pin-b} (:current-anchors s2)))
      (is (= #{pin-a pin-b} (:previous-anchors s2)))
      (is (= #{pin-a pin-b} (anchors/usable-anchors s2)) "inside the window, both")
      (is (= #{pin-b} (anchors/usable-anchors
                       (assoc s2 :now-ms (inc (:accept-previous-until-ms s2)))))
          "after it, only the current set — a previous set that never expires was never retired")
      (is (false? (:aiueos.anchors/one-way? retire))
          "no step of this can strand a device"))))

(deftest retiring-the-only-key-in-one-step-is-not-a-rotation
  (is (= :disjoint-without-break-glass
         (:aiueos.anchors/reason (anchors/admit-set (proposed :anchors #{pin-b}) (state))))
      "going straight from {a} to {b} is the one-way door, whatever it is called"))

(deftest break-glass-drops-the-previous-set-at-once
  (let [s0 (state)
        v (anchors/admit-set (proposed :anchors #{pin-b pin-c} :break-glass? true
                                       :signatures all-sigs)
                             s0)
        s1 (anchors/apply-set s0 v)]
    (is (= #{} (:previous-anchors s1)))
    (is (nil? (:accept-previous-until-ms s1)))
    (is (= #{pin-b pin-c} (anchors/usable-anchors s1))
        "a window in which the stolen key still works is the thing being escaped")))

(deftest a-refused-set-changes-nothing
  (let [s0 (state)
        v (anchors/admit-set (proposed :anchors #{}) s0)]
    (is (= s0 (anchors/apply-set s0 v)))))

;; ── a machine that cannot fetch keeps what it has ─────────────────────────

(deftest an-unreachable-publisher-does-not-empty-the-pin-set
  (is (true? (anchors/keep-using? (state))))
  (is (true? (anchors/keep-using? (state :now-ms (+ 1000 (* 1000 60 60 24 30))))))
  (is (false? (anchors/keep-using? (state :current-anchors #{})))))

;; ── the set this produces is the set aiueos.cloud consumes ────────────────

(deftest what-anchors-decides-is-what-cloud-checks-against
  (let [s0 (state)
        s1 (anchors/apply-set s0 (anchors/admit-set (proposed :anchors #{pin-a pin-b}) s0))
        s2 (anchors/apply-set s1 (anchors/admit-set (proposed :set-id "anchors-8" :sequence 8
                                                              :anchors #{pin-b})
                                                    s1))
        policy {:aiueos.cloud/trust-anchors (anchors/usable-anchors s2)}]
    (is (cloud/allowed? (cloud/admit-peer policy {:spki-sha256 pin-b})) "the new key")
    (is (cloud/allowed? (cloud/admit-peer policy {:spki-sha256 pin-a})) "the old one, for now")
    (is (= :peer-not-pinned
           (:aiueos.cloud/reason (cloud/admit-peer policy {:spki-sha256 pin-c}))))
    (let [after {:aiueos.cloud/trust-anchors
                 (anchors/usable-anchors
                  (assoc s2 :now-ms (inc (:accept-previous-until-ms s2))))}]
      (is (= :peer-not-pinned (:aiueos.cloud/reason (cloud/admit-peer after {:spki-sha256 pin-a})))
          "once the window closes the retired key is refused by the same function"))))

(deftest every-reason-this-namespace-produces-is-declared
  (doseq [r [:set-id-missing :anchor-set-empty :no-current-set
             :disjoint-without-break-glass :break-glass-below-threshold]]
    (is (contains? anchors/deny-reasons r) (str r " is produced but not declared"))))
