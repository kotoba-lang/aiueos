#ifndef AIUEOS_DEVICE_RESULT_H
#define AIUEOS_DEVICE_RESULT_H

#include <stdint.h>
#include "../include/boot_info.h"

struct aiueos_device_result {
  const struct aiueos_boot_info *boot;
  const uint8_t *mac;
  uint32_t token;
  uint32_t second_token;
  uint32_t generated_tokens;
  uint32_t decode_tokens;
  uint64_t first_token_cycles;
  uint64_t decode_cycles;
  uint64_t inference_cycles;
  uint32_t vector_bits;
  uint32_t worker_threads;
};

enum aiueos_device_worker_operation {
  AIUEOS_DEVICE_WORKER_POLL = 1,
  AIUEOS_DEVICE_WORKER_RESULT = 2,
  AIUEOS_DEVICE_WORKER_CONTROL_ACK = 3
};

struct aiueos_device_worker_request {
  const struct aiueos_boot_info *boot;
  const uint8_t *mac;
  uint32_t sequence;
  enum aiueos_device_worker_operation operation;
  uint64_t job_id;
  uint32_t token;
  uint32_t second_token;
  uint32_t generated_tokens;
  uint32_t decode_tokens;
  uint64_t first_token_cycles;
  uint64_t decode_cycles;
  uint64_t inference_cycles;
  uint32_t vector_bits;
  uint32_t worker_threads;
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
