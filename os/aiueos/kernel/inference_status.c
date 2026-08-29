/* SPDX-License-Identifier: Apache-2.0 */
#include "inference_status.h"

static int bounded_display_text(const char *text, int required) {
  if (!text) return !required;
  uint32_t length = 0;
  while (length <= AIUEOS_INFERENCE_STATUS_TEXT_MAX && text[length]) {
    unsigned char c = (unsigned char)text[length++];
    if (c < 0x20U || c > 0x7eU) return 0;
  }
  return length && length <= AIUEOS_INFERENCE_STATUS_TEXT_MAX;
}

static int duration(uint64_t value) {
  return value == AIUEOS_INFERENCE_UNMEASURED || value > 0;
}

int aiueos_inference_status_valid(const struct aiueos_inference_status *status) {
  if (!status || status->abi_version != AIUEOS_INFERENCE_STATUS_ABI_VERSION ||
      status->byte_size != sizeof(*status) ||
      status->phase < AIUEOS_INFERENCE_ADMISSION ||
      status->phase > AIUEOS_INFERENCE_ERROR ||
      !bounded_display_text(status->model, 1) ||
      !bounded_display_text(status->quant, 1) ||
      !bounded_display_text(status->detail, 0) ||
      status->prompt_tokens > AIUEOS_INFERENCE_TOKEN_MAX ||
      status->generated_tokens > AIUEOS_INFERENCE_TOKEN_MAX ||
      status->target_tokens > AIUEOS_INFERENCE_TOKEN_MAX ||
      (status->target_tokens && status->generated_tokens > status->target_tokens) ||
      !duration(status->load_ns) || !duration(status->prefill_ns) ||
      !duration(status->decode_ns) ||
      !duration(status->time_to_first_token_ns)) return 0;

  if (status->phase == AIUEOS_INFERENCE_COMPLETE &&
      (!status->generated_tokens ||
       (status->decode_ns == AIUEOS_INFERENCE_UNMEASURED &&
        !status->compute_cycles))) return 0;
  return 1;
}

uint64_t aiueos_inference_milli_tokens_per_second(uint32_t tokens,
                                                  uint64_t elapsed_ns) {
  if (!tokens || !elapsed_ns || elapsed_ns == AIUEOS_INFERENCE_UNMEASURED ||
      tokens > AIUEOS_INFERENCE_TOKEN_MAX) return AIUEOS_INFERENCE_UNMEASURED;
  /* token bound above keeps tokens * 1e12 inside uint64_t. */
  return ((uint64_t)tokens * 1000000000000ULL) / elapsed_ns;
}
