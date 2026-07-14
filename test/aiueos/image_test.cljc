(ns aiueos.image-test
  "`build-initramfs!` IS exercised for real here (actual `cpio`/`gzip`
  subprocess, actual staged files) -- both are ordinary system tools
  present on this dev machine, and this is the part of the retired Rust
  `InitramfsPlan` that's pure staging/packaging, safe to run in CI."
  (:require [aiueos.image :as image]
            [clojure.test :refer [deftest is testing]]
            #?(:clj [clojure.java.shell :as shell]))
  #?(:clj (:import [java.io File])))

#?(:clj
   (defn- temp-dir! []
     (let [f (File/createTempFile "aiueos-image-test" "")]
       (.delete f)
       (.mkdirs f)
       f)))

#?(:clj
   (defn- delete-tree! [^File f]
     (when (.isDirectory f) (doseq [c (.listFiles f)] (delete-tree! c)))
     (.delete f)))

(deftest plan-requires-system
  (is (thrown? #?(:clj Exception :cljs js/Error) (image/plan {}))))

#?(:clj
   (deftest plan-defaults-guest-paths-and-out
     (let [dir (temp-dir!)]
       (try
         (spit (File. dir "system.edn") "{:aiueos/components []}")
         (let [p (image/plan {:system (.getPath (File. dir "system.edn"))})]
           (is (= "/etc/aiueos/system/system.edn" (:guest-system p)))
           (is (nil? (:guest-policy p)))
           (is (.endsWith (.getPath (:out p)) "system.initramfs.cpio.gz")))
         (finally (delete-tree! dir))))))

#?(:clj
   (deftest plan-with-policy-sets-guest-policy
     (let [dir (temp-dir!)]
       (try
         (spit (File. dir "system.edn") "{:aiueos/components []}")
         (spit (File. dir "policy.edn") "{}")
         (let [p (image/plan {:system (.getPath (File. dir "system.edn"))
                               :policy (.getPath (File. dir "policy.edn"))})]
           (is (= "/etc/aiueos/policy.edn" (:guest-policy p))))
         (finally (delete-tree! dir))))))

#?(:clj
   (deftest build-initramfs-produces-a-real-gzip-file
     (let [dir (temp-dir!)]
       (try
         (spit (File. dir "system.edn") "{:aiueos/components []}")
         (let [out (File. dir "out.initramfs.cpio.gz")
               p (image/plan {:system (.getPath (File. dir "system.edn"))
                               :out (.getPath out)})
               result (image/build-initramfs! p)]
           (testing "the gzip file exists and is non-trivially sized"
             (is (.exists out))
             (is (pos? (.length out)))
             (is (= (.length out) (:out-bytes result))))
           (testing "gzip magic bytes (1f 8b)"
             (with-open [in (java.io.FileInputStream. out)]
               (let [b (byte-array 2)]
                 (.read in b)
                 (is (= [0x1f -0x75] [(aget b 0) (aget b 1)])))))
           (testing "the scratch stage dir was cleaned up"
             (is (empty? (filter #(re-find #"^\.stage-" (.getName %)) (.listFiles dir))))))
         (finally (delete-tree! dir))))))

#?(:clj
   (deftest build-initramfs-embeds-boot-edn-and-system
     (let [dir (temp-dir!)]
       (try
         (spit (File. dir "system.edn") "{:aiueos/components []}")
         (let [out (File. dir "out.initramfs.cpio.gz")
               p (image/plan {:system (.getPath (File. dir "system.edn"))
                               :out (.getPath out)})
               _ (image/build-initramfs! p)
               listing (:out (shell/sh "sh" "-c"
                                       (str "gzip -dc " (pr-str (.getPath out)) " | cpio -it")))]
           (is (re-find #"etc/aiueos/boot\.edn" listing))
           (is (re-find #"etc/aiueos/system/system\.edn" listing))
           ;; GNU cpio (Linux CI) lists `find .`'s entries without the `./`
           ;; prefix bsdcpio (macOS) keeps -- accept either.
           (is (re-find #"(?m)^\.?/?init$" listing)))
         (finally (delete-tree! dir))))))
