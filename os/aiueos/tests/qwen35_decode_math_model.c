#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qwen35_infer.h"

#define HEAD 128U
#define FULL_HEAD 256U
#define FULL_KV 1024U

float aiueos_qwen35_test_softplus(float);
void aiueos_qwen35_test_rope(float[FULL_HEAD], uint32_t);
void aiueos_qwen35_test_recurrent_step(
    float[HEAD * HEAD], const float[HEAD], const float[HEAD],
    const float[HEAD], float, float, float[HEAD], float[HEAD]);
uint32_t aiueos_qwen35_test_linear_key_head(uint32_t);
int aiueos_qwen35_test_attention_score(
    const float[FULL_HEAD], const float[FULL_HEAD], double *);
uint64_t aiueos_qwen35_test_cache_hash(const float[FULL_KV]);
int aiueos_qwen35_test_cache_resolve(
    float[FULL_KV], float[FULL_KV], uint64_t);
int aiueos_qwen35_test_output_logit(
    const float *, const float *, uint32_t, float *);

int aiueos_qwen35_dequantize_row(uint32_t type, const uint8_t *data,
                                 uint64_t elements, float *output) {
  (void)type; (void)data; (void)elements; (void)output;
  return 0;
}

uint64_t aiueos_qwen35_quant_row_bytes(uint32_t type, uint64_t elements) {
  (void)type; (void)elements;
  return 0;
}

static int near(float actual, float expected, float tolerance) {
  return fabsf(actual - expected) <= tolerance;
}

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "check failed: %s\n", #x); return 1; \
} } while (0)

static float recurrent_state[HEAD * HEAD];
static float expected_state[HEAD * HEAD];
static float key[HEAD], query[HEAD], value[HEAD];
static float correction[HEAD], output[HEAD], expected_output[HEAD];
static float primary_cache[FULL_KV], shadow_cache[FULL_KV];
static float output_left[5120], output_right[5120];

static void reference_step(float decay, float beta) {
  float reference_correction[HEAD];
  for (uint32_t column = 0; column < HEAD; column++) {
    float remembered = 0.0f;
    for (uint32_t row = 0; row < HEAD; row++) {
      expected_state[row * HEAD + column] *= decay;
      remembered += expected_state[row * HEAD + column] * key[row];
    }
    reference_correction[column] = (value[column] - remembered) * beta;
  }
  for (uint32_t row = 0; row < HEAD; row++)
    for (uint32_t column = 0; column < HEAD; column++)
      expected_state[row * HEAD + column] +=
        key[row] * reference_correction[column];
  for (uint32_t column = 0; column < HEAD; column++) {
    float sum = 0.0f;
    for (uint32_t row = 0; row < HEAD; row++)
      sum += expected_state[row * HEAD + column] * query[row];
    expected_output[column] = sum * 0.08838834764831845f;
  }
}

