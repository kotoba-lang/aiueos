/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/qwen35_runtime.h"

static int check(int condition, const char *name) {
  if (condition) return 1;
  fprintf(stderr, "FAIL %s\n", name);
  return 0;
}

static uint8_t *read_file(const char *path, uint64_t *length) {
  FILE *file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0) return 0;
  long end = ftell(file);
  if (end <= 0 || fseek(file, 0, SEEK_SET) != 0) return 0;
  uint8_t *bytes = malloc((size_t)end);
  if (!bytes || fread(bytes, 1, (size_t)end, file) != (size_t)end) {
    free(bytes);
    fclose(file);
    return 0;
  }
  fclose(file);
  *length = (uint64_t)end;
  return bytes;
}

static uint8_t *find_bytes(uint8_t *bytes, uint64_t length,
                           const char *needle) {
  uint64_t needle_length = 0;
  while (needle[needle_length]) needle_length++;
  for (uint64_t i = 0; i + needle_length <= length; i++) {
    uint64_t j = 0;
    while (j < needle_length && bytes[i + j] == (uint8_t)needle[j]) j++;
    if (j == needle_length) return bytes + i;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: qwen35-runtime-model HEADER-FIXTURE\n");
    return 2;
  }
  uint64_t length = 0;
  uint8_t *bytes = read_file(argv[1], &length);
  if (!bytes) return 2;
  int ok = 1;
  static struct aiueos_qwen35_model model;
  ok &= check(length == AIUEOS_QWEN35_DATA_OFFSET, "fixture prefix length");
  ok &= check(aiueos_qwen35_model_parse(
                bytes, length, AIUEOS_QWEN35_ARTIFACT_BYTES, &model),
              "exact contract admitted");
  ok &= check(model.tensor_count == AIUEOS_QWEN35_TENSOR_COUNT &&
              model.block_count == AIUEOS_QWEN35_LAYER_COUNT &&
              model.trunk_layer_count == AIUEOS_QWEN35_TRUNK_LAYER_COUNT,
              "model counts");
  ok &= check(model.linear_layer_count == AIUEOS_QWEN35_LINEAR_LAYER_COUNT &&
              model.full_layer_count == AIUEOS_QWEN35_FULL_LAYER_COUNT &&
              model.nextn_layer_count == 1,
              "hybrid layer schedule");
  ok &= check(model.token_embedding.offset == 715182080ULL &&
              model.output.offset == 0 &&
              model.layers[0].mixer.linear.qkv.offset == 1149091840ULL &&
              model.layers[64].post_attention_norm.offset == 10923843584ULL,
              "representative tensor offsets");
  ok &= check(model.token_embedding.data == 0 && model.output.data == 0,
              "prefix parse does not fabricate data pointers");
  ok &= check(!aiueos_qwen35_model_bind(&model, bytes, length),
              "truncated mapping bind refused");
  ok &= check(!aiueos_qwen35_model_parse(
                bytes, length - 32, AIUEOS_QWEN35_ARTIFACT_BYTES, &model),
              "truncated tensor table refused");
  ok &= check(!aiueos_qwen35_model_parse(
                bytes, length, AIUEOS_QWEN35_ARTIFACT_BYTES - 1, &model),
              "wrong artifact length refused");

  uint8_t *architecture = find_bytes(bytes, length, "qwen35");
  ok &= check(architecture != 0, "architecture fixture located");
  if (architecture) {
    uint8_t saved = architecture[5];
    architecture[5] = '4';
    ok &= check(!aiueos_qwen35_model_parse(
                  bytes, length, AIUEOS_QWEN35_ARTIFACT_BYTES, &model),
                "wrong architecture refused");
    architecture[5] = saved;
  }
  free(bytes);
  if (!ok) return 1;
  puts("AIUEOS_QWEN35_RUNTIME_MODEL_OK gguf-v3 tensors=866 trunk=64 linear=48 full=16 mtp=1 bind=read-only");
  return 0;
}
