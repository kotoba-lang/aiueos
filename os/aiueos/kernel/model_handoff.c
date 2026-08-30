#include <stdint.h>
#include "model_handoff.h"
#include "aiueos-model-identity.h"

#define PAGE_SIZE 4096ULL
#define HUGE_PAGE_SIZE (2ULL * 1024ULL * 1024ULL)
#define ONE_GIB (1024ULL * 1024ULL * 1024ULL)

int aiueos_model_mapping_plan(uint64_t base, uint64_t size,
                              struct aiueos_model_mapping_plan *plan) {
  if (!plan || !size || (base & (HUGE_PAGE_SIZE - 1)) ||
      base < ONE_GIB || base >= AIUEOS_MODEL_IDENTITY_LIMIT ||
      size > AIUEOS_MODEL_IDENTITY_LIMIT - base)
    return 0;
  uint64_t end = base + size;
  if (base < AIUEOS_MODEL_RESERVED_APIC_END &&
      end > AIUEOS_MODEL_RESERVED_APIC_START)
    return 0;
  uint64_t first = base & ~(HUGE_PAGE_SIZE - 1);
  uint64_t last = (end - 1) & ~(HUGE_PAGE_SIZE - 1);
  uint32_t first_pdpt = (uint32_t)(first >> 30);
  uint32_t last_pdpt = (uint32_t)(last >> 30);
  uint32_t directories = last_pdpt - first_pdpt + 1U;
  if (!directories || directories > AIUEOS_MODEL_MAX_PDPT_DIRECTORIES ||
      first_pdpt == 0U || (first_pdpt <= 3U && last_pdpt >= 3U))
    return 0;
  *plan = (struct aiueos_model_mapping_plan){
    first, last, first_pdpt, last_pdpt, directories
  };
  return 1;
}

int aiueos_model_handoff_validate(const struct aiueos_boot_info *boot,
                                  uint64_t expected_size,
                                  const uint8_t expected_sha256[32],
                                  uint32_t expected_parts,
                                  const uint8_t header[8]) {
  if (!boot || boot->magic != AIUEOS_BOOT_INFO_MAGIC ||
      boot->version < AIUEOS_BOOT_INFO_VERSION_MODEL_HANDOFF ||
      !boot->model_base || boot->model_size != expected_size ||
      boot->model_part_count != expected_parts ||
      boot->model_format != AIUEOS_MODEL_FORMAT_GGUF ||
      !boot->model_paging_base ||
      (boot->model_paging_base & (PAGE_SIZE - 1)) ||
      boot->model_paging_base >= ONE_GIB ||
      (boot->version < AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED &&
       boot->model_paging_pages != AIUEOS_MODEL_PAGING_PAGES) ||
      boot->model_flags != (AIUEOS_MODEL_HANDOFF_SHA256_VERIFIED |
                            AIUEOS_MODEL_HANDOFF_SPLIT_EXACT) || !header)
    return 0;
  struct aiueos_model_mapping_plan plan;
  if (!aiueos_model_mapping_plan(boot->model_base, boot->model_size, &plan))
    return 0;
  for (uint32_t i = 0; i < 32; i++)
    if (boot->model_sha256[i] != expected_sha256[i]) return 0;
  if (header[0] != 'G' || header[1] != 'G' ||
      header[2] != 'U' || header[3] != 'F')
    return 0;
  uint32_t version = (uint32_t)header[4] | ((uint32_t)header[5] << 8) |
                     ((uint32_t)header[6] << 16) |
                     ((uint32_t)header[7] << 24);
  return version == 3U;
}

int aiueos_qwen38_model_handoff_admit(const struct aiueos_boot_info *boot) {
  const uint8_t *header = boot && boot->model_base ?
    (const uint8_t *)(uintptr_t)boot->model_base : 0;
  return aiueos_model_handoff_validate(
    boot, AIUEOS_MODEL_TOTAL_BYTES, aiueos_expected_model_sha256,
    AIUEOS_MODEL_PART_COUNT, header);
}