int main(void) {
  CHECK(strcmp(aiueos_qwen35_failure_stage_label(
                 AIUEOS_QWEN35_FAILURE_FULL_KEY), "FULL KEY") == 0);
  CHECK(strcmp(aiueos_qwen35_failure_stage_label(
                 AIUEOS_QWEN35_FAILURE_FULL_SOFTMAX), "SOFTMAX") == 0);
  CHECK(strcmp(aiueos_qwen35_failure_stage_label(
                 AIUEOS_QWEN35_FAILURE_FULL_QUERY), "FULL QUERY") == 0);
  CHECK(strcmp(aiueos_qwen35_failure_stage_label(
                 AIUEOS_QWEN35_FAILURE_FULL_CACHE), "KV CACHE") == 0);
  CHECK(strcmp(aiueos_qwen35_failure_stage_label(999U), "UNKNOWN") == 0);

  /* Qwen3.5 repeat_interleave maps each of the 16 Q/K heads to three
     adjacent value heads, rather than cycling heads with modulo. */
  for (uint32_t head = 0; head < 48U; head++)
    CHECK(aiueos_qwen35_test_linear_key_head(head) == head / 3U);

  /* Finite full-attention operands are multiplied in double precision so
     their score cannot fail solely because a float product overflows. */
  float large_query[FULL_HEAD], large_key[FULL_HEAD];
  for (uint32_t i = 0; i < FULL_HEAD; i++) {
    large_query[i] = 1.0e30f;
    large_key[i] = i & 1U ? -1.0e30f : 1.0e30f;
  }
  double large_score = 0.0;
  CHECK(aiueos_qwen35_test_attention_score(
          large_query, large_key, &large_score));
  CHECK(isfinite(large_score));

  for (uint32_t i = 0; i < FULL_KV; i++)
    primary_cache[i] = shadow_cache[i] = (float)(i + 1U) / 1024.0f;
  uint64_t cache_hash = aiueos_qwen35_test_cache_hash(primary_cache);
  primary_cache[17] += 1.0f;
  CHECK(aiueos_qwen35_test_cache_resolve(
          primary_cache, shadow_cache, cache_hash));
  CHECK(memcmp(primary_cache, shadow_cache, sizeof(primary_cache)) == 0);
  primary_cache[17] += 1.0f;
  shadow_cache[18] += 1.0f;
  CHECK(!aiueos_qwen35_test_cache_resolve(
          primary_cache, shadow_cache, cache_hash));

  /* The ordinary float dot overflows its positive and negative lanes, while
     the mathematically finite cancellation remains representable. */
  for (uint32_t i = 0; i < 5120U; i++) {
    output_left[i] = 1.0e30f;
    output_right[i] = i & 1U ? -1.0e30f : 1.0e30f;
  }
  float stable_output = 1.0f;
  CHECK(aiueos_qwen35_test_output_logit(
          output_left, output_right, 5120U, &stable_output));
  CHECK(stable_output == 0.0f);

  const float samples[] = {-10.0f, -1.0f, 0.0f, 1.0f, 10.0f};
  for (uint32_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
    float expected = log1pf(expf(samples[i]));
    CHECK(near(aiueos_qwen35_test_softplus(samples[i]), expected, 0.00002f));
  }

  float rope[FULL_HEAD] = {0};
  rope[0] = 1.0f;
  aiueos_qwen35_test_rope(rope, 1);
  CHECK(near(rope[0], cosf(1.0f), 0.000002f));
  CHECK(near(rope[32], sinf(1.0f), 0.000002f));

  key[0] = 0.6f; key[1] = 0.8f;
  query[0] = 0.8f; query[1] = 0.6f;
  for (uint32_t i = 0; i < HEAD; i++) value[i] = (float)(i + 1U) / 256.0f;
  reference_step(1.0f, 0.25f);
  aiueos_qwen35_test_recurrent_step(
    recurrent_state, key, query, value, 1.0f, 0.25f,
    correction, output);
  for (uint32_t i = 0; i < HEAD * HEAD; i++)
    CHECK(near(recurrent_state[i], expected_state[i], 0.0000005f));
  for (uint32_t i = 0; i < HEAD; i++)
    CHECK(near(output[i], expected_output[i], 0.0000005f));

  for (uint32_t i = 0; i < HEAD; i++)
    value[i] = (float)(HEAD - i) / 128.0f;
  reference_step(0.75f, 0.4f);
  aiueos_qwen35_test_recurrent_step(
    recurrent_state, key, query, value, 0.75f, 0.4f,
    correction, output);
  for (uint32_t i = 0; i < HEAD * HEAD; i++)
    CHECK(near(recurrent_state[i], expected_state[i], 0.000001f));
  for (uint32_t i = 0; i < HEAD; i++)
    CHECK(near(output[i], expected_output[i], 0.000001f));

  puts("AIUEOS_QWEN35_DECODE_MATH_OK softplus=reference rope=partial64 recurrent=delta-rule qk=repeat-interleave score=double output-overflow=double-fallback");
  return 0;
}
