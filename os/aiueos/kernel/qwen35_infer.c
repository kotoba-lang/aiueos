/* SPDX-License-Identifier: Apache-2.0 */
#include "qwen35_infer.h"
#include "qwen35_quant.h"

#include <stdint.h>

#ifdef AIUEOS_QWEN35_SMP
#include "smp.h"
#endif

#define EMBED 5120U
#define FFN 17408U
#define LINEAR_QKV 10240U
#define LINEAR_INNER 6144U
#define FULL_QG 12288U
#define FULL_VALUE 1024U
#define HEAD_DIM 256U
#define LINEAR_HEAD_DIM 128U
#define EPSILON 0.000001f

struct qwen35_workspace {
  float state[EMBED];
  float normalized[EMBED];
  float scratch_a[FFN];
  float scratch_b[FFN];
  float scratch_c[FULL_QG];
  float dequantized[FFN];
  float ap_dequantized[FFN];
  float beta_values[48];
};

typedef char qwen35_workspace_size[
    (sizeof(struct qwen35_workspace) == AIUEOS_QWEN35_WORKSPACE_BYTES) ? 1 : -1];

static float *state;
static float *normalized;
static float *scratch_a;
static float *scratch_b;
static float *scratch_c;
static float *dequantized;
static float *ap_dequantized;
static float *beta_values;
static uint32_t qwen_vector_bits;
static uint32_t qwen_worker_threads = 1U;
static uint32_t qwen_force_scalar;

void aiueos_qwen35_force_scalar(void) {
  qwen_force_scalar = 1U;
  qwen_vector_bits = 0U;
}

static float local_sqrt(float value) {
#if defined(__x86_64__)
  float result;
  __asm__ volatile("sqrtss %1, %0" : "=x"(result) : "x"(value));
  return result;
#else
  return __builtin_sqrtf(value);
#endif
}

static float local_exp(float value) {
  if (value <= -87.0f) return 0.0f;
  if (value >= 88.0f) value = 88.0f;
  float scaled = value * 1.4426950408889634f;
  int32_t exponent = scaled >= 0.0f ? (int32_t)(scaled + 0.5f)
                                     : (int32_t)(scaled - 0.5f);
  float remainder = value - (float)exponent * 0.6931471805599453f;
  float square = remainder * remainder;
  float polynomial =
      1.0f + remainder + square *
      (0.5f + remainder *
      (0.1666666716f + remainder *
      (0.0416666679f + remainder *
      (0.0083333310f + remainder * 0.0013888949f))));
  union { uint32_t bits; float value; } power = {
      (uint32_t)(exponent + 127) << 23
  };
  return polynomial * power.value;
}

static float sigmoid(float value) {
  if (value >= 0.0f) {
    float e = local_exp(-value);
    return 1.0f / (1.0f + e);
  }
  float e = local_exp(value);
  return e / (1.0f + e);
}

static float silu(float value) {
  return value * sigmoid(value);
}

static uint64_t read_cycles(void) {
#if defined(__x86_64__)
  uint32_t low, high;
  __asm__ volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
  return ((uint64_t)high << 32) | low;
#else
  return 0;
#endif
}

static float dot_scalar(const float * left, const float * right,
                        uint64_t count) {
  float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f, sum3 = 0.0f;
  uint64_t index = 0;
  for (; index + 4 <= count; index += 4) {
    sum0 += left[index + 0] * right[index + 0];
    sum1 += left[index + 1] * right[index + 1];
    sum2 += left[index + 2] * right[index + 2];
    sum3 += left[index + 3] * right[index + 3];
  }
  float sum = (sum0 + sum1) + (sum2 + sum3);
  for (; index < count; index++) sum += left[index] * right[index];
  return sum;
}

#if defined(__x86_64__) && !defined(AIUEOS_QWEN35_SCALAR)
typedef float qwen_v8f __attribute__((vector_size(32), aligned(1)));
typedef float qwen_v4f __attribute__((vector_size(16), aligned(1)));

static void prepare_bsp_extended_state(void) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(1U), "c"(0U));
  if ((ecx & ((1U << 26) | (1U << 28))) !=
      ((1U << 26) | (1U << 28))) return;
  uintptr_t cr4;
  __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
  cr4 |= (1U << 9) | (1U << 10) | (1U << 18);
  __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
  uint32_t xcr0_low, xcr0_high;
  __asm__ volatile("xgetbv"
                   : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0U));
  xcr0_low |= 0x6U;
  __asm__ volatile("xsetbv"
                   : : "a"(xcr0_low), "d"(xcr0_high), "c"(0U));
}

