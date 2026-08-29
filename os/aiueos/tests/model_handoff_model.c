#include <stdint.h>
#include <stdio.h>
#include "../kernel/model_handoff.h"

static const uint8_t digest[32] = {
  0xc0,0xb7,0xc3,0x03,0x86,0x81,0xed,0x2e,
  0x30,0x40,0x45,0x6c,0x1d,0xd4,0x5f,0x98,
  0x58,0xb6,0xc2,0x29,0x0b,0xed,0x17,0x2c,
  0x70,0x38,0x8a,0x94,0x87,0x4f,0x3e,0xee
};

static int check(int condition, const char *name) {
  if (condition) return 1;
  fprintf(stderr, "FAIL %s\n", name);
  return 0;
}

int main(void) {
  int ok = 1;
  struct aiueos_model_mapping_plan plan;
  const uint64_t eight_gib = 8ULL * 1024ULL * 1024ULL * 1024ULL;
  ok &= check(aiueos_model_mapping_plan(eight_gib, 10934860704ULL, &plan),
              "production mapping plan");
  ok &= check(plan.first_pdpt == 8U && plan.last_pdpt == 18U &&
              plan.directory_count == 11U, "production directory span");
  ok &= check(!aiueos_model_mapping_plan(
                5ULL * 1024ULL * 1024ULL * 1024ULL / 2ULL,
                1024ULL * 1024ULL * 1024ULL, &plan),
              "APIC window overlap refused");
  ok &= check(!aiueos_model_mapping_plan(eight_gib, 17ULL << 30, &plan),
              "directory bound enforced");
  ok &= check(!aiueos_model_mapping_plan(eight_gib + 1, 4096, &plan),
              "unaligned base refused");
  ok &= check(!aiueos_model_mapping_plan(eight_gib + 4096, 4096, &plan),
              "4K-only aligned base refused");

  struct aiueos_boot_info boot = {0};
  boot.magic = AIUEOS_BOOT_INFO_MAGIC;
  boot.version = AIUEOS_BOOT_INFO_VERSION_MODEL_HANDOFF;
  boot.model_base = eight_gib;
  boot.model_size = 10934860704ULL;
  for (uint32_t i = 0; i < 32; i++) boot.model_sha256[i] = digest[i];
  boot.model_part_count = 3;
  boot.model_format = AIUEOS_MODEL_FORMAT_GGUF;
  boot.model_paging_base = 512ULL * 1024ULL * 1024ULL;
  boot.model_paging_pages = AIUEOS_MODEL_PAGING_PAGES;
  boot.model_flags = AIUEOS_MODEL_HANDOFF_SHA256_VERIFIED |
                     AIUEOS_MODEL_HANDOFF_SPLIT_EXACT;
  const uint8_t gguf_v3[8] = {'G','G','U','F',3,0,0,0};
  ok &= check(aiueos_model_handoff_validate(
                &boot, boot.model_size, digest, 3, gguf_v3),
              "exact metadata admitted");
  uint8_t bad_header[8] = {'G','G','U','F',2,0,0,0};
  ok &= check(!aiueos_model_handoff_validate(
                &boot, boot.model_size, digest, 3, bad_header),
              "GGUF version refused");
  boot.model_flags = AIUEOS_MODEL_HANDOFF_SHA256_VERIFIED;
  ok &= check(!aiueos_model_handoff_validate(
                &boot, boot.model_size, digest, 3, gguf_v3),
              "partial admission flags refused");
  boot.model_flags |= AIUEOS_MODEL_HANDOFF_SPLIT_EXACT;
  boot.model_sha256[0] ^= 1;
  ok &= check(!aiueos_model_handoff_validate(
                &boot, boot.model_size, digest, 3, gguf_v3),
              "digest mismatch refused");
  if (!ok) return 1;
  puts("AIUEOS_MODEL_HANDOFF_MODEL_OK exact-identity gguf-v3 map=read-only-plan");
  return 0;
}
