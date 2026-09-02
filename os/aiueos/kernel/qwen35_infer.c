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
#define LINEAR_KEY_HEADS 16U
#define LINEAR_VALUE_HEADS 48U
#define LINEAR_KV_GROUP_SIZE (LINEAR_VALUE_HEADS / LINEAR_KEY_HEADS)
#define EPSILON 0.000001f
#define LINEAR_CONV_HISTORY 3U
#define FULL_KV_WIDTH 1024U
#define FULL_GATE_TEMP_OFFSET FULL_VALUE
#define FULL_KEY_TEMP_OFFSET (FULL_GATE_TEMP_OFFSET + HEAD_DIM)
#define ROPE_DIM 64U
#define ROPE_HALF 32U
#define ROPE_LOG_THETA 16.11809565095832f
#define INV_SQRT_HEAD_DIM 0.0625f
#define INV_SQRT_LINEAR_HEAD_DIM 0.08838834764831845f

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

struct qwen35_decode_context {
  float *recurrent;
  float *conv;
  float *full_key;
  float *full_value;
  float *full_key_shadow;
  uint64_t *full_key_hash;
  uint32_t position;
};

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
static uint32_t qwen_failure_stage;

const char *aiueos_qwen35_failure_stage_label(uint32_t stage) {
  switch (stage) {
    case AIUEOS_QWEN35_FAILURE_EMBEDDING: return "EMBEDDING";
    case AIUEOS_QWEN35_FAILURE_ATTENTION_PROJECTION: return "ATTN PROJ";
    case AIUEOS_QWEN35_FAILURE_LINEAR_ALPHA: return "ALPHA";
    case AIUEOS_QWEN35_FAILURE_LINEAR_CONV: return "CONV";
    case AIUEOS_QWEN35_FAILURE_LINEAR_DECAY: return "DECAY";
    case AIUEOS_QWEN35_FAILURE_LINEAR_RECURRENT: return "RECURRENT";
    case AIUEOS_QWEN35_FAILURE_LINEAR_OUTPUT: return "LINEAR OUT";
    case AIUEOS_QWEN35_FAILURE_FULL_KEY: return "FULL KEY";
    case AIUEOS_QWEN35_FAILURE_FULL_SOFTMAX: return "SOFTMAX";
    case AIUEOS_QWEN35_FAILURE_FULL_OUTPUT: return "FULL OUT";
    case AIUEOS_QWEN35_FAILURE_FFN: return "FFN";
    case AIUEOS_QWEN35_FAILURE_STATE_NONFINITE: return "STATE NAN";
    case AIUEOS_QWEN35_FAILURE_OUTPUT_NORM: return "OUTPUT NORM";
    case AIUEOS_QWEN35_FAILURE_OUTPUT_LOGITS: return "LOGITS NAN";
    case AIUEOS_QWEN35_FAILURE_FULL_QUERY: return "FULL QUERY";
    case AIUEOS_QWEN35_FAILURE_FULL_CACHE: return "KV CACHE";
    default: return "UNKNOWN";
  }
}

static int finite_float(float value) {
  union { float value; uint32_t bits; } representation = {value};
  return (representation.bits & 0x7f800000U) != 0x7f800000U;
}

static int finite_values(const float *values, uint64_t count) {
  for (uint64_t index = 0; index < count; index++)
    if (!finite_float(values[index])) return 0;
  return 1;
}

