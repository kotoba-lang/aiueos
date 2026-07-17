(ns aiueos.hvt-test
  "Pure unit tests for `aiueos.hvt` -- the parts that don't need a live
  `/dev/kvm`: ioctl-number encoding, struct-field offsets, the aarch64 core
  PC register id, and the fixed guest program. The live KVM boot loop
  (`aiueos.hvt/spike`) is exercised by the #110 smoke gate inside a Linux/KVM
  VM, not here (this suite runs on any JVM host, incl. macOS CI), mirroring
  `aiueos.vfio`'s hardware-honesty split."
  (:require [clojure.test :refer [deftest is testing]]
            [aiueos.hvt :as hvt]))

(deftest ioctl-numbers
  (testing "KVM ioctl request numbers match linux/kvm.h (KVMIO=0xAE)"
    ;; _IO(0xAE, nr)
    (is (= 0xAE00 hvt/get-api-version))
    (is (= 0xAE01 hvt/create-vm))
    (is (= 0xAE04 hvt/get-vcpu-mmap-size))
    (is (= 0xAE41 hvt/create-vcpu))
    (is (= 0xAE80 hvt/run))
    ;; _IOW(0xAE, 0x46, sizeof kvm_userspace_memory_region=32)
    (is (= 0x4020AE46 hvt/set-user-memory-region))
    ;; _IOW(0xAE, 0xac, sizeof kvm_one_reg=16)
    (is (= 0x4010AEAC hvt/set-one-reg))
    ;; _IOW(0xAE, 0xae, sizeof kvm_vcpu_init=32)
    (is (= 0x4020AEAE hvt/arm-vcpu-init))
    ;; _IOR(0xAE, 0xaf, sizeof kvm_vcpu_init=32)
    (is (= 0x8020AEAF hvt/arm-preferred-target))))

(deftest ioc-encoding
  (testing "the _IOC direction/type/nr/size bit layout"
    (is (= 0xAE00 (hvt/io- 0xAE 0x00)))
    (is (= 0x4020AE46 (hvt/iow 0xAE 0x46 32)))
    (is (= 0x8020AEAF (hvt/ior 0xAE 0xaf 32)))))

(deftest arm64-pc-register-id
  (testing "KVM_REG_ARM64 | SIZE_U64 | ARM_CORE | (offsetof(pc)/4 = 0x40)"
    (is (= 0x6030000000100040 hvt/arm64-core-reg-pc))))

(deftest struct-offsets
  (testing "kvm_userspace_memory_region field byte offsets"
    (is (= {:slot 0 :flags 4 :guest-phys-addr 8 :memory-size 16 :userspace-addr 24}
           hvt/mem-region-layout)))
  (testing "kvm_run fields V0 reads: exit_reason @8, union @32"
    (is (= 8 (:exit-reason hvt/kvm-run-layout)))
    (is (= 32 (:mmio-phys-addr hvt/kvm-run-layout)))
    (is (= 40 (:mmio-data hvt/kvm-run-layout)))
    (is (= 48 (:mmio-len hvt/kvm-run-layout)))
    (is (= 52 (:mmio-is-write hvt/kvm-run-layout)))
    (is (= 32 (:sysevent-type hvt/kvm-run-layout))))
  (testing "exit-reason + system-event-type constants (linux/kvm.h)"
    (is (= 6 (:mmio hvt/exit-reason)))
    (is (= 24 (:system-event hvt/exit-reason)))
    (is (= 1 (:shutdown hvt/system-event-type)))))

(deftest guest-program
  (testing "the V0 guest is 10 aarch64 words: write HI\\n via serial MMIO, then the poweroff port"
    (is (= 10 (count hvt/guest-program-words)))
    (is (= "HI\n" hvt/guest-serial-expected))
    (is (= 0x09000000 hvt/guest-mmio-base))
    (is (= 0x09000008 hvt/guest-poweroff-addr)
        "poweroff port is serial base + 8")
    ;; spot-check the load-address movz and the b . that parks the guest.
    (is (= 0xD2A12001 (first hvt/guest-program-words))   ; movz x1,#0x0900,lsl#16
        "x1 <- 0x09000000 (MMIO base)")
    (is (= 0x14000000 (last hvt/guest-program-words))    ; b .
        "guest ends parked in b . (VMM halts on the poweroff write)")
    ;; three strb w0,[x1] stores to the serial port, one per emitted byte.
    (is (= 3 (count (filter #(= 0x39000020 %) hvt/guest-program-words))))
    ;; one strb w0,[x1,#8] store to the poweroff port.
    (is (= 1 (count (filter #(= 0x39002020 %) hvt/guest-program-words)))
        "one poweroff-port store")
    ;; every word fits in 32 bits.
    (is (every? #(<= 0 % 0xFFFFFFFF) hvt/guest-program-words))))
