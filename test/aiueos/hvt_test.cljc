(ns aiueos.hvt-test
  "Pure unit tests for `aiueos.hvt` -- the parts that don't need a live
  `/dev/kvm`: ioctl-number encoding, struct-field offsets, the aarch64 core
  PC register id, and the fixed guest program. The live KVM boot loop
  (`aiueos.hvt/spike`) is exercised by the #110 smoke gate inside a Linux/KVM
  VM, not here (this suite runs on any JVM host, incl. macOS CI), mirroring
  `aiueos.vfio`'s hardware-honesty split."
  (:require [clojure.test :refer [deftest is testing]]
            [clojure.java.io :as io]
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

(deftest psci-diagnostic-guest
  (testing "the PSCI variant: serial HI\\n, then hvc PSCI SYSTEM_OFF, then a poweroff fall-through"
    (is (= 13 (count hvt/guest-program-psci)))
    (is (= 0x84000008 hvt/psci-system-off-fid) "PSCI 0.2 SYSTEM_OFF fid")
    ;; movz x1 base, then the hvc, then the fall-through poweroff store + b .
    (is (= 0xD2A12001 (first hvt/guest-program-psci)))
    (is (= 1 (count (filter #(= 0xD4000002 %) hvt/guest-program-psci)))
        "exactly one hvc #0")
    (is (= 0x14000000 (last hvt/guest-program-psci)) "parks in b .")
    ;; the diagnostic fall-through: a poweroff store reached only if PSCI did
    ;; not fire, so :halt :mmio-poweroff in the receipt means PSCI was ignored.
    (is (= 1 (count (filter #(= 0x39002020 %) hvt/guest-program-psci)))
        "one poweroff fall-through store")
    (is (every? #(<= 0 % 0xFFFFFFFF) hvt/guest-program-psci))))

(deftest vcpu-init-psci-constants
  (testing "KVM_ARM_VCPU_PSCI_0_2 feature-bit layout (features[0] at struct offset 4, bit 0)"
    (is (= 4 hvt/vcpu-init-features0-offset))
    (is (= 1 hvt/vcpu-feature-psci-0-2) "KVM_ARM_VCPU_PSCI_0_2 == feature bit 0")))

(defn- vec-accessor [v] (fn [off] (nth v off)))

(deftest rd-le-reads-little-endian
  (testing "rd-le assembles n little-endian bytes into a long"
    (let [rd (vec-accessor [0x78 0x56 0x34 0x12 0xff 0xee])]
      (is (= 0x12345678 (hvt/rd-le rd 0 4)))
      (is (= 0x5678 (hvt/rd-le rd 0 2)))
      (is (= 0xeeff (hvt/rd-le rd 4 2)))
      (is (= 0xEEFF1234 (hvt/rd-le rd 2 4))))))

(deftest elf-load-range-spans-segments
  (testing "elf-load-range covers all PT_LOAD vaddr..vaddr+memsz, page-aligned"
    ;; a single segment at 0x40000000, memsz 0x28 -> [0x40000000, 0x40001000)
    (is (= [0x40000000 0x40001000]
           (hvt/elf-load-range [{:vaddr 0x40000000 :memsz 0x28}] 4096)))
    ;; two segments -> lo rounds down, hi rounds up across both
    (is (= [0x40000000 0x40003000]
           (hvt/elf-load-range [{:vaddr 0x40000100 :memsz 0x10}
                                {:vaddr 0x40002000 :memsz 0x40}] 4096)))))

(deftest parse-elf64-rejects-non-elf
  (testing "a bad magic throws with a specific reason"
    (is (thrown-with-msg? clojure.lang.ExceptionInfo #"bad magic"
          (hvt/parse-elf64 (vec-accessor [0x00 0x01 0x02 0x03 0x04 0x05]))))))

(deftest parse-elf64-on-real-fixture
  (testing "parsing the checked-in aarch64 ELF fixture yields the expected load plan"
    (let [bytes (with-open [in (io/input-stream (io/resource "hvt/guest-aarch64.elf"))]
                  (.readAllBytes in))
          rd (fn [off] (bit-and (long (aget bytes (int off))) 0xff))
          {:keys [class machine entry segments]} (hvt/parse-elf64 rd)]
      (is (= hvt/elf-class-64 class) "ELFCLASS64")
      (is (= (:aarch64 hvt/elf-machine) machine) "EM_AARCH64 (0xB7)")
      (is (= 0x40000000 entry) "e_entry")
      (is (= 1 (count segments)) "one PT_LOAD")
      (let [{:keys [offset vaddr filesz memsz]} (first segments)]
        (is (= 0x1000 offset) "p_offset")
        (is (= 0x40000000 vaddr) "p_vaddr")
        (is (= 0x28 filesz) "p_filesz (10 aarch64 words)")
        (is (= 0x28 memsz) "p_memsz"))
      (is (= [0x40000000 0x40001000] (hvt/elf-load-range segments 4096))))))
