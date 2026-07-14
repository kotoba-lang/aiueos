(ns aiueos.vm-test
  (:require [aiueos.vm :as vm]
            [clojure.test :refer [deftest is testing]]))

(deftest plan-defaults
  (let [p (vm/plan {:kernel "Image" :initramfs "init.cpio.gz"})]
    (is (= "1024M" (:memory p)))
    (is (= "2" (:cpus p)))
    (is (= "console=ttyAMA0 panic=0 rdinit=/init" (:cmdline p)))
    (is (= "none" (:graphics p)))
    (is (= "none" (:display p)))
    (is (= "pl011" (:console p)))
    (is (= "qemu-system-aarch64" (:qemu-binary p)))))

(deftest x86-profile-has-q35-and-serial-console
  (let [p (vm/plan {:kernel "bzImage" :initramfs "init.cpio.gz" :arch "x86_64" :accel "tcg"})
        a (vm/argv p)]
    (is (= "qemu-system-x86_64" (:qemu-binary p)))
    (is (= "console=ttyS0 panic=0 rdinit=/init" (:cmdline p)))
    (is (some #{"q35,accel=tcg"} a))))

(deftest plan-requires-kernel-and-initramfs
  (is (thrown? #?(:clj Exception :cljs js/Error) (vm/plan {:initramfs "init.cpio.gz"})))
  (is (thrown? #?(:clj Exception :cljs js/Error) (vm/plan {:kernel "Image"}))))

(deftest plan-virtio-gpu-defaults-display-to-cocoa
  (let [p (vm/plan {:kernel "Image" :initramfs "init.cpio.gz" :graphics "virtio-gpu"})]
    (is (= "cocoa" (:display p)))))

(deftest plan-rejects-unknown-graphics-and-console
  (is (thrown? #?(:clj Exception :cljs js/Error)
               (vm/plan {:kernel "Image" :initramfs "init.cpio.gz" :graphics "vga"})))
  (is (thrown? #?(:clj Exception :cljs js/Error)
               (vm/plan {:kernel "Image" :initramfs "init.cpio.gz" :console "serial0"}))))

(deftest plan-rejects-display-without-virtio-gpu
  (is (thrown? #?(:clj Exception :cljs js/Error)
               (vm/plan {:kernel "Image" :initramfs "init.cpio.gz" :display "cocoa"}))))

(deftest argv-shape-minimal
  (let [p (vm/plan {:kernel "Image" :initramfs "init.cpio.gz"})]
    (is (= ["qemu-system-aarch64" "-machine" (str "virt,accel=" (#'vm/accel-name p)) "-cpu" "host"
            "-smp" "2" "-m" "1024M" "-nographic"
            "-kernel" "Image" "-initrd" "init.cpio.gz"
            "-append" "console=ttyAMA0 panic=0 rdinit=/init"]
           (vm/argv p)))))

(deftest argv-with-virtio-gpu-block-and-console
  (let [p (vm/plan {:kernel "Image" :initramfs "init.cpio.gz" :graphics "virtio-gpu"
                     :block "disk.raw" :console "virtio-console" :console-socket "c.sock"})
        a (vm/argv p)]
    (testing "virtio-gpu device + display, not -nographic"
      (is (some #(= "virtio-gpu-pci" %) a))
      (is (not (some #(= "-nographic" %) a))))
    (testing "virtio-blk drive + device"
      (is (some #(= "file=disk.raw,if=none,format=raw,id=aiueosblk" %) a))
      (is (some #(= "virtio-blk-pci,drive=aiueosblk" %) a)))
    (testing "virtio-console chardev + device"
      (is (some #(= "socket,id=aiueoscon,path=c.sock,server=on,wait=off" %) a))
      (is (some #(= "virtconsole,chardev=aiueoscon,name=aiueos.console.0" %) a)))))

(deftest command-line-quotes-multi-word-args
  (is (re-find #"\"console=ttyAMA0 panic=0 rdinit=/init\"" (vm/command-line (vm/plan {:kernel "Image" :initramfs "i.gz"})))))

#?(:clj
   (deftest boot-throws-on-nonzero-exit
     (testing "qemu-binary overridden to a command guaranteed to fail fast"
       (let [kernel (java.io.File/createTempFile "kernel" "")
             initrd (java.io.File/createTempFile "initrd" "")]
         (try
           (is (thrown? Exception
                        (vm/boot! (assoc (vm/plan {:kernel (.getPath kernel)
                                                   :initramfs (.getPath initrd)})
                                         :qemu-binary "false"))))
           (finally (.delete kernel) (.delete initrd)))))))
