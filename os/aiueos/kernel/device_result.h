#ifndef AIUEOS_DEVICE_RESULT_H
#define AIUEOS_DEVICE_RESULT_H

#include <stdint.h>
#include "../include/boot_info.h"

struct aiueos_device_result {
  const struct aiueos_boot_info *boot;
  const uint8_t *mac;
  uint32_t token;
  uint32_t second_token;
  uint64_t inference_cycles;
};

int aiueos_cpu_random_bytes(uint8_t *out, uint32_t bytes);
uint32_t aiueos_device_result_http_request(
    const struct aiueos_device_result *result, uint8_t *out, uint32_t capacity,
    char *did_out, uint32_t did_capacity);

#endif
