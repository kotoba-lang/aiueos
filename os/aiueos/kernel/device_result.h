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

enum aiueos_device_worker_operation {
  AIUEOS_DEVICE_WORKER_POLL = 1,
  AIUEOS_DEVICE_WORKER_RESULT = 2
};

struct aiueos_device_worker_request {
  const struct aiueos_boot_info *boot;
  const uint8_t *mac;
  uint32_t sequence;
  enum aiueos_device_worker_operation operation;
  uint64_t job_id;
  uint32_t token;
  uint32_t second_token;
  uint64_t inference_cycles;
};

int aiueos_cpu_random_bytes(uint8_t *out, uint32_t bytes);
uint32_t aiueos_device_result_http_request(
    const struct aiueos_device_result *result, uint8_t *out, uint32_t capacity,
    char *did_out, uint32_t did_capacity);
uint32_t aiueos_device_worker_http_request(
    const struct aiueos_device_worker_request *request,
    uint8_t *out, uint32_t capacity,
    char *did_out, uint32_t did_capacity);

#endif
