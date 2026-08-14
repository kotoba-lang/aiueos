(ns aiueos.verify-value-handle-arena
  (:require [clojure.edn :as edn]
            [kotoba.compiler.core :as compiler]))

(defn- fail! [message data]
  (throw (ex-info message (assoc data :phase :aiueos-value-handle-arena))))

(defn- descriptor [word]
  [(bit-and word 0xffffffff) (quot word 4294967296)])

(defn- initialized-state [state]
  (merge {:initialized? false :lock 0 :next-handle 1 :entries {}} state))

(defn- step [state [operation handle descriptor-word]]
  (let [state (initialized-state state)]
    (if (not= 0 (:lock state))
      [state 0]
      (case operation
        0 [(assoc state :initialized? true) 1]
        1 (let [[value-token cid-token] (descriptor descriptor-word)
                next-handle (:next-handle state)
                entries (:entries state)]
            (if (and (:initialized? state) (pos? value-token) (pos? cid-token)
                     (<= next-handle 4096) (< (count entries) 63))
              [(-> state
                   (assoc-in [:entries next-handle]
                             {:value value-token :cid cid-token})
                   (update :next-handle inc))
               next-handle]
              [state 0]))
        2 [state (get-in state [:entries handle :value] 0)]
        3 [state (get-in state [:entries handle :cid] 0)]
        4 (if (contains? (:entries state) handle)
            [(update state :entries dissoc handle) 1]
            [state 0])
        [state 0]))))

(defn- verify-vectors! [vectors]
  (doseq [{:keys [name initial steps]} vectors]
    (reduce (fn [state {:keys [args expected]}]
              (let [[next-state actual] (step state args)]
                (when-not (= expected actual)
                  (fail! "semantic vector mismatch"
                         {:vector name :args args :expected expected :actual actual}))
                next-state))
            initial steps)))

(defn -main [& [source-path contract-path]]
  (when-not (and source-path contract-path)
    (fail! "usage: <value-handle-arena.kotoba> <value-handle-arena-v1.edn>" {}))
  (let [contract (edn/read-string (slurp contract-path))
        result (compiler/compile-source (slurp source-path)
                                        :x86_64-aiueos-kernel-v1)
        object (:object result)
        code (get-in result [:artifact :code])
        opcode (get-in contract [:native :required-opcode])]
    (when-not (= (get-in contract [:native :export]) (:export object))
      (fail! "native export mismatch" {:object (select-keys object [:export :abi])}))
    (when-not (= (get-in contract [:native :imports]) (:imports object))
      (fail! "native object unexpectedly imports foreign code" {:imports (:imports object)}))
    (when-not (some #{opcode} (partition (count opcode) 1 code))
      (fail! "native object does not contain LOCK CMPXCHG" {:opcode opcode}))
    (verify-vectors! (:vectors contract))
    (println (pr-str {:format :aiueos.value-handle-arena/verification-v1
                      :vectors (reduce + (map (comp count :steps) (:vectors contract)))
                      :export (:export object) :imports (:imports object)
                      :atomic-opcode opcode :status :passed}))))