static uint64_t float_values_hash(const float *values, uint64_t count) {
  uint64_t hash = 14695981039346656037ULL;
  for (uint64_t index = 0; index < count; index++) {
    union { float value; uint32_t bits; } representation = {values[index]};
    for (uint32_t byte = 0; byte < 4U; byte++) {
      hash ^= (representation.bits >> (byte * 8U)) & 0xffU;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

static int stable_attention_score(const float *query, const float *key,
                                  uint32_t count, double *score) {
  if (!score || !finite_values(query, count) || !finite_values(key, count))
    return 0;
  double sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
  uint32_t index = 0;
  for (; index + 4U <= count; index += 4U) {
    sum0 += (double)query[index + 0U] * (double)key[index + 0U];
    sum1 += (double)query[index + 1U] * (double)key[index + 1U];
    sum2 += (double)query[index + 2U] * (double)key[index + 2U];
    sum3 += (double)query[index + 3U] * (double)key[index + 3U];
  }
  double sum = (sum0 + sum1) + (sum2 + sum3);
  for (; index < count; index++)
    sum += (double)query[index] * (double)key[index];
  *score = sum * (double)INV_SQRT_HEAD_DIM;
  return *score == *score && *score <= 1.7976931348623157e+308 &&
         *score >= -1.7976931348623157e+308;
}

static int fail_at(uint32_t stage) {
  if (qwen_failure_stage == AIUEOS_QWEN35_FAILURE_NONE)
    qwen_failure_stage = stage;
  return 0;
}

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

static float local_log1p(float value) {
  /* log(1+x) = 2 * atanh(x/(2+x)); for x in [0,1] the transformed
     argument is at most 1/3 and this odd series is float-accurate. */
  float z = value / (2.0f + value);
  float z2 = z * z;
  float term = z;
  float sum = term;
  term *= z2; sum += term * (1.0f / 3.0f);
  term *= z2; sum += term * (1.0f / 5.0f);
  term *= z2; sum += term * (1.0f / 7.0f);
  term *= z2; sum += term * (1.0f / 9.0f);
  term *= z2; sum += term * (1.0f / 11.0f);
  term *= z2; sum += term * (1.0f / 13.0f);
  return 2.0f * sum;
}

static float softplus(float value) {
  if (value >= 20.0f) return value;
  if (value <= -20.0f) return local_exp(value);
  if (value >= 0.0f) return value + local_log1p(local_exp(-value));
  return local_log1p(local_exp(value));
}

static void local_sincos(float value, float *sine, float *cosine) {
#if defined(__x86_64__)
  float s, c;
  __asm__ volatile("fsincos" : "=t"(c), "=u"(s) : "0"(value));
  *sine = s;
  *cosine = c;
#else
  *sine = __builtin_sinf(value);
  *cosine = __builtin_cosf(value);
#endif
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

static const float *f32_vector(const struct aiueos_qwen35_tensor *tensor,
                               uint64_t count) {
  if (!tensor || !tensor->data || tensor->type != AIUEOS_GGML_F32 ||
      tensor->dimension_count != 1 || tensor->dimensions[0] != count ||
      tensor->storage_bytes != count * sizeof(float)) return 0;
  return (const float *)(const void *)tensor->data;
}

static int rms_norm_heads_weighted(float *values, uint32_t heads,
                                   uint32_t width,
                                   const struct aiueos_qwen35_tensor *tensor) {
  const float *weights = f32_vector(tensor, width);
  if (!weights) return 0;
  for (uint32_t head = 0; head < heads; head++) {
    float *vector = values + head * width;
    double sum = 0.0;
    int ordinary = 1;
    float maximum = 0.0f;
    for (uint32_t index = 0; index < width; index++) {
      if (!finite_float(vector[index])) return 0;
      float magnitude = vector[index] < 0.0f ? -vector[index] : vector[index];
      if (magnitude > maximum) maximum = magnitude;
      float square = vector[index] * vector[index];
      if (!finite_float(square)) ordinary = 0;
      else sum += (double)square;
    }
    float scale;
    if (ordinary) {
      scale = 1.0f /
        local_sqrt((float)(sum / (double)width) + EPSILON);
    } else {
      double scaled_sum = 0.0;
      for (uint32_t index = 0; index < width; index++) {
        double scaled = (double)vector[index] / (double)maximum;
        scaled_sum += scaled * scaled;
      }
      double epsilon_scaled =
        (double)EPSILON / ((double)maximum * (double)maximum);
      scale = (1.0f / maximum) /
        local_sqrt((float)(scaled_sum / (double)width + epsilon_scaled));
    }
    if (!finite_float(scale)) return 0;
    for (uint32_t index = 0; index < width; index++) {
      vector[index] = vector[index] * scale * weights[index];
      if (!finite_float(vector[index])) return 0;
    }
  }
  return 1;
}

static void l2_norm_heads(float *values, uint32_t heads, uint32_t width) {
  for (uint32_t head = 0; head < heads; head++) {
    float *vector = values + head * width;
    double sum = 0.0;
    for (uint32_t index = 0; index < width; index++)
      sum += (double)(vector[index] * vector[index]);
    float scale = 1.0f / local_sqrt((float)sum + EPSILON);
    for (uint32_t index = 0; index < width; index++) vector[index] *= scale;
  }
}

static void rope_heads(float *values, uint32_t heads, uint32_t position) {
  if (!position) return;
  for (uint32_t pair = 0; pair < ROPE_HALF; pair++) {
    float frequency = local_exp(
      -ROPE_LOG_THETA * (float)pair / (float)ROPE_HALF);
    float sine, cosine;
    local_sincos((float)position * frequency, &sine, &cosine);
    for (uint32_t head = 0; head < heads; head++) {
      float *vector = values + head * HEAD_DIM;
      float first = vector[pair];
      float second = vector[pair + ROPE_HALF];
      vector[pair] = first * cosine - second * sine;
      vector[pair + ROPE_HALF] = second * cosine + first * sine;
    }
  }
}

static void recurrent_step(float *head_state,
                           const float *key,
                           const float *query,
                           const float *value,
                           float decay,
                           float beta,
                           float *correction,
                           float *output) {
  for (uint32_t value_index = 0; value_index < LINEAR_HEAD_DIM;
       value_index++) {
    float remembered = 0.0f;
    for (uint32_t key_index = 0; key_index < LINEAR_HEAD_DIM; key_index++) {
      float *cell = head_state +
        (uint64_t)key_index * LINEAR_HEAD_DIM + value_index;
      *cell *= decay;
      remembered += *cell * key[key_index];
    }
    correction[value_index] = (value[value_index] - remembered) * beta;
  }
  for (uint32_t key_index = 0; key_index < LINEAR_HEAD_DIM; key_index++) {
    float k = key[key_index];
    float *row = head_state + (uint64_t)key_index * LINEAR_HEAD_DIM;
    for (uint32_t value_index = 0; value_index < LINEAR_HEAD_DIM;
         value_index++)
      row[value_index] += k * correction[value_index];
  }
  for (uint32_t value_index = 0; value_index < LINEAR_HEAD_DIM;
       value_index++) {
    float value_out = 0.0f;
    for (uint32_t key_index = 0; key_index < LINEAR_HEAD_DIM; key_index++)
      value_out += head_state[
        (uint64_t)key_index * LINEAR_HEAD_DIM + value_index] *
        query[key_index];
    output[value_index] = value_out * INV_SQRT_LINEAR_HEAD_DIM;
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

static const float *resolved_cached_key(
    struct qwen35_decode_context *decode, uint64_t cache_entry,
    uint32_t kv_head) {
  if (!decode || kv_head >= 4U) return 0;
  float *primary = decode->full_key + cache_entry * FULL_KV_WIDTH;
  uint64_t expected_hash = decode->full_key_hash[cache_entry];
  if (!expected_hash || !finite_values(primary, FULL_KV_WIDTH) ||
      float_values_hash(primary, FULL_KV_WIDTH) != expected_hash) {
    const float *shadow =
      decode->full_key_shadow + cache_entry * FULL_KV_WIDTH;
    if (!finite_values(shadow, FULL_KV_WIDTH) ||
        float_values_hash(shadow, FULL_KV_WIDTH) != expected_hash)
      return 0;
    for (uint32_t index = 0; index < FULL_KV_WIDTH; index++)
      primary[index] = shadow[index];
  }
  return primary + kv_head * HEAD_DIM;
}

static int linear_attention(const struct aiueos_qwen35_layer *layer,
                            struct qwen35_decode_context *decode,
                            uint32_t linear_slot) {
  const struct aiueos_qwen35_linear_tensors *linear = &layer->mixer.linear;
  if (!rms_norm(state, &layer->attention_norm, EMBED, normalized) ||
      !matvec(&linear->qkv, normalized, EMBED, scratch_a, LINEAR_QKV) ||
      !matvec(&linear->gate, normalized, EMBED, scratch_b, LINEAR_INNER) ||
      !matvec(&linear->beta, normalized, EMBED, beta_values, 48))
    return fail_at(AIUEOS_QWEN35_FAILURE_ATTENTION_PROJECTION);

  if (decode && decode->position &&
      !matvec(&linear->alpha, normalized, EMBED, ap_dequantized, 48))
    return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_ALPHA);

  if (!linear->conv1d.data || linear->conv1d.type != AIUEOS_GGML_F32 ||
      linear->conv1d.dimension_count != 2 ||
      linear->conv1d.dimensions[0] != 4 ||
      linear->conv1d.dimensions[1] != LINEAR_QKV)
    return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_CONV);
  const float *kernel = (const float *)(const void *)linear->conv1d.data;
  float *conv = decode ? decode->conv +
    (uint64_t)linear_slot * LINEAR_QKV * LINEAR_CONV_HISTORY : 0;
  for (uint32_t channel = 0; channel < LINEAR_QKV; channel++) {
    float current = scratch_a[channel];
    float mixed = current * kernel[channel * 4U + 3U];
    if (conv) {
      float *history = conv + (uint64_t)channel * LINEAR_CONV_HISTORY;
      if (decode->position)
        mixed += history[0] * kernel[channel * 4U + 0U] +
                 history[1] * kernel[channel * 4U + 1U] +
                 history[2] * kernel[channel * 4U + 2U];
      history[0] = history[1];
      history[1] = history[2];
      history[2] = current;
    }
    scratch_a[channel] = silu(mixed);
  }

  l2_norm_heads(scratch_a, 16, LINEAR_HEAD_DIM);
  l2_norm_heads(scratch_a + 2048, 16, LINEAR_HEAD_DIM);

  if (!decode) {
    for (uint32_t head = 0; head < 48; head++) {
      uint32_t key_head = head / LINEAR_KV_GROUP_SIZE;
      float coefficient =
          dot(scratch_a + key_head * LINEAR_HEAD_DIM,
              scratch_a + 2048U + key_head * LINEAR_HEAD_DIM,
              LINEAR_HEAD_DIM) * INV_SQRT_LINEAR_HEAD_DIM;
      coefficient *= sigmoid(beta_values[head]);
      for (uint32_t index = 0; index < LINEAR_HEAD_DIM; index++)
        scratch_c[head * LINEAR_HEAD_DIM + index] =
            scratch_a[4096U + head * LINEAR_HEAD_DIM + index] * coefficient;
    }
  } else {
    const float *a = f32_vector(&linear->a, 48);
    const float *dt = f32_vector(&linear->dt_bias, 48);
    if (!a || !dt) return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_DECAY);
    float *layer_state = decode->recurrent +
      (uint64_t)linear_slot * 48U * LINEAR_HEAD_DIM * LINEAR_HEAD_DIM;
    for (uint32_t head = 0; head < 48; head++) {
      uint32_t key_head = head / LINEAR_KV_GROUP_SIZE;
      const float *query = scratch_a + key_head * LINEAR_HEAD_DIM;
      const float *key = scratch_a + 2048U + key_head * LINEAR_HEAD_DIM;
      const float *value = scratch_a + 4096U + head * LINEAR_HEAD_DIM;
      float *head_state = layer_state +
        (uint64_t)head * LINEAR_HEAD_DIM * LINEAR_HEAD_DIM;
      float transition = decode->position ?
        a[head] * softplus(ap_dequantized[head] + dt[head]) : 0.0f;
      if (!finite_float(transition))
        return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_DECAY);
      float decay = decode->position ? local_exp(transition) : 1.0f;
      float beta = sigmoid(beta_values[head]);
      if (!finite_float(decay) || !finite_float(beta))
        return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_DECAY);

      float *output = scratch_c + head * LINEAR_HEAD_DIM;
      /* S <- decay*S; delta <- beta*(v-k^T S); S <- S+k*delta;
         y <- (q/sqrt(d))^T S.  Rows are key dimension, columns value. */
      recurrent_step(head_state, key, query, value, decay, beta,
                     dequantized, output);
      if (!finite_values(output, LINEAR_HEAD_DIM))
        return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_RECURRENT);
      if (!decode->position) {
        /* With an all-zero recurrent state the official delta rule reduces
           exactly to v * beta * dot(q, k) / sqrt(d).  Keep the state written
           by recurrent_step, but preserve the already physically-qualified
           position-zero reduction order for the emitted activation.  The two
           forms are algebraically identical; fixing the association here
           prevents an IQ3 argmax from changing solely because the cache path
           introduced a different float accumulation order. */
        float coefficient = dot(query, key, LINEAR_HEAD_DIM) *
                            INV_SQRT_LINEAR_HEAD_DIM * beta;
        for (uint32_t index = 0; index < LINEAR_HEAD_DIM; index++)
          output[index] = value[index] * coefficient;
      }
    }
  }

  if (!linear->norm.data || linear->norm.type != AIUEOS_GGML_F32 ||
      linear->norm.dimension_count != 1 ||
      linear->norm.dimensions[0] != LINEAR_HEAD_DIM)
    return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_OUTPUT);
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
    return fail_at(AIUEOS_QWEN35_FAILURE_LINEAR_OUTPUT);
  for (uint32_t index = 0; index < EMBED; index++) state[index] += normalized[index];
  return 1;
}