__attribute__((target("avx2")))
static float dot_avx2(const float *left, const float *right, uint64_t count) {
  qwen_v4f sums = {0.0f, 0.0f, 0.0f, 0.0f};
  uint64_t index = 0;
  for (; index + 8U <= count; index += 8U) {
    qwen_v8f left8 = *(const qwen_v8f *)(const void *)(left + index);
    qwen_v8f right8 = *(const qwen_v8f *)(const void *)(right + index);
    qwen_v8f product = left8 * right8;
    qwen_v4f lower = __builtin_shufflevector(product, product, 0, 1, 2, 3);
    qwen_v4f upper = __builtin_shufflevector(product, product, 4, 5, 6, 7);
    /* Preserve the scalar implementation's four-accumulator operation order.
       Each SIMD lane is one original accumulator, and lower is added before
       upper just like two consecutive four-element scalar iterations. */
    sums += lower;
    sums += upper;
  }
  union { qwen_v4f vector; float lane[4]; } reduced = {sums};
  /* Four volatile updates prevent the optimizer from replacing the contract's
     left-to-right final reduction with a different horizontal tree. */
  volatile float ordered = reduced.lane[0];
  ordered += reduced.lane[1];
  ordered += reduced.lane[2];
  ordered += reduced.lane[3];
  float sum = ordered;
  for (; index < count; index++) sum += left[index] * right[index];
  return sum;
}

static int cpu_has_avx2(void) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0U), "c"(0U));
  if (eax < 7U) return 0;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(1U), "c"(0U));
  if ((ecx & ((1U << 27) | (1U << 28))) !=
      ((1U << 27) | (1U << 28))) return 0;
  uint32_t xcr0_low, xcr0_high;
  __asm__ volatile("xgetbv"
                   : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0U));
  if ((xcr0_low & 0x6U) != 0x6U) return 0;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(7U), "c"(0U));
  return (ebx & (1U << 5)) != 0;
}
#endif

static float dot(const float *left, const float *right, uint64_t count) {
#if defined(__x86_64__) && !defined(AIUEOS_QWEN35_SCALAR)
  if (qwen_vector_bits == 256U) return dot_avx2(left, right, count);
#endif
  return dot_scalar(left, right, count);
}

static int tensor_row(const struct aiueos_qwen35_tensor *tensor,
                      uint64_t row, float *output) {
  if (!tensor || !tensor->data || tensor->dimension_count != 2 ||
      row >= tensor->dimensions[1] || tensor->dimensions[0] > FFN) return 0;
  uint64_t row_bytes =
      aiueos_qwen35_quant_row_bytes(tensor->type, tensor->dimensions[0]);
  if (!row_bytes || row > UINT64_MAX / row_bytes ||
      row * row_bytes > tensor->storage_bytes ||
      row_bytes > tensor->storage_bytes - row * row_bytes) return 0;
  return aiueos_qwen35_dequantize_row(
      tensor->type, tensor->data + row * row_bytes,
      tensor->dimensions[0], output);
}

static int matvec_range(const struct aiueos_qwen35_tensor *tensor,
                        const float *input, uint64_t input_count,
                        float *output, uint64_t first, uint64_t end,
                        float *row_values) {
  for (uint64_t row = first; row < end; row++) {
    if (!tensor_row(tensor, row, row_values)) return 0;
    output[row] = dot(row_values, input, input_count);
  }
  return 1;
}

#ifdef AIUEOS_QWEN35_SMP
struct matvec_ap_task {
  const struct aiueos_qwen35_tensor *tensor;
  const float *input;
  uint64_t input_count;
  float *output;
  uint64_t first;
  uint64_t end;
  int ok;
};

static void matvec_ap(void *opaque) {
  struct matvec_ap_task *task = opaque;
  task->ok = matvec_range(task->tensor, task->input, task->input_count,
                          task->output, task->first, task->end,
                          ap_dequantized);
}
#endif

static int matvec(const struct aiueos_qwen35_tensor *tensor,
                  const float *input, uint64_t input_count,
                  float *output, uint64_t output_count) {
  if (!tensor || tensor->dimension_count != 2 ||
      tensor->dimensions[0] != input_count ||
      tensor->dimensions[1] != output_count) return 0;
#ifdef AIUEOS_QWEN35_SMP
  if (qwen_worker_threads == 2U && output_count >= 512U) {
    uint64_t split = output_count / 2U;
    struct matvec_ap_task task = {
      .tensor = tensor, .input = input, .input_count = input_count,
      .output = output, .first = split, .end = output_count, .ok = 0
    };
    if (aiueos_smp_dispatch(matvec_ap, &task)) {
      int bsp_ok = matvec_range(tensor, input, input_count, output, 0, split,
                                dequantized);
      int ap_ok = aiueos_smp_join() && task.ok;
      return bsp_ok && ap_ok;
    }
  }
#endif
  return matvec_range(tensor, input, input_count, output, 0, output_count,
                      dequantized);
}

