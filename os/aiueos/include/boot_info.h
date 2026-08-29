#ifndef AIUEOS_BOOT_INFO_H
#define AIUEOS_BOOT_INFO_H

#include <stdint.h>

#define AIUEOS_BOOT_INFO_MAGIC 0x414955454f53424fULL
#define AIUEOS_BOOT_INFO_VERSION_BASE 3ULL
#define AIUEOS_BOOT_INFO_VERSION_MODEL_HANDOFF 4ULL

#define AIUEOS_MODEL_FORMAT_NONE 0U
#define AIUEOS_MODEL_FORMAT_GGUF 1U
#define AIUEOS_MODEL_HANDOFF_SHA256_VERIFIED (1ULL << 0)
#define AIUEOS_MODEL_HANDOFF_SPLIT_EXACT (1ULL << 1)
#define AIUEOS_MODEL_PAGING_PAGES 16U

/* Loader -> native-kernel ABI.  Additions are append-only: versions 2 and 3
 * end at firmware_cr3, while version 4 carries a read-only model allocation.
 * The model bytes remain LoaderData after ExitBootServices and are mapped by
 * the kernel as supervisor-only, non-executable, read-only RAM. */
struct aiueos_boot_info {
  uint64_t magic, version;
  void *memory_map;
  uint64_t memory_map_size, descriptor_size, descriptor_version;
  void *acpi_rsdp;
  uint64_t framebuffer_base, framebuffer_size;
  uint32_t framebuffer_width, framebuffer_height;
  uint32_t framebuffer_stride, framebuffer_format;
  uint64_t initramfs_base, initramfs_size;
  void *runtime_services;
  uint64_t firmware_cr3;
  uint64_t model_base, model_size;
  uint8_t model_sha256[32];
  uint32_t model_part_count, model_format;
  uint64_t model_flags, model_load_cycles;
  uint64_t model_paging_base, model_paging_pages;
};

_Static_assert(sizeof(struct aiueos_boot_info) == 208,
               "boot-info v4 ABI size changed");

#endif
