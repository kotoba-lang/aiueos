(ns aiueos.compositor-guest-test
  "nbb-loadable classifiers for KERNEL.ELF guest compositor gates (ADR-0100).

  `clojure -M:test` of compositor_test.clj still covers hosted HTML/QEMU
  argv. This ns is the second runtime: the gate command must not need a
  JVM, hosted WM serial must stay red, and unanswered QEMU is exit 3."
  (:require [aiueos.compositor.guest :as guest]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]))

(deftest gate-cmd-is-nbb-not-clojure
  (doseq [p guest/guest-profiles]
    (let [cmd (guest/gate-cmd p)]
      (is (str/starts-with? cmd "nbb --classpath src scripts/compositor-guest.cljs "))
      (is (str/ends-with? cmd p))
      (is (not (str/includes? cmd "clojure -M"))
          "guest evidence must not be a JVM alias"))))

(deftest guest-session-serial-is-green-without-jvm-wm
  (testing "KERNEL.ELF packed front 2 restore is green"
    (is (guest/guest-session-ok?
         "AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2\n"))
    (is (:green? (guest/guest-session-result
                  {:serial "AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2\n"}))))
  (testing "hosted JVM WM serial does not count"
    (let [r (guest/guest-session-result {:serial "AIUEOS_COMPOSITOR_WM_OK\n"})]
      (is (not (:green? r)))
      (is (= 1 (:exit r)))
      (is (= :hosted-wm-does-not-count (:reason r))))
    (let [r (guest/guest-session-result {:hosted-wm? true :serial ""})]
      (is (= :hosted-wm-does-not-count (:reason r)))))
  (testing "unmeasured is exit 3, not a silent pass"
    (let [r (guest/guest-session-result {:qemu-unmeasured? true})]
      (is (= 3 (:exit r)))
      (is (= :unmeasured (:reason r)))))
  (testing "unknown profile is unmeasured, not a pass"
    (let [r (guest/classify "not-a-profile" {:serial "AIUEOS_GUEST_SESSION_OK restored-front=2 packed=2 kotoba-front=2 hit=2\n"})]
      (is (not (:green? r)))
      (is (= 3 (:exit r))))))

(deftest nbb-html-command-is-not-clojure-substring
  (is (not (str/includes? (guest/gate-cmd "guest-ime") "clojure -M:compositor guest-ime")))
  (is (str/includes? (guest/gate-cmd "guest-session") "guest-session")))
