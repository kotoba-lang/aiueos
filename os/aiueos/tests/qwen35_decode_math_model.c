#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qwen35_infer.h"

#define HEAD 128U
#define FULL_HEAD 256U

float aiueos_qwen35_test_softplus(float);
void aiueos_qwen35_test_rope(float[FULL_HEAD], uint32_t);
void aiueos_qwen35_test_recurrent_step(
    float[HEAD * HEAD], const float[HEAD], const float[HEAD],
    const float[HEAD], float, float, float[HEAD], float[HEAD]);

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
  CHECK(strcmp(aiueos_qwen35_failure_stage_label(999U), "UNKNOWN") == 0);

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

  puts("AIUEOS_QWEN35_DECODE_MATH_OK softplus=reference rope=partial64 recurrent=delta-rule");
  return 0;
}