static int full_attention(const struct aiueos_qwen35_layer *layer,
                          struct qwen35_decode_context *decode,
                          uint32_t full_slot) {
  const struct aiueos_qwen35_attention_tensors *full = &layer->mixer.full;
  if (!rms_norm(state, &layer->attention_norm, EMBED, normalized) ||
      !matvec(&full->query_gate, normalized, EMBED, scratch_a, FULL_QG) ||
      !matvec(&full->value, normalized, EMBED, scratch_b, FULL_VALUE))
    return fail_at(AIUEOS_QWEN35_FAILURE_ATTENTION_PROJECTION);

  if (decode) {
    /* This output must not be `dequantized`: matvec_range uses that array as
       its BSP row buffer, so every next key row would overwrite the outputs
       already computed (and race the AP half).  A one-element position-zero
       softmax masks the corruption because its weight is always one; position
       one is the first time cached keys affect a score.  Keep values, the gate
       temporary, and this key projection in three disjoint scratch_b ranges. */
    float *key_projection = scratch_b + FULL_KEY_TEMP_OFFSET;
    if (decode->position >= AIUEOS_QWEN35_GENERATION_TOKENS ||
        !matvec(&full->key, normalized, EMBED, key_projection, FULL_VALUE) ||
        !finite_values(key_projection, FULL_VALUE))
      return fail_at(AIUEOS_QWEN35_FAILURE_FULL_KEY);

    /* Remove the per-head Q/G interleave before normalization and RoPE. */
    for (uint32_t head = 0; head < 24; head++)
      for (uint32_t index = 0; index < HEAD_DIM; index++)
        scratch_c[head * HEAD_DIM + index] =
          scratch_a[head * HEAD_DIM * 2U + index];
    if (!rms_norm_heads_weighted(scratch_c, 24, HEAD_DIM,
                                 &full->query_norm) ||
        !rms_norm_heads_weighted(key_projection, 4, HEAD_DIM,
                                 &full->key_norm))
      return fail_at(AIUEOS_QWEN35_FAILURE_FULL_KEY);
    rope_heads(scratch_c, 24, decode->position);
    rope_heads(key_projection, 4, decode->position);

    uint64_t entry = ((uint64_t)full_slot * AIUEOS_QWEN35_GENERATION_TOKENS +
                      decode->position) * FULL_KV_WIDTH;
    float *key_entry = decode->full_key + entry;
    float *value_entry = decode->full_value + entry;
    float *shadow_entry = decode->full_key_shadow + entry;
    for (uint32_t index = 0; index < FULL_KV_WIDTH; index++) {
      key_entry[index] = key_projection[index];
      shadow_entry[index] = key_projection[index];
      value_entry[index] = scratch_b[index];
    }
    uint32_t cache_index =
      full_slot * AIUEOS_QWEN35_GENERATION_TOKENS + decode->position;
    decode->full_key_hash[cache_index] =
      float_values_hash(key_projection, FULL_KV_WIDTH);

    if (decode->position) for (uint32_t head = 0; head < 24; head++) {
      uint32_t kv_head = head / 6U;
      const float *query = scratch_c + head * HEAD_DIM;
      if (!finite_values(query, HEAD_DIM))
        return fail_at(AIUEOS_QWEN35_FAILURE_FULL_QUERY);
      double attention_scores[AIUEOS_QWEN35_GENERATION_TOKENS];
      double maximum = -1.7976931348623157e+308;
      for (uint32_t prior = 0; prior <= decode->position; prior++) {
        uint64_t cache_entry =
          (uint64_t)full_slot * AIUEOS_QWEN35_GENERATION_TOKENS + prior;
        const float *key = resolved_cached_key(decode, cache_entry, kv_head);
        if (!key) return fail_at(AIUEOS_QWEN35_FAILURE_FULL_CACHE);
        double score;
        if (!stable_attention_score(query, key, HEAD_DIM, &score))
          return fail_at(AIUEOS_QWEN35_FAILURE_FULL_SOFTMAX);
        attention_scores[prior] = score;
        if (score > maximum) maximum = score;
      }
      float denominator = 0.0f;
      for (uint32_t prior = 0; prior <= decode->position; prior++) {
        double difference = attention_scores[prior] - maximum;
        float shifted = difference <= -87.0 ? -87.0f :
                        difference >= 0.0 ? 0.0f : (float)difference;
        beta_values[prior] = local_exp(shifted);
        denominator += beta_values[prior];
      }
      if (!(denominator > 0.0f) || !finite_float(denominator))
        return fail_at(AIUEOS_QWEN35_FAILURE_FULL_SOFTMAX);
      float *output = scratch_a + head * HEAD_DIM;
      for (uint32_t index = 0; index < HEAD_DIM; index++) output[index] = 0.0f;
      for (uint32_t prior = 0; prior <= decode->position; prior++) {
        uint64_t prior_entry =
          ((uint64_t)full_slot * AIUEOS_QWEN35_GENERATION_TOKENS + prior) *
          FULL_KV_WIDTH + kv_head * HEAD_DIM;
        float weight = beta_values[prior] / denominator;
        const float *value = decode->full_value + prior_entry;
        for (uint32_t index = 0; index < HEAD_DIM; index++)
          output[index] += value[index] * weight;
      }
      for (uint32_t index = 0; index < HEAD_DIM; index++) {
        float gate = scratch_a[head * HEAD_DIM * 2U + HEAD_DIM + index];
        /* The gate projection is still in the original interleaved buffer for
           heads not yet overwritten.  Save it before writing the output. */
        scratch_b[FULL_GATE_TEMP_OFFSET + index] = gate;
      }
      for (uint32_t index = 0; index < HEAD_DIM; index++)
        output[index] *= sigmoid(scratch_b[FULL_GATE_TEMP_OFFSET + index]);
    }
    if (decode->position) {
      /* Attention output must be contiguous [24,256].  The computation above
         wrote it there in scratch_a after consuming each head's gate. */
      for (uint32_t index = 0; index < LINEAR_INNER; index++)
        scratch_c[index] = scratch_a[index];
    }
  }
  if (!decode || !decode->position) {
    /* Position zero still populates the normalized K/V cache above, but its
       emitted activation must be bit-for-bit the same reduction as the
       physically-qualified cache-free first-token path.  A one-element
       softmax is algebraically equivalent, not floating-point equivalent. */
    for (uint32_t head = 0; head < 24; head++) {
      uint32_t value_head = head / 6U;
      for (uint32_t index = 0; index < HEAD_DIM; index++) {
        float gate = scratch_a[head * HEAD_DIM * 2U + HEAD_DIM + index];
        scratch_c[head * HEAD_DIM + index] =
            scratch_b[value_head * HEAD_DIM + index] * sigmoid(gate);
      }
    }
  }
  if (!matvec(&full->output, scratch_c, LINEAR_INNER, normalized, EMBED))
    return fail_at(AIUEOS_QWEN35_FAILURE_FULL_OUTPUT);
  for (uint32_t index = 0; index < EMBED; index++) state[index] += normalized[index];
  return 1;
}

