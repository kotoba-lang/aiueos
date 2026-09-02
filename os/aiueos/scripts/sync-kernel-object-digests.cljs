#!/usr/bin/env nbb
;; `build-uefi.sh` pins a sha256 for every Kotoba kernel object it links, and
;; `os/aiueos/kotoba/provenance.edn` records one too. Two copies of a pin are a
;; pin that can disagree with itself.
;;
;;   nbb os/aiueos/scripts/sync-kernel-object-digests.cljs --check
;;       exit 0 they agree, 3 they do not (and it names every one), 2 it could
;;       not tell (a digest slot it cannot resolve to an object).
;;
;;   nbb os/aiueos/scripts/sync-kernel-object-digests.cljs --write
;;       rewrite the shell's literals from the manifest.
;;
;; The manifest is the source: its digests are written by
;; `reproduce-kotoba-objects.cljs --attest`, which produced the bytes it is
;; describing. The shell's copies exist because `verify-kotoba-kernel-object.py`
;; takes the expected digest as an argument and neither it nor `sh` reads EDN.
;;
;; WHY A GATE AND NOT JUST A FIXER. The disagreement this catches is not
;; hypothetical: rebuilding the 74 objects with one current amu changed 69 of
;; them, and every one of those 69 made `build-uefi.sh` fail closed with
;; "fixture digest does not match the pinned compiler output" -- which is the
;; check working. What it cannot do is tell you WHICH copy is stale, and a
;; build that fails on the first of 70 tells you about one.
(ns sync-kernel-object-digests
  (:require [clojure.edn :as edn]
            [clojure.string :as str]
            ["node:fs" :as fs]
            ["node:path" :as path]))

(def ^:private args (vec *command-line-args*))
(defn- flag? [f] (some #(= f %) args))
(defn- opt [f] (let [i (.indexOf args f)] (when-not (neg? i) (nth args (inc i) nil))))

(def ^:private root (path/resolve (or (opt "--root") (.cwd js/process))))
(def ^:private script (path/join root "os" "aiueos" "scripts" "build-uefi.sh"))
(def ^:private manifest (path/join root "os" "aiueos" "kotoba" "provenance.edn"))

(defn- die! [code msg] (.error js/console msg) (.exit js/process code))

(defn- object-vars
  "shell variable -> object basename, from the `<var>=${ENV:-\"$aiueos/kotoba/
  <name>.o\"}` defaults at the top of the script."
  [text]
  (into {} (for [[_ v n] (re-seq #"(?m)^(kotoba_[a-z0-9_]+)=\$\{AIUEOS_[A-Z0-9_]+:-\"\$aiueos/kotoba/([a-z0-9-]+\.o)\"\}"
                                 text)]
             [v n])))

(defn- invocations
  "Every `verify-kotoba-kernel-object.py \"$obj\" <slot>` call, as
  {:object <basename> :slot <literal-hex or shell-var name> :literal? bool}."
  [text vars]
  (for [[whole objvar slot]
        (re-seq #"verify-kotoba-kernel-object\.py\"?\s+\"\$([a-z0-9_]+)\"\s*(?:\\\s*\n\s*)?(\"?\$?[A-Za-z0-9_]+\"?)"
                text)
        :let [slot (-> slot (str/replace "\"" "") (str/replace "$" ""))
              literal? (re-matches #"[0-9a-f]{64}" slot)]]
    {:object (get vars objvar) :objvar objvar :slot slot
     :literal? (boolean literal?) :whole whole}))

(defn -main []
  (when-not (.existsSync fs script) (die! 2 (str "COULD-NOT-RUN reason=script-absent path=" script)))
  (when-not (.existsSync fs manifest) (die! 2 (str "COULD-NOT-RUN reason=manifest-absent path=" manifest)))
  (let [text (.readFileSync fs script "utf8")
        want (into {} (for [[o r] (:objects (edn/read-string (.readFileSync fs manifest "utf8")))]
                        [o (:object-sha256 r)]))
        vars (object-vars text)
        calls (vec (invocations text vars))
        unresolved (filterv #(nil? (:object %)) calls)]
    (when (empty? calls)
      (die! 2 "COULD-NOT-RUN reason=no-verify-invocations (the call shape changed)"))
    (when (seq unresolved)
      (die! 2 (str "COULD-NOT-RUN reason=object-variable-unresolved n=" (count unresolved)
                   " first=" (:objvar (first unresolved)))))
    ;; A slot that is a shell variable is resolved to its assignment, so the
    ;; three conditionally-pinned objects (kernel-probe, journal-plan, fnv1a)
    ;; are compared and rewritten like the rest instead of being skipped.
    (let [slots (for [c calls
                      :let [current (if (:literal? c)
                                      (:slot c)
                                      (second (re-find (re-pattern (str "(?m)^\\s*" (:slot c)
                                                                        "=([0-9a-f]{64})\\s*$"))
                                                       text)))]]
                  (assoc c :current current :expected (get want (:object c))))
          unknown (filterv #(nil? (:expected %)) slots)
          unreadable (filterv #(nil? (:current %)) slots)
          stale (filterv #(not= (:current %) (:expected %)) slots)]
      (when (seq unknown)
        (die! 2 (str "COULD-NOT-RUN reason=object-not-in-manifest n=" (count unknown)
                     " first=" (:object (first unknown)))))
      (when (seq unreadable)
        (die! 2 (str "COULD-NOT-RUN reason=digest-slot-unreadable n=" (count unreadable)
                     " first=" (:objvar (first unreadable)))))
      (doseq [s stale]
        (println (str "STALE\t" (:object s) "\tshell=" (subs (:current s) 0 12)
                      "\tmanifest=" (subs (:expected s) 0 12))))
      (println (str "SCANNED\t" (count slots)))
      (cond
        (flag? "--write")
        (let [out (reduce (fn [t s]
                            (if (= (:current s) (:expected s))
                              t
                              (str/replace t (:current s) (:expected s))))
                          text stale)]
          (.writeFileSync fs script out "utf8")
          (println (str "SYNCED\tobjects=" (count slots) " rewritten=" (count stale)))
          (.exit js/process 0))

        (seq stale)
        (do (println (str "AIUEOS_KERNEL_OBJECT_DIGESTS_STALE scanned=" (count slots)
                          " stale=" (count stale)))
            (.exit js/process 3))

        :else
        (do (println (str "AIUEOS_KERNEL_OBJECT_DIGESTS_OK scanned=" (count slots) " stale=0"))
            (.exit js/process 0))))))

(-main)
