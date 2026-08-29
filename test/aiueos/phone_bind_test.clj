(ns aiueos.phone-bind-test
  "Discriminating tests for ADR-2608221625 P1b / P1c.

  `clojure -M:test` of unrelated suites is not this gate. The Mac VM gate is
  `clojure -M:phone-bind smoke` (and `pre-enroll`). These tests name the
  reasons a bind is refused so a silent pass cannot stand in for a phone."
  (:require [aiueos.phone-bind :as pb]
            [clojure.test :refer [deftest is testing]]
            [grant.enroll :as enroll]))

(def factory
  {:did "did:aiueos:vm:aaa" :state :factory :token "T-phone"
   :attested? false :first-seen-ms 0 :model "aiueos-qemu-hosted"})

(deftest p1b-phone-http-is-the-only-green-path
  (testing "guest VGA/keyboard is named red, not an alternate success"
    (let [v (pb/apply-phone-claim
             factory
             {:path :guest-vga-keyboard
              :did (:did factory) :token "T-phone" :owner "acct:x"
              :now-ms 1 :possession-proof-valid? true})]
      (is (not (enroll/granted? v)))
      (is (= :local-console-required (:aiueos.enroll/reason v))
          "this gate is red if the operator used the guest keyboard")))
  (testing "phone HTTP with possession proof is grant.enroll's factory claim"
    (let [v (pb/apply-phone-claim
             factory
             {:path :phone-http
              :did (:did factory) :token "T-phone" :owner "acct:x"
              :now-ms 1 :possession-proof-valid? true})]
      (is (enroll/granted? v))
      (is (= :claimed (:aiueos.enroll/next-state v))))))

(deftest p1b-chassis-argv-has-no-monitor
  (let [argv (pb/chassis-argv-shape {:qemu "qemu-system-aarch64"
                                     :firmware "/fw.fd"
                                     :qmp "/tmp/q.qmp"
                                     :serial "/tmp/s.log"
                                     :accel "hvf"})]
    (is (pb/headless-argv? argv))
    (is (= "none" (second (drop-while #(not= % "-display") argv))))
    (is (not (some #{"cocoa" "gtk" "sdl" "vnc" "-nographic"} argv))
        "-nographic muxes guest serial onto the operator terminal; P1b prints the chassis QR on the host instead")
    (is (some #(= % "user,id=n0") argv)
        "user-mode/slirp stands in for Ethernet DHCP")))

(deftest p1b-session-document-is-one-spa
  (let [html (pb/session-html)]
    (is (re-find #"href=\"#setup\"" html))
    (is (re-find #"href=\"#session\"" html))
    (is (re-find #"href=\"#manage\"" html))
    (is (re-find #"dads-button" html)
        "P1 is red if this is still the proving-slice hand-rolled face")
    (is (re-find #"window.__aiueosSessionAlive" html)
        "crossing fragments must not load a second document")
    (is (re-find #"jp-go-dds" html))
    (is (re-find #"--hig-" html)
        "bridge --hig-* tokens onto DADS")
    (is (not (re-find #"liquid-glass" html)))))

(deftest p1c-copied-grant-cannot-bind-a-second-device
  (let [grant {:aiueos.enroll/kind :pre-grant
               :nonce "once-only"
               :tenant "acct:fleet"
               :allowed-clouds ["https://kotobase.net"]
               :uses-remaining 1}
        ledger {:consumed-grant-nonces {} :devices {}}
        a {:did "did:aiueos:vm:one" :state :factory :token "Ta"
           :model "aiueos-qemu-hosted" :first-seen-ms 0 :attested? false}
        b {:did "did:aiueos:vm:two" :state :factory :token "Tb"
           :model "aiueos-qemu-hosted" :first-seen-ms 0 :attested? false}
        first (pb/consume-pre-grant ledger grant a)
        second (pb/consume-pre-grant (:ledger first) grant b)]
    (is (enroll/granted? (:verdict first)))
    (is (= :pre-grant (:aiueos.enroll/via (:verdict first))))
    (is (false? (get-in first [:ledger :devices "did:aiueos:vm:one" :qr?]))
        "P1c appears in the device list with zero QR")
    (is (not (enroll/granted? (:verdict second))))
    (is (= :already-consumed (:aiueos.enroll/reason (:verdict second)))
        "a copied grant must not bind a second VM")))

(deftest p1b-http-phone-client-binds-without-guest-console
  (let [dir (java.io.File. (str (System/getProperty "java.io.tmpdir")
                                "/aiueos-pb-http-" (System/nanoTime)))
        rt (pb/start-http! (pb/make-runtime {:dir dir :listen-port 0}))
        printed (pb/print-chassis! rt)]
    (try
      (is (re-find #"^http://127\.0\.0\.1:\d+/#setup$" (:url printed)))
      (is (re-find #"^aiueos:2;" (:qr printed)))
      (is (not (re-find #"token=" (:qr printed)))
          "a scanned code must not expose the device enrollment token")
      (let [setup (:body (pb/http-get (str (pb/base-url rt) "/setup.json")))]
        (is (re-find #"\"claim_secret_exposed\":false" setup))
        (is (not (re-find #"\"token\"" setup))))
      (let [plan (pb/http-get (str (pb/base-url rt) "/api/device-auth/plan"))
            challenge (pb/http-post
                       (str (pb/base-url rt) "/api/device-auth/challenge")
                       {:method "phone-scan"})
            complete (pb/http-post
                      (str (pb/base-url rt) "/api/device-auth/complete")
                      {:verified true})]
        (is (= 200 (:code plan)))
        (is (re-find #"\"engine\":\"kotoba-lang/browser\"" (:body plan)))
        (is (= "external-authority-required"
               (get-in challenge [:parsed :verification])))
        (is (re-find #"^aiueos-auth:1;did=.*;secret=none$"
                     (get-in challenge [:parsed :scan_payload])))
        (is (= 501 (:code complete)))
        (is (= "authority-verifier-not-wired"
               (get-in complete [:parsed :reason])))
        "the hosted helper never trusts a client-supplied verified flag")
      (let [bind (pb/phone-bind-http (pb/base-url rt) "acct:test")]
        (is (= 200 (:code bind)))
        (is (= "grant" (get-in bind [:parsed (keyword "aiueos/decision")])))
        (is (.isFile (pb/receipt-file dir))))
      (finally
        (pb/stop-http! rt)))))

(deftest clojure-m-test-is-not-the-p1b-gate
  (is (nil? (System/getenv "AIUEOS_PHONE_BIND_COUNTED_UNRELATED_SUITE"))
      "do not count unrelated clojure -M:test as P1b; run clojure -M:phone-bind smoke"))