struct qwen35_token_choice {
  uint32_t token;
  uint32_t second_token;
  float logit;
  float second_logit;
  uint64_t cycles;
  uint32_t failed_layer;
  uint32_t failure_stage;
};

static int inference_inputs_valid(const struct aiueos_qwen35_model *model,
                                  uint32_t input_token,
                                  const void *workspace,
                                  uint64_t workspace_bytes,
                                  uint64_t required_bytes) {
  return model && workspace && !((uintptr_t)workspace & 15U) &&
         workspace_bytes >= required_bytes &&
         model->accessible_bytes == model->artifact_bytes &&
         input_token < model->vocab_size &&
         model->artifact_bytes == AIUEOS_QWEN35_ARTIFACT_BYTES;
}

static void attach_workspace(void *workspace) {
  struct qwen35_workspace *memory = (struct qwen35_workspace *)workspace;
  state = memory->state;
  normalized = memory->normalized;
  scratch_a = memory->scratch_a;
  scratch_b = memory->scratch_b;
  scratch_c = memory->scratch_c;
  dequantized = memory->dequantized;
  ap_dequantized = memory->ap_dequantized;
  beta_values = memory->beta_values;
}

static void configure_backend(void) {
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
}

static int evaluate_token(const struct aiueos_qwen35_model *model,
                          uint32_t input_token,
                          struct qwen35_decode_context *decode,
                          aiueos_qwen35_progress_fn progress,
                          struct qwen35_token_choice *choice) {
  if (!choice) return 0;
  choice->failed_layer = 0;
  choice->failure_stage = AIUEOS_QWEN35_FAILURE_NONE;
  qwen_failure_stage = AIUEOS_QWEN35_FAILURE_NONE;
  if (!tensor_row(&model->token_embedding, input_token, state)) {
    choice->failure_stage = AIUEOS_QWEN35_FAILURE_EMBEDDING;
    return 0;
  }

  uint64_t started = read_cycles();
  uint32_t linear_slot = 0;
  uint32_t full_slot = 0;
  for (uint32_t index = 0; index < AIUEOS_QWEN35_TRUNK_LAYER_COUNT; index++) {
    const struct aiueos_qwen35_layer *layer = &model->layers[index];
    int ok = layer->linear_attention ?
      linear_attention(layer, decode, linear_slot++) :
      full_attention(layer, decode, full_slot++);
    if (!ok) {
      choice->failed_layer = index + 1U;
      choice->failure_stage = qwen_failure_stage;
      return 0;
    }
    if (!ffn(layer)) {
      choice->failed_layer = index + 1U;
      choice->failure_stage = AIUEOS_QWEN35_FAILURE_FFN;
      return 0;
    }
    if (!finite_values(state, EMBED)) {
      choice->failed_layer = index + 1U;
      choice->failure_stage = AIUEOS_QWEN35_FAILURE_STATE_NONFINITE;
      return 0;
    }
    if (progress) progress(index + 1U, AIUEOS_QWEN35_TRUNK_LAYER_COUNT, 0);
  }

  if (!rms_norm(state, &model->output_norm, EMBED, normalized) ||
      !finite_values(normalized, EMBED)) {
    choice->failure_stage = AIUEOS_QWEN35_FAILURE_OUTPUT_NORM;
    return 0;
  }
  if (progress) progress(64, 64, 1);

  choice->token = UINT32_MAX;
  choice->second_token = UINT32_MAX;
  choice->logit = -3.402823466e+38f;
  choice->second_logit = -3.402823466e+38f;
  for (uint32_t token = 0; token < model->vocab_size; token++) {
    if (!tensor_row(&model->output, token, dequantized)) {
      choice->failure_stage = AIUEOS_QWEN35_FAILURE_OUTPUT_LOGITS;
      return 0;
    }
    float logit = dot(dequantized, normalized, EMBED);
    if (!finite_float(logit)) {
      choice->failure_stage = AIUEOS_QWEN35_FAILURE_OUTPUT_LOGITS;
      return 0;
    }
    if (logit > choice->logit) {
      choice->second_logit = choice->logit;
      choice->second_token = choice->token;
      choice->logit = logit;
      choice->token = token;
    } else if (logit > choice->second_logit) {
      choice->second_logit = logit;
      choice->second_token = token;
    }
  }
  uint64_t finished = read_cycles();
  choice->cycles = finished >= started ? finished - started : 0;
  if (choice->token == UINT32_MAX || choice->second_token == UINT32_MAX) {
    choice->failure_stage = AIUEOS_QWEN35_FAILURE_OUTPUT_SELECTION;
    return 0;
  }
  return 1;
}

