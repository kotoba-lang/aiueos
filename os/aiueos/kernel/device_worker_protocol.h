#ifndef AIUEOS_DEVICE_WORKER_PROTOCOL_H
#define AIUEOS_DEVICE_WORKER_PROTOCOL_H

#include <stdint.h>

struct aiueos_device_worker_poll {
  uint64_t job_id;
  uint32_t bos_token;
  int ready;
  int has_job;
};

int aiueos_device_worker_poll_response(
    const uint8_t *http, uint32_t length,
    struct aiueos_device_worker_poll *poll);

#endif
