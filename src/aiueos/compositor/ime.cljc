(ns aiueos.compositor.ime
  "Hosted IME for the compositor WM (root ADR-2608221625 Desktop leftover).

  There is no west IME repo (concept-lookup/repo-search 2026-08-23).
  This is the compositor's input method, not a second WM and not mozc.

  Keys are consumed into a romaji buffer and emitted as hiragana only
  on commit. Space converts a reading that the tiny dictionary knows
  (ADR-0088: か→加). Delivering latin `ka` to the focused guest while
  IME is on is the named red (`:ime-bypass`). IME-off is that red path
  on purpose so the kana gate can discriminate. Space that commits kana
  while the dictionary has か is leftover `:kanji-absent`."
  (:require [clojure.string :as str]))

(def mora
  "Longest-match romaji → hiragana. Enough to prove conversion; not mozc."
  {"a" "あ" "i" "い" "u" "う" "e" "え" "o" "お"
   "ka" "か" "ki" "き" "ku" "く" "ke" "け" "ko" "こ"
   "sa" "さ" "si" "し" "su" "す" "se" "せ" "so" "そ"
   "shi" "し"
   "ta" "た" "ti" "ち" "tu" "つ" "te" "て" "to" "と"
   "chi" "ち" "tsu" "つ"
   "na" "な" "ni" "に" "nu" "ぬ" "ne" "ね" "no" "の"
   "ha" "は" "hi" "ひ" "hu" "ふ" "he" "へ" "ho" "ほ"
   "fu" "ふ"
   "ma" "ま" "mi" "み" "mu" "む" "me" "め" "mo" "も"
   "ya" "や" "yu" "ゆ" "yo" "よ"
   "ra" "ら" "ri" "り" "ru" "る" "re" "れ" "ro" "ろ"
   "wa" "わ" "wo" "を" "nn" "ん" "n'" "ん"
   "ga" "が" "gi" "ぎ" "gu" "ぐ" "ge" "げ" "go" "ご"
   "za" "ざ" "zi" "じ" "zu" "ず" "ze" "ぜ" "zo" "ぞ"
   "ji" "じ"
   "da" "だ" "di" "ぢ" "du" "づ" "de" "で" "do" "ど"
   "ba" "ば" "bi" "び" "bu" "ぶ" "be" "べ" "bo" "ぼ"
   "pa" "ぱ" "pi" "ぴ" "pu" "ぷ" "pe" "ぺ" "po" "ぽ"
   "kya" "きゃ" "kyu" "きゅ" "kyo" "きょ"
   "sha" "しゃ" "shu" "しゅ" "sho" "しょ"
   "cha" "ちゃ" "chu" "ちゅ" "cho" "ちょ"
   "nya" "にゃ" "nyu" "にゅ" "nyo" "にょ"
   "hya" "ひゃ" "hyu" "ひゅ" "hyo" "ひょ"
   "mya" "みゃ" "myu" "みゅ" "myo" "みょ"
   "rya" "りゃ" "ryu" "りゅ" "ryo" "りょ"
   "gya" "ぎゃ" "gyu" "ぎゅ" "gyo" "ぎょ"
   "ja" "じゃ" "ju" "じゅ" "jo" "じょ"
   "bya" "びゃ" "byu" "びゅ" "byo" "びょ"
   "pya" "ぴゃ" "pyu" "ぴゅ" "pyo" "ぴょ"
   "-" "ー" "," "、" "." "。"})

(def readings
  "Tiny hosted dictionary. Not mozc and not a west repo. か must convert
  so `:kanji-absent` can go red. Unknown readings stay kana."
  {"か" ["加" "可" "課"]
   "ひ" ["日" "火"]
   "あ" ["亜"]})

(defn- engine-leftover
  [ime]
  (if (false? (:kanji? ime))
    :kanji-absent
    :native-compositor-absent))