int aiueos_qwen35_first_token(
    const struct aiueos_qwen35_model *model,
    uint32_t input_token,
    void *workspace,
    uint64_t workspace_bytes,
    aiueos_qwen35_progress_fn progress,
    struct aiueos_qwen35_first_token_result *result) {
  if (!result || !inference_inputs_valid(
      model, input_token, workspace, workspace_bytes,
      sizeof(struct qwen35_workspace))) return 0;
  attach_workspace(workspace);
  configure_backend();
  struct qwen35_token_choice choice;
  if (!evaluate_token(model, input_token, 0, progress, &choice)) return 0;
  result->token = choice.token;
  result->second_token = choice.second_token;
  result->logit = choice.logit;
  result->second_logit = choice.second_logit;
  result->compute_cycles = choice.cycles;
  result->vector_bits = qwen_vector_bits;
  result->worker_threads = qwen_worker_threads;
  return 1;
}

static void zero_bytes(void *memory, uint64_t count) {
  uint8_t *bytes = (uint8_t *)memory;
  for (uint64_t index = 0; index < count; index++) bytes[index] = 0;
}

int aiueos_qwen35_generate(
    const struct aiueos_qwen35_model *model,
    uint32_t input_token,
    uint32_t generated_tokens,
    void *workspace,
    uint64_t workspace_bytes,
    aiueos_qwen35_progress_fn progress,
    struct aiueos_qwen35_generation_result *result) {
  if (!result || generated_tokens < 2U ||
      generated_tokens > AIUEOS_QWEN35_GENERATION_TOKENS ||
      !inference_inputs_valid(model, input_token, workspace, workspace_bytes,
                              AIUEOS_QWEN35_DECODE_WORKSPACE_BYTES))
    return 0;
  attach_workspace(workspace);
  configure_backend();

  uint8_t *decode_memory =
    (uint8_t *)workspace + AIUEOS_QWEN35_WORKSPACE_BYTES;
  zero_bytes(decode_memory,
             AIUEOS_QWEN35_DECODE_WORKSPACE_BYTES -
             AIUEOS_QWEN35_WORKSPACE_BYTES);
  struct qwen35_decode_context decode = {
    .recurrent = (float *)(void *)decode_memory,
    .conv = (float *)(void *)(decode_memory + AIUEOS_QWEN35_RECURRENT_BYTES),
    .full_key = (float *)(void *)(decode_memory +
      AIUEOS_QWEN35_RECURRENT_BYTES + AIUEOS_QWEN35_CONV_STATE_BYTES),
    .full_value = (float *)(void *)(decode_memory +
      AIUEOS_QWEN35_RECURRENT_BYTES + AIUEOS_QWEN35_CONV_STATE_BYTES +
      AIUEOS_QWEN35_FULL_CACHE_PLANE_BYTES),
    .full_key_shadow = (float *)(void *)(decode_memory +
      AIUEOS_QWEN35_RECURRENT_BYTES + AIUEOS_QWEN35_CONV_STATE_BYTES +
      2U * AIUEOS_QWEN35_FULL_CACHE_PLANE_BYTES),
    .full_key_hash = (uint64_t *)(void *)(decode_memory +
      AIUEOS_QWEN35_RECURRENT_BYTES + AIUEOS_QWEN35_CONV_STATE_BYTES +
      3U * AIUEOS_QWEN35_FULL_CACHE_PLANE_BYTES),
    .position = 0
  };

  result->generated_tokens = 0;
  result->decode_tokens = 0;
  result->first_token_cycles = 0;
  result->decode_cycles = 0;
  result->total_cycles = 0;
  result->vector_bits = qwen_vector_bits;
  result->worker_threads = qwen_worker_threads;
  result->failed_token = 0;
  result->failed_layer = 0;
  result->failure_stage = AIUEOS_QWEN35_FAILURE_NONE;

  uint32_t current_input = input_token;
  for (uint32_t position = 0; position < generated_tokens; position++) {
    struct qwen35_token_choice choice;
    decode.position = position;
    if (progress) progress(position + 1U, generated_tokens, 3);
    if (!evaluate_token(model, current_input, &decode, progress, &choice)) {
      result->failed_token = position + 1U;
      result->failed_layer = choice.failed_layer;
      result->failure_stage = choice.failure_stage;
      return 0;
    }
    result->tokens[position] = choice.token;
    result->generated_tokens++;
    result->total_cycles += choice.cycles;
    if (!position) {
      result->first_token_cycles = choice.cycles;
      if (choice.token != AIUEOS_QWEN35_REFERENCE_FIRST_TOKEN) {
        result->failed_token = 1U;
        /* The output head is outside the trunk, so this field carries the
           observed token for the bounded physical failure report. */
        result->failed_layer = choice.token;
        result->failure_stage = AIUEOS_QWEN35_FAILURE_REFERENCE_TOKEN;
        return 0;
      }
    } else {
      result->decode_cycles += choice.cycles;
      result->decode_tokens++;
    }
    if (progress) progress(position + 1U, generated_tokens, 2);
    current_input = choice.token;
  }
  return result->generated_tokens == generated_tokens &&
         result->decode_tokens == generated_tokens - 1U;
}

#ifdef AIUEOS_QWEN35_TESTING
float aiueos_qwen35_test_softplus(float value) {
  return softplus(value);
}

void aiueos_qwen35_test_rope(float values[HEAD_DIM], uint32_t position) {
  rope_heads(values, 1, position);
}

void aiueos_qwen35_test_recurrent_step(
    float state_values[LINEAR_HEAD_DIM * LINEAR_HEAD_DIM],
    const float key[LINEAR_HEAD_DIM],
    const float query[LINEAR_HEAD_DIM],
    const float value[LINEAR_HEAD_DIM],
    float decay, float beta,
    float correction[LINEAR_HEAD_DIM],
    float output[LINEAR_HEAD_DIM]) {
  recurrent_step(state_values, key, query, value, decay, beta,
                 correction, output);
}

uint32_t aiueos_qwen35_test_linear_key_head(uint32_t value_head) {
  return value_head / LINEAR_KV_GROUP_SIZE;
}

int aiueos_qwen35_test_attention_score(
    const float query[HEAD_DIM], const float key[HEAD_DIM], double *score) {
  return stable_attention_score(query, key, HEAD_DIM, score);
}

uint64_t aiueos_qwen35_test_cache_hash(const float values[FULL_KV_WIDTH]) {
  return float_values_hash(values, FULL_KV_WIDTH);
}

int aiueos_qwen35_test_cache_resolve(
    float primary[FULL_KV_WIDTH], float shadow[FULL_KV_WIDTH],
    uint64_t expected_hash) {
  struct qwen35_decode_context decode = {
    .full_key = primary,
    .full_key_shadow = shadow,
    .full_key_hash = &expected_hash
  };
  return resolved_cached_key(&decode, 0, 0) == primary;
}

#endif

/* ── QWEN-PARITY: the Kotoba objects against this file, on the CPU ─────────
 *
 * ADR-0147.  `aiueos-qwen35-dequant-row`, `aiueos-qwen35-dot-f32` and
 * `aiueos-qwen35-matvec` are ports of `aiueos_qwen35_dequantize_row`,
 * `dot_scalar` and `matvec_range`.  Their contracts are checked against a
 * ClojureScript re-derivation in the KIR interpreter, which proves the
 * ALGORITHM and says nothing about what amu emitted.  This runs the emitted
 * objects on the target and compares every bit against the C that is compiled
 * beside them -- the only evidence that covers the backend too, and the same
 * argument `AIUEOS_X25519_OK` makes in main.c.
 *
 * The inputs are synthetic and deterministic (xorshift32), not the model: this
 * has to run in the plain UEFI smoke, where no 10.9 GiB mapping exists.  What
 * it establishes is bit-equality of the arithmetic, which is the property the
 * model path needs and the one a wrong nibble, a wrong scale index or a
 * different accumulation tree breaks.
 *
 * Prints one line per stage on the serial port, `QWEN-PARITY <stage> ok` or
 * `... mismatch`, and returns zero on the first disagreement.
 */