static int rms_norm(const float *input,
                    const struct aiueos_qwen35_tensor *weights,
                    uint64_t count, float *output) {
  if (!weights || !weights->data || weights->type != AIUEOS_GGML_F32 ||
      weights->dimension_count != 1 || weights->dimensions[0] != count ||
      weights->storage_bytes != count * sizeof(float)) return 0;
  double sum = 0.0;
  for (uint64_t index = 0; index < count; index++)
    sum += (double)(input[index] * input[index]);
  float mean = (float)(sum / (double)count);
  float scale = 1.0f / local_sqrt(mean + EPSILON);
  const float *weight = (const float *)(const void *)weights->data;
  for (uint64_t index = 0; index < count; index++)
    output[index] = input[index] * scale * weight[index];
  return 1;
}

static void l2_norm_heads(float *values, uint32_t heads, uint32_t width) {
  for (uint32_t head = 0; head < heads; head++) {
    float *vector = values + head * width;
    double sum = 0.0;
    for (uint32_t index = 0; index < width; index++)
      sum += (double)(vector[index] * vector[index]);
    float length = local_sqrt((float)sum);
    float scale = 1.0f / (length > EPSILON ? length : EPSILON);
    for (uint32_t index = 0; index < width; index++) vector[index] *= scale;
  }
}

static int ffn(const struct aiueos_qwen35_layer *layer) {
  if (!rms_norm(state, &layer->post_attention_norm, EMBED, normalized) ||
      !matvec(&layer->ffn_gate, normalized, EMBED, scratch_a, FFN) ||
      !matvec(&layer->ffn_up, normalized, EMBED, scratch_b, FFN))
    return 0;
  for (uint32_t index = 0; index < FFN; index++)
    scratch_a[index] = silu(scratch_a[index]) * scratch_b[index];
  if (!matvec(&layer->ffn_down, scratch_a, FFN, scratch_b, EMBED)) return 0;
  for (uint32_t index = 0; index < EMBED; index++) state[index] += scratch_b[index];
  return 1;
}

static int linear_attention(const struct aiueos_qwen35_layer *layer) {
  const struct aiueos_qwen35_linear_tensors *linear = &layer->mixer.linear;
  if (!rms_norm(state, &layer->attention_norm, EMBED, normalized) ||
      !matvec(&linear->qkv, normalized, EMBED, scratch_a, LINEAR_QKV) ||
      !matvec(&linear->gate, normalized, EMBED, scratch_b, LINEAR_INNER) ||
      !matvec(&linear->beta, normalized, EMBED, beta_values, 48))
    return 0;

  if (!linear->conv1d.data || linear->conv1d.type != AIUEOS_GGML_F32 ||
      linear->conv1d.dimension_count != 2 ||
      linear->conv1d.dimensions[0] != 4 ||
      linear->conv1d.dimensions[1] != LINEAR_QKV) return 0;
  const float *kernel = (const float *)(const void *)linear->conv1d.data;
  for (uint32_t channel = 0; channel < LINEAR_QKV; channel++)
    scratch_a[channel] = silu(scratch_a[channel] * kernel[channel * 4U + 3U]);

  l2_norm_heads(scratch_a, 16, LINEAR_HEAD_DIM);
  l2_norm_heads(scratch_a + 2048, 16, LINEAR_HEAD_DIM);

  for (uint32_t head = 0; head < 48; head++) {
    uint32_t key_head = head % 16U;
    float coefficient =
        dot(scratch_a + key_head * LINEAR_HEAD_DIM,
            scratch_a + 2048U + key_head * LINEAR_HEAD_DIM,
            LINEAR_HEAD_DIM) / local_sqrt((float)LINEAR_HEAD_DIM);
    coefficient *= sigmoid(beta_values[head]);
    for (uint32_t index = 0; index < LINEAR_HEAD_DIM; index++)
      scratch_c[head * LINEAR_HEAD_DIM + index] =
          scratch_a[4096U + head * LINEAR_HEAD_DIM + index] * coefficient;
  }

  if (!linear->norm.data || linear->norm.type != AIUEOS_GGML_F32 ||
      linear->norm.dimension_count != 1 ||
      linear->norm.dimensions[0] != LINEAR_HEAD_DIM) return 0;
  const float *weights = (const float *)(const void *)linear->norm.data;
  for (uint32_t head = 0; head < 48; head++) {
    float *vector = scratch_c + head * LINEAR_HEAD_DIM;
    double sum = 0.0;
    for (uint32_t index = 0; index < LINEAR_HEAD_DIM; index++)
      sum += (double)(vector[index] * vector[index]);
    float scale = 1.0f /
        local_sqrt((float)(sum / (double)LINEAR_HEAD_DIM) + EPSILON);
    for (uint32_t index = 0; index < LINEAR_HEAD_DIM; index++) {
      uint32_t position = head * LINEAR_HEAD_DIM + index;
      vector[index] = vector[index] * scale * weights[index] *
                      silu(scratch_b[position]);
    }
  }

  if (!matvec(&linear->output, scratch_c, LINEAR_INNER, normalized, EMBED))
    return 0;
  for (uint32_t index = 0; index < EMBED; index++) state[index] += normalized[index];
  return 1;
}