(defn boot
  "IME on, empty buffers. `focused` is the window that receives commits.
  `:kanji? false` is the named red for ADR-0088 (Space commits kana)."
  ([] (boot {}))
  ([opts]
   {:on? (not (false? (:on? opts)))
    :kanji? (not (false? (:kanji? opts)))
    :buf ""
    :preedit ""
    :committed ""
    :guest-log ""
    :focused (or (:focused opts) nil)
    :converting? false
    :candidates []
    :cand-idx 0
    :reading ""
    :leftover (if (false? (:kanji? opts)) :kanji-absent :native-compositor-absent)
    :engine "aiueos.compositor.ime"}))

(defn- sokuon?
  [s]
  (boolean
   (and (>= (count s) 2)
        (let [a (subs s 0 1)
              b (subs s 1 2)]
          (and (= a b)
               (contains? #{"k" "s" "t" "p" "c" "g" "z" "d" "b" "j"} a)
               (not= a "n"))))))

(defn convert-buf
  "Greedy longest mora from the start. Remainder stays in `:buf`."
  [buf]
  (loop [s (or buf "") out ""]
    (cond
      (str/blank? s) {:preedit out :buf ""}
      (sokuon? s) (recur (subs s 1) (str out "っ"))
      :else
      (let [n (min 3 (count s))
            hit (some (fn [len]
                        (when-let [ch (get mora (subs s 0 len))]
                          [len ch]))
                      (range n 0 -1))]
        (if hit
          (recur (subs s (first hit)) (str out (second hit)))
          {:preedit out :buf s})))))

(defn- letter?
  [k]
  (boolean (re-matches #"[a-zA-Z]" (str k))))

(defn- normalize-key
  [k]
  (let [s (str (or k ""))]
    (cond
      (= s "Enter") "Enter"
      (= s "Escape") "Escape"
      (= s "Backspace") "Backspace"
      (= s " ") "Space"
      (= s "Space") "Space"
      :else (str/lower-case s))))

(defn- flush-preedit
  "Fold remaining romaji into `:preedit` (n→ん)."
  [ime]
  (let [raw (if (= (:buf ime) "n") "nn" (:buf ime))
        conv (convert-buf raw)
        leftover-buf (:buf conv)
        pre (str (:preedit ime) (:preedit conv) leftover-buf)]
    (assoc ime :buf "" :preedit pre)))

(defn- commit-text
  [ime text]
  (assoc ime
         :buf ""
         :preedit ""
         :converting? false
         :candidates []
         :cand-idx 0
         :reading ""
         :committed (str (:committed ime) text)
         :guest-log (str (:guest-log ime) text)
         :leftover (engine-leftover ime)))

(defn- cancel-conversion
  [ime]
  (if (:converting? ime)
    (assoc ime
           :converting? false
           :candidates []
           :cand-idx 0
           :preedit (or (:reading ime) "")
           :reading "")
    (assoc ime :buf "" :preedit "")))

(defn handle-key
  "Returns `[ime' event]`. Event always names `:consumed?` and whether
  latin leaked to the guest (`:guest-text`). On-path must not leak."
  [ime key]
  (let [k (normalize-key key)
        focused (:focused ime)]
    (cond
      (not (:on? ime))
      (let [ch (if (#{"Enter" "Escape" "Backspace" "Space"} k)
                 ""
                 k)
            ime' (update ime :guest-log str ch)]
        [ime' {:consumed? false
               :reason :ime-bypass
               :guest-text ch
               :preedit ""
               :committed (:committed ime)
               :focused focused}])

      (= k "Escape")
      (let [ime' (cancel-conversion ime)]
        [ime' {:consumed? true
               :reason :cancel
               :guest-text ""
               :preedit (:preedit ime')
               :committed (:committed ime)
               :focused focused}])

      (= k "Backspace")
      (let [ime' (cond
                   (:converting? ime) (cancel-conversion ime)
                   (seq (:buf ime)) (assoc ime :buf (subs (:buf ime) 0 (dec (count (:buf ime)))))
                   (seq (:preedit ime)) (assoc ime :preedit (subs (:preedit ime) 0 (dec (count (:preedit ime)))))
                   :else ime)]
        [ime' {:consumed? true
               :reason :backspace
               :guest-text ""
               :preedit (:preedit ime')
               :committed (:committed ime)
               :focused focused}])

      (= k "Space")
      (let [ime1 (if (:converting? ime) ime (flush-preedit ime))
            reading (if (:converting? ime1)
                      (:reading ime1)
                      (:preedit ime1))
            cands (when (:kanji? ime1) (get readings reading))]
        (cond
          (and (seq cands) (:converting? ime1))
          (let [idx (mod (inc (or (:cand-idx ime1) 0)) (count cands))
                ch (nth cands idx)
                ime' (assoc ime1 :cand-idx idx :preedit ch)]
            [ime' {:consumed? true
                   :reason :cycle
                   :guest-text ""
                   :preedit ch
                   :committed (:committed ime)
                   :focused focused}])

          (seq cands)
          (let [ch (first cands)
                ime' (assoc ime1
                            :converting? true
                            :reading reading
                            :candidates (vec cands)
                            :cand-idx 0
                            :preedit ch
                            :leftover :native-compositor-absent)]
            [ime' {:consumed? true
                   :reason :convert
                   :guest-text ""
                   :preedit ch
                   :committed (:committed ime)
                   :focused focused}])

          :else
          (let [text (:preedit ime1)
                ime' (commit-text (assoc ime1 :leftover :kanji-absent) text)]
            [ime' {:consumed? true
                   :reason :kanji-absent
                   :guest-text text
                   :preedit ""
                   :committed (:committed ime')
                   :focused focused}])))

      (= k "Enter")
      (let [ime1 (if (:converting? ime) ime (flush-preedit ime))
            text (:preedit ime1)
            ime' (commit-text ime1 text)]
        [ime' {:consumed? true
               :reason :commit
               :guest-text text
               :preedit ""
               :committed (:committed ime')
               :focused focused}])

      (letter? k)
      (let [buf (str (:buf ime) k)
            conv (convert-buf buf)
            ime' (assoc ime :buf (:buf conv)
                        :preedit (str (:preedit ime) (:preedit conv)))]
        [ime' {:consumed? true
               :reason :compose
               :guest-text ""
               :preedit (:preedit ime')
               :committed (:committed ime)
               :focused focused}])

      :else
      [ime {:consumed? true
            :reason :ignored
            :guest-text ""
            :preedit (:preedit ime)
            :committed (:committed ime)
            :focused focused}])))

(defn feed
  "Apply a sequence of keys. Returns the final ime."
  [ime keys]
  (reduce (fn [s k]
            (first (handle-key s k)))
          ime
          keys))

(defn latin-leaked?
  "True when an on-IME key event delivered latin to the guest.
  That is the named red for the gate."
  [event]
  (boolean
   (and (= :compose (:reason event))
        (seq (:guest-text event))
        (re-find #"[a-zA-Z]" (str (:guest-text event))))))

(defn public-snapshot
  [ime]
  {:ime? (boolean (:on? ime))
   :on? (boolean (:on? ime))
   :kanji? (not (false? (:kanji? ime)))
   :buf (or (:buf ime) "")
   :preedit (or (:preedit ime) "")
   :committed (or (:committed ime) "")
   :guest-log (or (:guest-log ime) "")
   :focused (or (:focused ime) 0)
   :converting? (boolean (:converting? ime))
   :candidates (str/join "," (or (:candidates ime) []))
   :cand-idx (or (:cand-idx ime) 0)
   :leftover (name (or (:leftover ime) (engine-leftover ime)))
   :engine (or (:engine ime) "aiueos.compositor.ime")})