#ifdef AIUEOS_QWEN35_KOTOBA_PARITY

/* TWO PROFILES, AND THE REASON IS THE LINKER SCRIPT.
 * `aiueos_low_end <= 0x1f4000` leaves 999,424 bytes for text, rodata,
 * data and bss together, and since the tokenizer objects landed there is
 * less headroom than these five objects need at once (measured: adding the
 * 16,120 bytes of `qwen35-activation.o` + `qwen35-norm.o` to a link that
 * already carries the other three overflows it). So the comparison runs in
 * two halves, each linking only the objects its stages call:
 *
 *   AIUEOS_QWEN35_KOTOBA_PARITY=1   dequant, dot, matvec
 *   AIUEOS_QWEN35_KOTOBA_PARITY=2   activation, norm
 *
 * Splitting the RUN rather than the evidence: both halves are the emitted
 * objects against the compiled C on the same CPU, and neither half is
 * weaker for the other one not being linked beside it. */
static uint32_t qwen_parity_state;

static uint8_t qwen_parity_byte(void) {
  uint32_t x = qwen_parity_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  qwen_parity_state = x;
  return (uint8_t)(x & 0xffU);
}

/* A spread of magnitudes rather than a spread of mantissas: the accumulation
   tree is what is being compared, and a tree only shows itself when the
   addends round differently depending on the order they arrive in. */
static float qwen_parity_value(void) {
  int32_t mantissa = (int32_t)qwen_parity_byte() - 128;
  int32_t exponent = (int32_t)(qwen_parity_byte() & 15U) - 7;
  float value = (float)mantissa;
  for (int32_t step = 0; step < exponent; step++) value *= 2.0f;
  for (int32_t step = exponent; step < 0; step++) value *= 0.5f;
  return value;
}

static uint32_t qwen_parity_bits(float value) {
  union { float value; uint32_t bits; } representation = {value};
  return representation.bits;
}

#if AIUEOS_QWEN35_KOTOBA_PARITY == 1

#define QWEN_PARITY_COLS 256U
#define QWEN_PARITY_ROWS 4U
#define QWEN_PARITY_MAX_ROW_BYTES 1024U

extern uint64_t kotoba_aiueos_qwen35_dequant_row(uint64_t type,
                                                 const uint8_t *source,
                                                 uint64_t source_bytes,
                                                 float *destination,
                                                 uint64_t destination_bytes);
extern uint64_t kotoba_aiueos_qwen35_dot_f32(const float *left,
                                             uint64_t left_bytes,
                                             const float *right,
                                             uint64_t right_bytes,
                                             uint64_t count);
extern uint64_t kotoba_aiueos_qwen35_matvec(uint8_t *arena, uint64_t arena_bytes,
                                            const uint8_t *plan,
                                            uint64_t plan_bytes);

static uint64_t qwen_parity_row_bytes(uint32_t type) {
  return aiueos_qwen35_quant_row_bytes(type, QWEN_PARITY_COLS);
}

static void qwen_parity_write_u32(uint8_t *plan, uint32_t offset, uint32_t v) {
  plan[offset + 0] = (uint8_t)(v & 0xffU);
  plan[offset + 1] = (uint8_t)((v >> 8) & 0xffU);
  plan[offset + 2] = (uint8_t)((v >> 16) & 0xffU);
  plan[offset + 3] = (uint8_t)((v >> 24) & 0xffU);
}

static void qwen_parity_write_u64(uint8_t *plan, uint32_t offset, uint64_t v) {
  qwen_parity_write_u32(plan, offset, (uint32_t)(v & 0xffffffffU));
  qwen_parity_write_u32(plan, offset + 4, (uint32_t)(v >> 32));
}

/* [weights][input][output][row scratch] in one region, which is what
   `aiueos-qwen35-matvec` takes: `kernel-subregion` requires a BASE to be a
   parameter, so four regions cannot arrive as four bases through a five-
   argument ABI. */
static uint8_t __attribute__((section(".high_bss"), aligned(8))) qwen_parity_arena[
    QWEN_PARITY_ROWS * QWEN_PARITY_MAX_ROW_BYTES
    + QWEN_PARITY_COLS * 4U + QWEN_PARITY_ROWS * 4U + QWEN_PARITY_COLS * 4U];
static uint8_t __attribute__((section(".high_bss"), aligned(8))) qwen_parity_plan[96];
static float __attribute__((section(".high_bss"))) qwen_parity_reference_row[QWEN_PARITY_COLS];
static float __attribute__((section(".high_bss"))) qwen_parity_object_row[QWEN_PARITY_COLS];
static float __attribute__((section(".high_bss"))) qwen_parity_reference_out[QWEN_PARITY_ROWS];

/* F32, Q8_0, Q4_K, Q6_K -- the four this object decodes.  The IQ types stay in
   the C for want of a rodata facility to hold their codebook grids. */
static const uint32_t qwen_parity_types[4] = {
  AIUEOS_GGML_F32, AIUEOS_GGML_Q8_0, AIUEOS_GGML_Q4_K, AIUEOS_GGML_Q6_K
};

static int qwen_parity_dequant(uint32_t type) {
  uint64_t row_bytes = qwen_parity_row_bytes(type);
  if (!row_bytes || row_bytes > QWEN_PARITY_MAX_ROW_BYTES) return 0;
  qwen_parity_state = 0x1234567u + type;
  for (uint64_t index = 0; index < row_bytes; index++)
    qwen_parity_arena[index] = qwen_parity_byte();
  if (!aiueos_qwen35_dequantize_row(type, qwen_parity_arena,
                                    QWEN_PARITY_COLS,
                                    qwen_parity_reference_row))
    return 0;
  for (uint32_t index = 0; index < QWEN_PARITY_COLS; index++)
    qwen_parity_object_row[index] = 0.0f;
  if (kotoba_aiueos_qwen35_dequant_row(type, qwen_parity_arena, row_bytes,
                                       qwen_parity_object_row,
                                       QWEN_PARITY_COLS * 4U) != 0)
    return 0;
  for (uint32_t index = 0; index < QWEN_PARITY_COLS; index++)
    if (qwen_parity_bits(qwen_parity_reference_row[index]) !=
        qwen_parity_bits(qwen_parity_object_row[index]))
      return 0;
  return 1;
}

/* Counts 0..17 and 4096: every residue of four (dot_scalar's accumulator
   step) and of eight (the tree `kernel-dot-f32` would use if the SIMD swap-in
   were wired), plus one long vector so a per-lane divergence has room to
   show. */
