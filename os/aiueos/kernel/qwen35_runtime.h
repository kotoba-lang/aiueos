/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AIUEOS_QWEN35_RUNTIME_H
#define AIUEOS_QWEN35_RUNTIME_H

#include <stdint.h>

#define AIUEOS_QWEN35_TENSOR_COUNT 866U
#define AIUEOS_QWEN35_METADATA_COUNT 50U
#define AIUEOS_QWEN35_LAYER_COUNT 65U
#define AIUEOS_QWEN35_TRUNK_LAYER_COUNT 64U
#define AIUEOS_QWEN35_LINEAR_LAYER_COUNT 48U
#define AIUEOS_QWEN35_FULL_LAYER_COUNT 16U
#define AIUEOS_QWEN35_NEXTN_LAYER_COUNT 1U
#define AIUEOS_QWEN35_CONTEXT_LENGTH 262144U
#define AIUEOS_QWEN35_EMBEDDING_LENGTH 5120U
#define AIUEOS_QWEN35_FEED_FORWARD_LENGTH 17408U
#define AIUEOS_QWEN35_VOCAB_SIZE 248320U
#define AIUEOS_QWEN35_DATA_OFFSET 10996640ULL
#define AIUEOS_QWEN35_ARTIFACT_BYTES 10934860704ULL
#define AIUEOS_QWEN35_MAX_GGML_TYPE 31U

enum aiueos_ggml_type {
  AIUEOS_GGML_F32 = 0,
  AIUEOS_GGML_Q8_0 = 8,
  AIUEOS_GGML_Q2_K = 10,
  AIUEOS_GGML_Q3_K = 11,
  AIUEOS_GGML_Q4_K = 12,
  AIUEOS_GGML_Q5_K = 13,
  AIUEOS_GGML_Q6_K = 14,
  AIUEOS_GGML_IQ2_XXS = 16,
  AIUEOS_GGML_IQ2_XS = 17,
  AIUEOS_GGML_IQ3_XXS = 18,
  AIUEOS_GGML_IQ1_S = 19,
  AIUEOS_GGML_IQ3_S = 21,
  AIUEOS_GGML_IQ2_S = 22,
  AIUEOS_GGML_IQ4_XS = 23,
  AIUEOS_GGML_IQ1_M = 29
};

struct aiueos_qwen35_tensor {
  uint64_t dimensions[4];
  uint64_t offset;
  uint64_t storage_bytes;
  uint32_t dimension_count;
  uint32_t type;
  const uint8_t *data;
};

struct aiueos_qwen35_linear_tensors {
  struct aiueos_qwen35_tensor gate;
  struct aiueos_qwen35_tensor qkv;
  struct aiueos_qwen35_tensor a;
  struct aiueos_qwen35_tensor alpha;
  struct aiueos_qwen35_tensor beta;
  struct aiueos_qwen35_tensor conv1d;
  struct aiueos_qwen35_tensor dt_bias;
  struct aiueos_qwen35_tensor norm;
  struct aiueos_qwen35_tensor output;
};

struct aiueos_qwen35_attention_tensors {
  struct aiueos_qwen35_tensor key;
  struct aiueos_qwen35_tensor key_norm;
  struct aiueos_qwen35_tensor output;
  struct aiueos_qwen35_tensor query_gate;
  struct aiueos_qwen35_tensor query_norm;
  struct aiueos_qwen35_tensor value;
};

struct aiueos_qwen35_nextn_tensors {
  struct aiueos_qwen35_tensor eh_projection;
  struct aiueos_qwen35_tensor embedding_norm;
  struct aiueos_qwen35_tensor hidden_norm;
  struct aiueos_qwen35_tensor shared_head_norm;
};

struct aiueos_qwen35_layer {
  uint32_t linear_attention;
  struct aiueos_qwen35_tensor attention_norm;
  struct aiueos_qwen35_tensor post_attention_norm;
  struct aiueos_qwen35_tensor ffn_down;
  struct aiueos_qwen35_tensor ffn_gate;
  struct aiueos_qwen35_tensor ffn_up;
  union {
    struct aiueos_qwen35_linear_tensors linear;
    struct aiueos_qwen35_attention_tensors full;
  } mixer;
  struct aiueos_qwen35_nextn_tensors nextn;
};

struct aiueos_qwen35_model {
  const uint8_t *bytes;
  uint64_t accessible_bytes;
  uint64_t artifact_bytes;
  uint64_t metadata_end;
  uint64_t tensor_info_end;
  uint64_t data_offset;
  uint64_t tensor_count;
  uint64_t metadata_count;
  uint32_t block_count;
  uint32_t trunk_layer_count;
  uint32_t context_length;
  uint32_t embedding_length;
  uint32_t feed_forward_length;
  uint32_t vocab_size;
  uint32_t attention_head_count;
  uint32_t attention_kv_head_count;
  uint32_t attention_key_length;
  uint32_t attention_value_length;
  uint32_t linear_key_head_count;
  uint32_t linear_value_head_count;
  uint32_t linear_state_size;
  uint32_t linear_inner_size;
  uint32_t linear_conv_kernel;
  uint32_t full_attention_interval;
  uint32_t rope_dimension_count;
  uint32_t rope_sections[4];
  uint32_t nextn_layer_count;
  uint32_t linear_layer_count;
  uint32_t full_layer_count;
  uint32_t ggml_type_counts[AIUEOS_QWEN35_MAX_GGML_TYPE];
  struct aiueos_qwen35_tensor token_embedding;
  struct aiueos_qwen35_tensor output_norm;
  struct aiueos_qwen35_tensor output;
  struct aiueos_qwen35_layer layers[AIUEOS_QWEN35_LAYER_COUNT];
};

/* Parse only the GGUF metadata and tensor table. accessible_bytes may be the
   10.9 MiB prefix in a host gate; artifact_bytes is always the complete file
   length against which every tensor extent is checked. */
int aiueos_qwen35_model_parse(const uint8_t *bytes,
                              uint64_t accessible_bytes,
                              uint64_t artifact_bytes,
                              struct aiueos_qwen35_model *model);

/* Bind tensor pointers only when the complete, already-admitted artifact is
   mapped. Parsing a prefix never fabricates pointers into inaccessible bytes. */
int aiueos_qwen35_model_bind(struct aiueos_qwen35_model *model,
                             const uint8_t *bytes,
                             uint64_t accessible_bytes);

#endif
