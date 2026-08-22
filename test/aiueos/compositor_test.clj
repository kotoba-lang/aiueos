(ns aiueos.compositor-test
  "Discriminating tests for the compositor process.

  `clojure -M:test` of this ns is not the Mac QEMU gate. Hosted surfaces
  are `clojure -M:compositor smoke`. Guest 2D is `clojure -M:compositor gpu`.
  These tests name the reds so HTTP-only `#session` cannot stand in for a
  display session, a wiped desktop cannot count as restore, and query-pci
  cannot stand in for CREATE+FLUSH."
  (:require [aiueos.compositor :as comp]
            [aiueos.compositor.desktop :as desktop]
            [aiueos.phone-bind :as pb]
            [clojure.java.io :as io]
            [clojure.test :refer [deftest is testing]]))

(deftest boot-desktop-owns-surfaces
  (let [d (desktop/boot-desktop)]
    (is (desktop/restore-admitted? d))
    (is (= 2 (count (:windows d))))
    (is (= "#session" (:session-fragment d)))
    (is (some (fn [[_ w]]
                (= "aiueos.session" (get-in w [:component :app-id])))
              (:windows d)))
    (is (some (fn [[_ w]]
                (= :kami (first (get-in w [:component :content]))))
              (:windows d))
        "guest surface is a kami content window, not CSS")))

(deftest wiped-state-is-the-named-red
  (testing "restore of a missing file must not be admitted"
    (let [wiped (desktop/empty-desktop)]
      (is (not (desktop/restore-admitted? wiped)))
      (is (true? (:wiped? wiped)))
      (is (empty? (:windows wiped)))))
  (testing "persist then delete then load is refused, not an empty success"
    (let [dir (io/file (str (System/getProperty "java.io.tmpdir")
                            "/aiueos-comp-wipe-" (System/nanoTime)))
          _ (comp/persist! dir (desktop/boot-desktop))
          restored (comp/load-persisted dir)
          _ (.delete (comp/desktop-file dir))
          wiped (comp/load-persisted dir)]
      (is (desktop/restore-admitted? restored)
          "live persist must restore windows")
      (is (= "#session" (:session-fragment restored)))
      (is (not (desktop/restore-admitted? wiped))
          "gate is red if restore of wiped state is treated as success"))))

(deftest http-only-session-is-not-compositor
  (is (not (comp/html-has-desktop-face?
            "<html>href=\"#session\" dads-button jp-go-dds</html>"))
      "P1 SPA without #desktop / kami viewport is not this gate"))

(deftest gpu-argv-keeps-headless-bind
  (let [argv (comp/gpu-argv {:qemu "qemu-system-aarch64"
                             :firmware "/fw.fd"
                             :qmp "/tmp/q.qmp"
                             :serial "/tmp/s.log"
                             :accel "hvf"})]
    (is (comp/gpu-argv? argv))
    (is (pb/headless-argv? argv)
        "compositor QEMU must not require a local keyboard/cocoa console")
    (is (= "none" (second (drop-while #(not= % "-display") argv))))
    (is (some #{"virtio-gpu-pci"} argv))
    (is (not (some #{"cocoa" "gtk" "sdl"} argv)))))

(deftest pci-probe-requires-the-device-name
  (is (not (comp/pci-names-gpu? "{\"return\":[]}")))
  (is (not (comp/pci-names-gpu? "{\"class_info\":{\"desc\":\"Ethernet controller\"}}")))
  (is (comp/pci-names-gpu? "{\"return\":[{\"devices\":[{\"id\":\"virtio-gpu-pci\"}]}]}"))
  (is (comp/pci-names-gpu?
       "{\"class_info\": {\"class\": 896, \"desc\": \"Display controller\"}, \"id\": {\"device\": 4176}}")
      "QEMU query-pci names a Display controller, not the qdev string"))

(deftest generated-spa-is-the-desktop-face
  (is (comp/html-has-desktop-face? (pb/session-html))
      "gate is red until apps/session/index.html is regenerated with #desktop"))

(deftest p1b-default-argv-stays-without-gpu
  (let [argv (pb/chassis-argv-shape {:qemu "qemu-system-aarch64"
                                     :firmware "/fw.fd"
                                     :qmp "/tmp/q.qmp"
                                     :serial "/tmp/s.log"
                                     :accel "hvf"})]
    (is (pb/headless-argv? argv))
    (is (not (some #{"virtio-gpu-pci"} argv))
        "phone-bind default must not require a GPU device")))

(deftest guest-2d-serial-is-the-gpu-gate
  (testing "CREATE+FLUSH serial is green"
    (is (comp/guest-2d-create+flush?
         (str "AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded\n"
              "AIUEOS_VIRTIO_GPU_CREATE result=ok resource=1 format=2 w=32 h=32\n"
              "AIUEOS_VIRTIO_GPU_FLUSH result=ok resource=1\n")))
    (is (:green? (comp/gpu-2d-result
                  {:serial (str "AIUEOS_VIRTIO_GPU_CREATE result=ok resource=1 format=2 w=32 h=32\n"
                                "AIUEOS_VIRTIO_GPU_FLUSH result=ok resource=1\n")}))))
  (testing "display-info without create/flush is the named leftover"
    (is (not (comp/guest-2d-create+flush?
              "AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded\n")))
    (let [r (comp/gpu-2d-result
             {:serial (str "AIUEOS_VIRTIO_GPU_OK modern-pci controlq display-info bounded\n"
                           "AIUEOS_VIRTIO_GPU_CREATE result=absent\n"
                           "AIUEOS_VIRTIO_GPU_FLUSH result=absent\n")})]
      (is (not (:green? r)))
      (is (= 1 (:exit r)))
      (is (= :gpu-2d-create-flush-absent (:reason r)))))
  (testing "query-pci listing is not guest 2D"
    (let [r (comp/gpu-2d-result {:pci-only? true :serial ""})]
      (is (not (:green? r)))
      (is (= :pci-device-listed-does-not-count (:reason r)))))
  (testing "unmeasured is exit 3, not a silent pass"
    (let [r (comp/gpu-2d-result {:qemu-unmeasured? true})]
      (is (= 3 (:exit r)))
      (is (= :unmeasured (:reason r))))))