static int qwen_parity_dot(void) {
  float *left = (float *)(void *)qwen_parity_arena;
  float *right = left + QWEN_PARITY_COLS;
  qwen_parity_state = 0x2468aceu;
  for (uint32_t index = 0; index < QWEN_PARITY_COLS; index++) {
    left[index] = qwen_parity_value();
    right[index] = qwen_parity_value();
  }
  for (uint32_t count = 0; count <= 17U; count++) {
    uint64_t object = kotoba_aiueos_qwen35_dot_f32(left, count * 4U, right,
                                                   count * 4U, count);
    if (object != (uint64_t)(int64_t)(int32_t)
        qwen_parity_bits(dot_scalar(left, right, count)))
      return 0;
  }
  {
    uint64_t object = kotoba_aiueos_qwen35_dot_f32(
        left, QWEN_PARITY_COLS * 4U, right, QWEN_PARITY_COLS * 4U,
        QWEN_PARITY_COLS);
    if (object != (uint64_t)(int64_t)(int32_t)
        qwen_parity_bits(dot_scalar(left, right, QWEN_PARITY_COLS)))
      return 0;
  }
  /* The refusals, so the object is not merely believed to compute. */
  if (kotoba_aiueos_qwen35_dot_f32(left, 12, right, 16, 4) !=
      (uint64_t)(int64_t)-4294967299LL) return 0;
  if (kotoba_aiueos_qwen35_dot_f32(left, 16, right, 12, 4) !=
      (uint64_t)(int64_t)-4294967300LL) return 0;
  return 1;
}

static int qwen_parity_matvec(uint32_t type) {
  uint64_t row_bytes = qwen_parity_row_bytes(type);
  uint64_t weights_bytes = row_bytes * QWEN_PARITY_ROWS;
  uint64_t input_offset = weights_bytes;
  uint64_t output_offset = input_offset + QWEN_PARITY_COLS * 4U;
  uint64_t scratch_offset = output_offset + QWEN_PARITY_ROWS * 4U;
  uint64_t arena_bytes = scratch_offset + QWEN_PARITY_COLS * 4U;
  float *input;
  struct aiueos_qwen35_tensor tensor;
  if (!row_bytes || row_bytes > QWEN_PARITY_MAX_ROW_BYTES) return 0;
  if (arena_bytes > sizeof qwen_parity_arena) return 0;
  qwen_parity_state = 0x9abcdefu + type;
  for (uint64_t index = 0; index < weights_bytes; index++)
    qwen_parity_arena[index] = qwen_parity_byte();
  input = (float *)(void *)(qwen_parity_arena + input_offset);
  for (uint32_t index = 0; index < QWEN_PARITY_COLS; index++)
    input[index] = qwen_parity_value();
  for (uint64_t index = output_offset; index < arena_bytes; index++)
    qwen_parity_arena[index] = 0;

  tensor.dimensions[0] = QWEN_PARITY_COLS;
  tensor.dimensions[1] = QWEN_PARITY_ROWS;
  tensor.dimensions[2] = 0;
  tensor.dimensions[3] = 0;
  tensor.offset = 0;
  tensor.storage_bytes = weights_bytes;
  tensor.dimension_count = 2;
  tensor.type = type;
  tensor.data = qwen_parity_arena;
  if (!matvec_range(&tensor, input, QWEN_PARITY_COLS,
                    qwen_parity_reference_out, 0, QWEN_PARITY_ROWS,
                    qwen_parity_reference_row))
    return 0;

  for (uint32_t index = 0; index < 96U; index++) qwen_parity_plan[index] = 0;
  qwen_parity_write_u32(qwen_parity_plan, 0, type);
  qwen_parity_write_u64(qwen_parity_plan, 8, QWEN_PARITY_ROWS);
  qwen_parity_write_u64(qwen_parity_plan, 16, QWEN_PARITY_COLS);
  qwen_parity_write_u64(qwen_parity_plan, 24, 0);
  qwen_parity_write_u64(qwen_parity_plan, 32, weights_bytes);
  qwen_parity_write_u64(qwen_parity_plan, 40, input_offset);
  qwen_parity_write_u64(qwen_parity_plan, 48, output_offset);
  qwen_parity_write_u64(qwen_parity_plan, 56, scratch_offset);
  qwen_parity_write_u64(qwen_parity_plan, 64, 0);
  qwen_parity_write_u64(qwen_parity_plan, 72, QWEN_PARITY_ROWS);
  if (kotoba_aiueos_qwen35_matvec(qwen_parity_arena, arena_bytes,
                                  qwen_parity_plan, 96) != 0)
    return 0;
  {
    const float *object = (const float *)(const void *)
        (qwen_parity_arena + output_offset);
    for (uint32_t row = 0; row < QWEN_PARITY_ROWS; row++)
      if (qwen_parity_bits(qwen_parity_reference_out[row]) !=
          qwen_parity_bits(object[row]))
        return 0;
  }
  /* A plan the object must refuse: a reserved slot that is not zero.  A
     self-test that only ever sees agreement has not shown the object
     discriminates. */
  qwen_parity_write_u32(qwen_parity_plan, 4, 1);
  if (kotoba_aiueos_qwen35_matvec(qwen_parity_arena, arena_bytes,
                                  qwen_parity_plan, 96) !=
      (uint64_t)(int64_t)-4)
    return 0;
  return 1;
}


#endif /* AIUEOS_QWEN35_KOTOBA_PARITY == 1 */

#if AIUEOS_QWEN35_KOTOBA_PARITY == 2

/* Stage 3: the elementwise activations.  Every one of `local_exp`'s clamp
   boundaries is a probe value, because the clamps are where a port drifts:
   -87 returns zero, +88 saturates, and +-20 switch softplus between its three
   branches. */
#define QWEN_PARITY_ACT 128U

extern uint64_t kotoba_aiueos_qwen35_activation(uint64_t mode, float *a,
                                                const float *b, uint64_t count,
                                                uint64_t spare);
/* Every argument as a word: mode 0 reads `b` as a POINTER (the weights) while
   modes 1 and 2 read it as a head COUNT, so a typed declaration would have to
   lie about one of them. */
extern uint64_t kotoba_aiueos_qwen35_norm(uint64_t mode, uint64_t a, uint64_t b,
                                          uint64_t c, uint64_t d);

static float __attribute__((section(".high_bss"))) qwen_parity_act_in[QWEN_PARITY_ACT];
static float __attribute__((section(".high_bss"))) qwen_parity_act_ref[QWEN_PARITY_ACT];
static float __attribute__((section(".high_bss"))) qwen_parity_act_obj[QWEN_PARITY_ACT];
static float __attribute__((section(".high_bss"))) qwen_parity_act_gate[QWEN_PARITY_ACT];

static const float qwen_parity_edges[16] = {
  0.0f, 1.0f, -1.0f, 20.0f, -20.0f, 20.5f, -20.5f, 87.0f,
  -87.0f, 88.0f, -88.0f, 89.0f, -89.0f, 0.001f, -0.001f, 12.0f
};

static int qwen_parity_activation(void) {
  uint32_t index;
  qwen_parity_state = 0x5a5a5a5u;
  for (index = 0; index < QWEN_PARITY_ACT; index++)
    qwen_parity_act_in[index] = index < 16U ? qwen_parity_edges[index]
                                            : qwen_parity_value();
  for (uint32_t mode = 0; mode < 5U; mode++) {
    for (index = 0; index < QWEN_PARITY_ACT; index++) {
      float x = qwen_parity_act_in[index];
      qwen_parity_act_obj[index] = x;
      qwen_parity_act_gate[index] = (float)((int32_t)index - 64) * 0.125f;
      qwen_parity_act_ref[index] =
        mode == 0U ? silu(x) :
        mode == 1U ? sigmoid(x) :
        mode == 2U ? softplus(x) :
        mode == 3U ? local_exp(x) :
                     silu(x) * qwen_parity_act_gate[index];
    }
    if (kotoba_aiueos_qwen35_activation(mode, qwen_parity_act_obj,
                                        qwen_parity_act_gate,
                                        QWEN_PARITY_ACT, 0) != 0)
      return 0;
    for (index = 0; index < QWEN_PARITY_ACT; index++)
      if (qwen_parity_bits(qwen_parity_act_ref[index]) !=
          qwen_parity_bits(qwen_parity_act_obj[index]))
        return 0;
  }
  /* A refusal, so the object is not merely believed to compute. */
  if (kotoba_aiueos_qwen35_activation(5, qwen_parity_act_obj,
                                      qwen_parity_act_gate,
                                      QWEN_PARITY_ACT, 0) != (uint64_t)(int64_t)-2)
    return 0;
  return 1;
}

