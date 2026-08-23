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
            [clojure.string :as str]
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

(deftest wm-requires-two-stacked-surfaces
  (testing "boot desktop is admitted; one notes iframe is the named red"
    (let [d (desktop/boot-desktop)
          one (desktop/one-surface-desktop)]
      (is (desktop/wm-admitted? d))
      (is (nil? (desktop/wm-refuse-reason d)))
      (is (not (desktop/wm-admitted? one)))
      (is (= :one-surface (desktop/wm-refuse-reason one)))))
  (testing "IME leftover on a boot desktop is kanji, not ime-absent"
    (let [st (desktop/ime-leftover (desktop/boot-desktop))]
      (is (true? (:ime? st)))
      (is (= :kanji-absent (:leftover st))))))

(deftest wm-hit-prefers-z-stack-not-key-order
  (let [d (desktop/boot-desktop)
        px (:x desktop/overlap-point)
        py (:y desktop/overlap-point)
        hit (desktop/hit-window d px py)
        keys (desktop/key-order-hit d px py)
        front (desktop/z-front d)]
    (is (= 2 (count (:windows d))))
    (is (= front hit) "overlap must hit the front surface")
    (is (not= hit keys)
        "gate is red if hit-window ignores z-order and scans map keys")
    (is (= 2 hit) "last opened window is front at boot")))

(deftest wm-raise-changes-front-and-input-target
  (let [d (desktop/boot-desktop)
        px (:x desktop/overlap-point)
        py (:y desktop/overlap-point)
        front (desktop/z-front d)
        back (desktop/z-back d)
        raised (desktop/raise d back)
        [_ ev] (desktop/route-pointer raised px py)]
    (is (not= front back))
    (is (= back (desktop/z-front raised))
        "raising the back window must change who is front")
    (is (not= front (desktop/z-front raised)))
    (is (= back (desktop/hit-window raised px py)))
    (is (desktop/occluded-at? raised front px py)
        "the old front still contains the point but is occluded")
    (is (= back (:hit ev)))
    (is (= [:panel back] (:input-target ev))
        "clicks route to the focused guest; fails if z-order is ignored")
    (is (= :kanji-absent (:leftover (desktop/ime-leftover raised)))
        "WM green does not require IME conversion; leftover is kanji")))

(deftest generated-spa-is-the-wm-face
  (is (comp/html-has-wm-face? (pb/session-html))
      "gate is red until #desktop has two DADS-decorated stacked windows")
  (is (not (comp/html-has-wm-face?
            (str "<html>href=\"#session\" href=\"#setup\" href=\"#desktop\" "
                 "dads-button jp-go-dds id=\"kami-viewport\" kami.webgpu "
                 "window.__aiueosSessionAlive "
                 "<pre id=\"compositor-out\">{surfaces:1}</pre></html>")))
      "a JSON dump of one surface is not a window manager"))

(deftest ime-on-consumes-latin-and-commits-kana
  (let [d (desktop/boot-desktop)
        [d1 e1] (desktop/route-key d "k")
        [d2 e2] (desktop/route-key d1 "a")
        [d3 e3] (desktop/route-key d2 "Enter")]
    (is (true? (:consumed? e1)))
    (is (= "" (:guest-text e1)))
    (is (false? (:latin-leaked? e1)))
    (is (true? (:consumed? e2)))
    (is (= "" (:guest-text e2)))
    (is (= "か" (:preedit e2)))
    (is (= "か" (:guest-text e3)))
    (is (desktop/ime-admitted? d3))
    (is (not (re-find #"[a-zA-Z]" (str (get-in d3 [:ime :guest-log])))))))

(deftest ime-off-is-the-named-red
  (let [d (desktop/set-ime (desktop/boot-desktop) false)
        [d1 e1] (desktop/route-key d "k")
        [d2 e2] (desktop/route-key d1 "a")]
    (is (false? (:consumed? e1)))
    (is (= :ime-bypass (:reason e1)))
    (is (= "k" (:guest-text e1)))
    (is (= "a" (:guest-text e2)))
    (is (str/includes? (str (get-in d2 [:ime :guest-log])) "ka"))
    (is (not (desktop/ime-admitted? d2)))))

(deftest generated-spa-is-the-ime-face
  (is (comp/html-has-ime-face? (pb/session-html))
      "gate is red until #desktop has the DADS IME candidate bar")
  (is (not (comp/html-has-ime-face?
            (str "<html>href=\"#session\" href=\"#setup\" href=\"#desktop\" "
                 "dads-button jp-go-dds id=\"kami-viewport\" kami.webgpu "
                 "window.__aiueosSessionAlive id=\"wm-stage\" "
                 "class=\"wm-window\" class=\"wm-window\" wm-titlebar "
                 "dads-chip-label data-raise dads-heading</html>")))
      "WM decorations without #ime-bar are not the IME gate"))

