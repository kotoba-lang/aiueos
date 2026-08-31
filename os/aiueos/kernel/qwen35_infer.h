/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIUEOS_QWEN35_INFER_H
#define AIUEOS_QWEN35_INFER_H

#include "qwen35_runtime.h"

#include <stdint.h>

#define AIUEOS_QWEN35_BOS_TOKEN 248044U
#define AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN 2005U
#define AIUEOS_QWEN35_WORKSPACE_BYTES 368832U

struct aiueos_qwen35_first_token_result {
  uint32_t token;
  uint32_t second_token;
  float logit;
  float second_logit;
  uint64_t compute_cycles;
  uint32_t vector_bits;
  uint32_t worker_threads;
};

typedef void (*aiueos_qwen35_progress_fn)(uint32_t completed_layers,
                                          uint32_t total_layers,
                                          int output_head);

void aiueos_qwen35_force_scalar(void);

int aiueos_qwen35_first_token(
    const struct aiueos_qwen35_model *model,
    uint32_t input_token,
    void *workspace,
    uint64_t workspace_bytes,
    aiueos_qwen35_progress_fn progress,
    struct aiueos_qwen35_first_token_result *result);

#endif