/* Stage 4: the three normalisations.  The reference is this file's own
   `rms_norm`, `l2_norm_heads` and `rms_norm_heads_weighted`, which reduce in
   f64 over f32 squares and narrow at different points -- the property the port
   is most likely to get subtly wrong. */
#define QWEN_PARITY_NORM_HEADS 4U
#define QWEN_PARITY_NORM_WIDTH 32U
#define QWEN_PARITY_NORM (QWEN_PARITY_NORM_HEADS * QWEN_PARITY_NORM_WIDTH)

static float __attribute__((section(".high_bss"))) qwen_parity_norm_in[QWEN_PARITY_NORM];
static float __attribute__((section(".high_bss"))) qwen_parity_norm_ref[QWEN_PARITY_NORM];
static float __attribute__((section(".high_bss"))) qwen_parity_norm_obj[QWEN_PARITY_NORM];
static float __attribute__((section(".high_bss"))) qwen_parity_norm_w[QWEN_PARITY_NORM];

static void qwen_parity_norm_fill(uint32_t seed) {
  qwen_parity_state = seed;
  for (uint32_t index = 0; index < QWEN_PARITY_NORM; index++) {
    qwen_parity_norm_in[index] = qwen_parity_value();
    qwen_parity_norm_w[index] = qwen_parity_value();
  }
}

static int qwen_parity_norm(void) {
  struct aiueos_qwen35_tensor weights;
  uint32_t index;
  qwen_parity_norm_fill(0x7654321u);
  weights.dimensions[0] = QWEN_PARITY_NORM;
  weights.dimensions[1] = 0;
  weights.dimensions[2] = 0;
  weights.dimensions[3] = 0;
  weights.offset = 0;
  weights.storage_bytes = QWEN_PARITY_NORM * sizeof(float);
  weights.dimension_count = 1;
  weights.type = AIUEOS_GGML_F32;
  weights.data = (const uint8_t *)(const void *)qwen_parity_norm_w;

  /* mode 0: rms_norm, out of place. */
  if (!rms_norm(qwen_parity_norm_in, &weights, QWEN_PARITY_NORM,
                qwen_parity_norm_ref))
    return 0;
  for (index = 0; index < QWEN_PARITY_NORM; index++)
    qwen_parity_norm_obj[index] = 0.0f;
  /* `[mode input weights count output]` -- the object's own order, which is
     `rms_norm`'s. Getting this wrong is not a crash: it computes a norm of the
     wrong vector into the wrong place and every bound still holds. Measured
     2026-09-02 under QEMU, where an earlier draft of this harness passed the
     output buffer as the input and the comparison said `norm mismatch`. */
  if (kotoba_aiueos_qwen35_norm(0, (uint64_t)(uintptr_t)qwen_parity_norm_in,
                                (uint64_t)(uintptr_t)qwen_parity_norm_w,
                                QWEN_PARITY_NORM,
                                (uint64_t)(uintptr_t)qwen_parity_norm_obj) != 0)
    return 0;
  for (index = 0; index < QWEN_PARITY_NORM; index++)
    if (qwen_parity_bits(qwen_parity_norm_ref[index]) !=
        qwen_parity_bits(qwen_parity_norm_obj[index]))
      return 0;

  /* mode 1: l2_norm_heads, in place. */
  qwen_parity_norm_fill(0x1111111u);
  for (index = 0; index < QWEN_PARITY_NORM; index++) {
    qwen_parity_norm_ref[index] = qwen_parity_norm_in[index];
    qwen_parity_norm_obj[index] = qwen_parity_norm_in[index];
  }
  l2_norm_heads(qwen_parity_norm_ref, QWEN_PARITY_NORM_HEADS,
                QWEN_PARITY_NORM_WIDTH);
  if (kotoba_aiueos_qwen35_norm(1, (uint64_t)(uintptr_t)qwen_parity_norm_obj,
                                QWEN_PARITY_NORM_HEADS,
                                QWEN_PARITY_NORM_WIDTH, 0) != 0)
    return 0;
  for (index = 0; index < QWEN_PARITY_NORM; index++)
    if (qwen_parity_bits(qwen_parity_norm_ref[index]) !=
        qwen_parity_bits(qwen_parity_norm_obj[index]))
      return 0;

  /* mode 2: rms_norm_heads_weighted, in place, one weight row per head. */
  qwen_parity_norm_fill(0x2222222u);
  for (index = 0; index < QWEN_PARITY_NORM; index++) {
    qwen_parity_norm_ref[index] = qwen_parity_norm_in[index];
    qwen_parity_norm_obj[index] = qwen_parity_norm_in[index];
  }
  weights.dimensions[0] = QWEN_PARITY_NORM_WIDTH;
  weights.storage_bytes = QWEN_PARITY_NORM_WIDTH * sizeof(float);
  if (!rms_norm_heads_weighted(qwen_parity_norm_ref, QWEN_PARITY_NORM_HEADS,
                               QWEN_PARITY_NORM_WIDTH, &weights))
    return 0;
  if (kotoba_aiueos_qwen35_norm(2, (uint64_t)(uintptr_t)qwen_parity_norm_obj,
                                QWEN_PARITY_NORM_HEADS,
                                QWEN_PARITY_NORM_WIDTH,
                                (uint64_t)(uintptr_t)qwen_parity_norm_w) != 0)
    return 0;
  for (index = 0; index < QWEN_PARITY_NORM; index++)
    if (qwen_parity_bits(qwen_parity_norm_ref[index]) !=
        qwen_parity_bits(qwen_parity_norm_obj[index]))
      return 0;

  /* A refusal: mode 3 does not exist. */
  if (kotoba_aiueos_qwen35_norm(3, (uint64_t)(uintptr_t)qwen_parity_norm_obj,
                                1, 8, 0) != (uint64_t)(int64_t)-2)
    return 0;
  return 1;
}

#endif /* AIUEOS_QWEN35_KOTOBA_PARITY == 2 */

int aiueos_qwen35_kotoba_parity_selftest(uint32_t stage) {
#if AIUEOS_QWEN35_KOTOBA_PARITY == 1
  if (stage == 0) {
    for (uint32_t index = 0; index < 4U; index++)
      if (!qwen_parity_dequant(qwen_parity_types[index])) return 0;
    return 1;
  }
  if (stage == 1) return qwen_parity_dot();
  if (stage == 2) {
    for (uint32_t index = 0; index < 4U; index++)
      if (!qwen_parity_matvec(qwen_parity_types[index])) return 0;
    return 1;
  }
#else
  if (stage == 3) return qwen_parity_activation();
  if (stage == 4) return qwen_parity_norm();
#endif
  /* A stage this profile did not compile is a REFUSAL, not a pass: a loop that
     asked for one and got 1 would report `ok` for a comparison that never ran. */
  return 0;
}

#endif
