/* SPDX-License-Identifier: Apache-2.0 */
#include "device_worker_protocol.h"

static int starts_at(const uint8_t *bytes, uint32_t length, uint32_t at,
                     const char *text) {
  while (*text) {
    if (at >= length || bytes[at++] != (uint8_t)*text++) return 0;
  }
  return 1;
}

static int find_text(const uint8_t *bytes, uint32_t length, const char *text,
                     uint32_t *after) {
  if (!bytes || !text || !after) return 0;
  for (uint32_t at = 0; at < length; at++) {
    if (starts_at(bytes, length, at, text)) {
      while (*text) { at++; text++; }
      *after = at;
      return 1;
    }
  }
  return 0;
}

static int decimal64(const uint8_t *bytes, uint32_t length, uint32_t *at,
                     uint64_t *value) {
  uint64_t parsed = 0;
  uint32_t digits = 0;
  while (*at < length && bytes[*at] >= '0' && bytes[*at] <= '9') {
    uint32_t digit = bytes[(*at)++] - '0';
    if (digits++ >= 20 || parsed > UINT64_MAX / 10U ||
        (parsed == UINT64_MAX / 10U && digit > UINT64_MAX % 10U)) return 0;
    parsed = parsed * 10U + digit;
  }
  if (!digits) return 0;
  *value = parsed;
  return 1;
}

int aiueos_device_worker_poll_response(
    const uint8_t *http, uint32_t length,
    struct aiueos_device_worker_poll *poll) {
  uint32_t at = 0;
  if (!http || !poll || length < 12 ||
      !starts_at(http, length, 0, "HTTP/1.1 2") ||
      !find_text(http, length, "\"accepted\":true", &at) ||
      !find_text(http, length, "\"operation\":\"poll\"", &at)) return 0;
  *poll = (struct aiueos_device_worker_poll){0};
  if (find_text(http, length, "\"ready\":true", &at)) poll->ready = 1;
  if (find_text(http, length,
                "\"control\":{\"action\":\"reboot-pxe\",\"command-id\":\"",
                &at)) {
    if (!decimal64(http, length, &at, &poll->control_id) ||
        at >= length || http[at] != '"' || !poll->control_id) return 0;
    poll->reboot_pxe = 1;
    return 1;
  }
  if (!find_text(http, length, "\"job-id\":\"", &at)) {
    return find_text(http, length, "\"job\":null", &at);
  }
  if (!decimal64(http, length, &at, &poll->job_id) ||
      at >= length || http[at] != '"' || !poll->job_id ||
      !find_text(http, length, "\"bos\":", &at)) return 0;
  uint64_t bos = 0;
  if (!decimal64(http, length, &at, &bos) || bos > UINT32_MAX) return 0;
  poll->bos_token = (uint32_t)bos;
  poll->has_job = 1;
  return 1;
}
