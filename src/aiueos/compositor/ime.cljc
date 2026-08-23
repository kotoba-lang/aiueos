(ns aiueos.compositor.ime
  "Hosted IME for the compositor WM (root ADR-2608221625 Desktop leftover).

  There is no west IME repo (concept-lookup/repo-search 2026-08-23).
  This is the compositor's input method, not a second WM and not mozc.

  Keys are consumed into a romaji buffer and emitted as hiragana only
  on commit. Delivering latin `ka` to the focused guest while IME is
  on is the named red (`:ime-bypass`). IME-off is that red path on
  purpose so the gate can discriminate."
  (:require [clojure.string :as str]))

(def mora
  "Longest-match romaji → hiragana. Enough to prove conversion; not a
  dictionary. Kanji conversion is leftover `:kanji-absent`."
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

(defn boot
  "IME on, empty buffers. `focused` is the window that receives commits."
  ([] (boot {}))
  ([opts]
   {:on? (not (false? (:on? opts)))
    :buf ""
    :preedit ""
    :committed ""
    :guest-log ""
    :focused (or (:focused opts) nil)
    :leftover :kanji-absent
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
      (let [ime' (assoc ime :buf "" :preedit "")]
        [ime' {:consumed? true
               :reason :cancel
               :guest-text ""
               :preedit ""
               :committed (:committed ime)
               :focused focused}])

      (= k "Backspace")
      (let [buf (:buf ime)
            pre (:preedit ime)
            ime' (cond
                   (seq buf) (assoc ime :buf (subs buf 0 (dec (count buf))))
                   (seq pre) (assoc ime :preedit (subs pre 0 (dec (count pre))))
                   :else ime)]
        [ime' {:consumed? true
               :reason :backspace
               :guest-text ""
               :preedit (:preedit ime')
               :committed (:committed ime)
               :focused focused}])

      (or (= k "Enter") (= k "Space"))
      (let [raw (if (= (:buf ime) "n") "nn" (:buf ime))
            conv (convert-buf raw)
            leftover (:buf conv)
            text (str (:preedit ime) (:preedit conv) leftover)
            ime' (assoc ime
                        :buf ""
                        :preedit ""
                        :committed (str (:committed ime) text)
                        :guest-log (str (:guest-log ime) text))]
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
   :buf (or (:buf ime) "")
   :preedit (or (:preedit ime) "")
   :committed (or (:committed ime) "")
   :guest-log (or (:guest-log ime) "")
   :focused (or (:focused ime) 0)
   :leftover (name (or (:leftover ime) :kanji-absent))
   :engine (or (:engine ime) "aiueos.compositor.ime")})
