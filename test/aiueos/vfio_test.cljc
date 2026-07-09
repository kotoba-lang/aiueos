(ns aiueos.vfio-test
  "Only the pure, hardware-free surface of `aiueos.vfio` is testable here (no
  VFIO device in this sandbox -- see the namespace docstring). The ioctl
  request-number encoding is cross-checked against Linux's real, stable UAPI
  constants (`linux/vfio.h`) so a transcription error in the `_IOC` macro port
  would be caught even without hardware."
  (:require [aiueos.vfio :as vfio]
            [clojure.test :refer [deftest is testing]]))

(deftest ioctl-numbers-match-real-linux-vfio-uapi
  (testing "VFIO_GET_API_VERSION = _IO(';', 100)"
    (is (= 0x3b64 vfio/ioctl-get-api-version)))
  (testing "VFIO_CHECK_EXTENSION = _IOW(';', 101, __u32)"
    (is (= 0x40043b65 vfio/ioctl-check-extension)))
  (testing "VFIO_SET_IOMMU = _IOW(';', 102, __s32)"
    (is (= 0x40043b66 vfio/ioctl-set-iommu)))
  (testing "VFIO_GROUP_GET_STATUS = _IOR(';', 103, struct vfio_group_status[8])"
    (is (= 0x80083b67 vfio/ioctl-group-get-status)))
  (testing "VFIO_GROUP_SET_CONTAINER = _IOW(';', 104, __s32)"
    (is (= 0x40043b68 vfio/ioctl-group-set-container)))
  (testing "VFIO_GROUP_GET_DEVICE_FD = _IO(';', 106)"
    (is (= 0x3b6a vfio/ioctl-group-get-device-fd)))
  (testing "VFIO_DEVICE_GET_INFO = _IOR(';', 107, struct vfio_device_info[32])"
    (is (= 0x80203b6b vfio/ioctl-device-get-info)))
  (testing "VFIO_DEVICE_GET_REGION_INFO = _IOWR(';', 108, struct vfio_region_info[32])"
    (is (= 0xc0203b6c vfio/ioctl-device-get-region-info)))
  (testing "VFIO_IOMMU_MAP_DMA = _IOW(';', 113, struct vfio_iommu_type1_dma_map[32])"
    (is (= 0x40203b71 vfio/ioctl-iommu-map-dma)))
  (testing "VFIO_IOMMU_UNMAP_DMA = _IOWR(';', 114, struct vfio_iommu_type1_dma_unmap[24])"
    (is (= 0xc0183b72 vfio/ioctl-iommu-unmap-dma))))

(deftest ioc-helper-shapes
  (is (= (vfio/io- 59 100) (vfio/ioc 0 59 100 0)))
  (is (= (vfio/ior 59 103 8) (vfio/ioc 2 59 103 8)))
  (is (= (vfio/iow 59 102 4) (vfio/ioc 1 59 102 4)))
  (is (= (vfio/iowr 59 108 32) (vfio/ioc 3 59 108 32))))

(deftest struct-layouts-are-self-consistent
  (testing "argsz/flags are always the first two u32s"
    (doseq [layout [vfio/group-status-layout vfio/region-info-layout
                    vfio/dma-map-layout vfio/dma-unmap-layout vfio/irq-set-header-layout]]
      (is (= 0 (:argsz layout)))
      (is (= 4 (:flags layout)))))
  (testing "region-info: index/cap-offset pack into the second u32 pair before the u64s"
    (is (= 8 (:index vfio/region-info-layout)))
    (is (= 12 (:cap-offset vfio/region-info-layout)))
    (is (= 16 (:size vfio/region-info-layout)))
    (is (= 24 (:offset vfio/region-info-layout)))
    (is (= 32 (:struct-size vfio/region-info-layout)))))

(deftest region-info-flags-are-distinct-bits
  (is (= #{1 2 4 8} (set (vals vfio/region-info-flag)))))
