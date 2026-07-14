(ns aiueos.pid1-test
  "`power-off!`/`restart!`/`halt!` are NEVER called here (they'd invoke the
  real Linux `reboot(2)` syscall) -- `boot!` is tested with a fake
  `poweroff-fn`. `reap-one-zombie!`/`reap-all-zombies!` and
  `install-shutdown-handler!` ARE real (no fakes needed): the former is a
  read-only `waitpid(WNOHANG)` against this test JVM's own (empty) child
  list, and the latter only replaces this JVM's own SIGTERM/SIGINT
  disposition, which is safe to exercise (and IS exercised, via `boot!`'s own
  shutdown below, since `Signal.raise` is how the test triggers shutdown)."
  (:require [aiueos.pid1 :as pid1]
            [clojure.test :refer [deftest is testing]])
  #?(:clj (:import [java.lang.foreign Arena])))

(deftest pid1-argv0-detection
  (is (true? (pid1/pid1-argv0? "/init")))
  (is (true? (pid1/pid1-argv0? "init")))
  (is (false? (pid1/pid1-argv0? "/bin/bash")))
  (is (false? (pid1/pid1-argv0? nil))))

#?(:clj
   (deftest pid1-mode-requires-both-argv0-and-boot-file
     (let [tmp (java.io.File/createTempFile "aiueos-boot" ".edn")]
       (try
         (.delete tmp)
         (testing "argv0 is init but boot file absent -> false"
           (is (false? (pid1/pid1-mode? "/init" (.getPath tmp)))))
         (spit tmp "{:aiueos/system \"/etc/aiueos/system\"}")
         (testing "both conditions met -> true"
           (is (true? (pid1/pid1-mode? "/init" (.getPath tmp)))))
         (testing "argv0 is not init, even with boot file present -> false"
           (is (false? (pid1/pid1-mode? "/bin/bash" (.getPath tmp)))))
         (finally (.delete tmp))))))

#?(:clj
   (deftest load-boot-config-round-trips
     (let [tmp (java.io.File/createTempFile "aiueos-boot" ".edn")]
       (try
         (spit tmp (pr-str {:aiueos/system "/etc/aiueos/system" :aiueos/policy "/etc/aiueos/policy.edn"}))
         (is (= {:aiueos/system "/etc/aiueos/system" :aiueos/policy "/etc/aiueos/policy.edn"}
                (pid1/load-boot-config (.getPath tmp))))
         (finally (.delete tmp))))))

#?(:clj
   (deftest load-boot-config-requires-system
     (let [tmp (java.io.File/createTempFile "aiueos-boot" ".edn")]
       (try
         (spit tmp (pr-str {:aiueos/policy "/etc/aiueos/policy.edn"}))
         (is (thrown? Exception (pid1/load-boot-config (.getPath tmp))))
         (finally (.delete tmp))))))

#?(:clj
   (deftest reap-zombie-calls-are-safe-with-no-children
     (with-open [arena (Arena/ofConfined)]
       (is (nil? (pid1/reap-one-zombie! arena)))
       (is (= 0 (pid1/reap-all-zombies! arena))))))

#?(:clj
   (deftest boot-runs-up-fn-then-reaps-until-shutdown-then-powers-off
     (let [up-calls (atom [])
           poweroff-calls (atom 0)
           boot-config {:aiueos/system "/etc/aiueos/system" :aiueos/policy nil}]
       (with-open [arena (Arena/ofConfined)]
         (future
           (Thread/sleep 50)
           (sun.misc.Signal/raise (sun.misc.Signal. "TERM")))
         (pid1/boot! boot-config
                     (fn [system-path policy-path] (swap! up-calls conj [system-path policy-path]))
                     (fn [] (swap! poweroff-calls inc))
                     arena
                     10))
       (is (= [["/etc/aiueos/system" nil]] @up-calls))
       (is (= 1 @poweroff-calls)))))

#?(:clj
   (deftest boot-smoke-mode-powers-off-immediately-after-success
     (let [poweroff-calls (atom 0)]
       (with-open [arena (Arena/ofConfined)]
         (is (= :shutdown-after-boot
                (pid1/boot! {:aiueos/system "/system"
                             :aiueos/shutdown-after-boot? true}
                            (fn [_ _] {:aiueos.cli/ok? true})
                            (fn [] (swap! poweroff-calls inc)) arena 1))))
       (is (= 1 @poweroff-calls)))))

#?(:clj
   (deftest boot-refuses-failed-component-graph
     (with-open [arena (Arena/ofConfined)]
       (is (thrown? Exception
                    (pid1/boot! {:aiueos/system "/system"}
                                (fn [_ _] {:aiueos.cli/ok? false})
                                (fn [] nil) arena 1))))))
