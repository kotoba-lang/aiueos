#include <stdint.h>
#include <string.h>
#include "../kernel/device_worker_protocol.h"

static int poll(const char *response, uint64_t job, uint32_t bos,
                int ready, int has_job) {
  struct aiueos_device_worker_poll parsed;
  if (!aiueos_device_worker_poll_response(
        (const uint8_t *)response, (uint32_t)strlen(response), &parsed)) return 0;
  return parsed.job_id == job && parsed.bos_token == bos &&
    parsed.ready == ready && parsed.has_job == has_job;
}

int main(void) {
  if (!poll("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{\"accepted\":true,\"operation\":\"poll\",\"ready\":true,\"job\":null}",
            0, 0, 1, 0)) return 1;
  if (!poll("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
            "5c\r\n{\"accepted\":true,\"operation\":\"poll\",\"ready\":false,"
            "\"job\":{\"job-id\":\"1788078031098390\",\"bos\":248044}}\r\n0\r\n\r\n",
            1788078031098390ULL, 248044, 0, 1)) return 2;
  {
    struct aiueos_device_worker_poll parsed;
    const char *bad = "HTTP/1.1 401 Unauthorized\r\n\r\n{\"accepted\":true}";
    if (aiueos_device_worker_poll_response(
          (const uint8_t *)bad, (uint32_t)strlen(bad), &parsed)) return 3;
  }
  return 0;
}