static int full_attention(const struct aiueos_qwen35_layer *layer) {
  const struct aiueos_qwen35_attention_tensors *full = &layer->mixer.full;
  if (!rms_norm(state, &layer->attention_norm, EMBED, normalized) ||
      !matvec(&full->query_gate, normalized, EMBED, scratch_a, FULL_QG) ||
      !matvec(&full->value, normalized, EMBED, scratch_b, FULL_VALUE))
    return 0;

  for (uint32_t head = 0; head < 24; head++) {
    uint32_t value_head = head / 6U;
    for (uint32_t index = 0; index < HEAD_DIM; index++) {
      float gate = scratch_a[head * HEAD_DIM * 2U + HEAD_DIM + index];
      scratch_c[head * HEAD_DIM + index] =
          scratch_b[value_head * HEAD_DIM + index] * sigmoid(gate);
    }
  }
  if (!matvec(&full->output, scratch_c, LINEAR_INNER, normalized, EMBED))
    return 0;
  for (uint32_t index = 0; index < EMBED; index++) state[index] += normalized[index];
  return 1;
}

int aiueos_qwen35_first_token(
    const struct aiueos_qwen35_model *model,
    uint32_t input_token,
    void *workspace,
    uint64_t workspace_bytes,
    aiueos_qwen35_progress_fn progress,
    struct aiueos_qwen35_first_token_result *result) {
  if (!model || !result || !workspace ||
      ((uintptr_t)workspace & 15U) ||
      workspace_bytes < sizeof(struct qwen35_workspace) ||
      model->accessible_bytes != model->artifact_bytes ||
      input_token >= model->vocab_size ||
      model->artifact_bytes != AIUEOS_QWEN35_ARTIFACT_BYTES)
    return 0;
  struct qwen35_workspace *memory = (struct qwen35_workspace *)workspace;
  state = memory->state;
  normalized = memory->normalized;
  scratch_a = memory->scratch_a;
  scratch_b = memory->scratch_b;
  scratch_c = memory->scratch_c;
  dequantized = memory->dequantized;
  ap_dequantized = memory->ap_dequantized;
  beta_values = memory->beta_values;
#if defined(__x86_64__) && !defined(AIUEOS_QWEN35_SCALAR)
  prepare_bsp_extended_state();
  qwen_vector_bits = !qwen_force_scalar && cpu_has_avx2() ? 256U : 0U;
#else
  qwen_vector_bits = 0U;
#endif
#ifdef AIUEOS_QWEN35_SMP
  qwen_worker_threads = aiueos_smp_worker_threads();
#else
  qwen_worker_threads = 1U;
#endif
  if (!tensor_row(&model->token_embedding, input_token, state)) return 0;

  uint64_t started = read_cycles();
  for (uint32_t index = 0; index < AIUEOS_QWEN35_TRUNK_LAYER_COUNT; index++) {
    const struct aiueos_qwen35_layer *layer = &model->layers[index];
    int ok = layer->linear_attention ? linear_attention(layer)
                                     : full_attention(layer);
    if (!ok || !ffn(layer)) return 0;
    if (progress) progress(index + 1U, AIUEOS_QWEN35_TRUNK_LAYER_COUNT, 0);
  }

  if (!rms_norm(state, &model->output_norm, EMBED, normalized)) return 0;
  if (progress) progress(64, 64, 1);

  result->token = UINT32_MAX;
  result->second_token = UINT32_MAX;
  result->logit = -3.402823466e+38f;
  result->second_logit = -3.402823466e+38f;
  result->vector_bits = qwen_vector_bits;
  result->worker_threads = qwen_worker_threads;
  for (uint32_t token = 0; token < model->vocab_size; token++) {
    if (!tensor_row(&model->output, token, dequantized)) return 0;
    float logit = dot(dequantized, normalized, EMBED);
    if (logit > result->logit) {
      result->second_logit = result->logit;
      result->second_token = result->token;
      result->logit = logit;
      result->token = token;
    } else if (logit > result->second_logit) {
      result->second_logit = logit;
      result->second_token = token;
    }
  }
  uint64_t finished = read_cycles();
  result->compute_cycles = finished >= started ? finished - started : 0;
  return result->token != UINT32_MAX && result->second_token != UINT32_MAX;
}
