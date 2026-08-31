#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/device_result.h"

static char signed_text[1024];
static uint32_t signed_length;

int aiueos_device_test_random_bytes(uint8_t *out, uint32_t bytes) {
  for (uint32_t i = 0; i < bytes; i++) out[i] = 0x02;
  return 1;
}

int aiueos_device_p256_key_load(uint8_t key[32]) {
  memset(key, 0, 32);
  key[31] = 1;
  return 1;
}

int aiueos_device_p256_key_save(const uint8_t key[32]) {
  (void)key;
  return 0;
}

uint64_t kotoba_aiueos_sha256(const uint8_t *input, uint64_t bytes,
                              uint8_t digest[32], uint8_t *workspace,
                              uint64_t workspace_bytes) {
  (void)workspace;
  (void)workspace_bytes;
  if (bytes >= sizeof(signed_text)) return 0;
  memcpy(signed_text, input, (size_t)bytes);
  signed_text[bytes] = 0;
  signed_length = (uint32_t)bytes;
  memset(digest, 0x33, 32);
  return 1;
}

uint64_t kotoba_aiueos_ecdsa_p256_sign(const uint8_t *private_key,
                                       const uint8_t *digest,
                                       const uint8_t *nonce,
                                       uint8_t *signature,
                                       uint8_t *workspace) {
  (void)private_key;
  (void)digest;
  (void)nonce;
  (void)workspace;
  memset(signature, 0x55, 64);
  return 1;
}

uint64_t kotoba_aiueos_ecdsa_p256_public(const uint8_t *private_key,
                                         uint8_t *public_key,
                                         uint8_t *workspace) {
  (void)private_key;
  (void)workspace;
  memset(public_key, 0x01, 64);
  return 1;
}

static int contains(const uint8_t *request, uint32_t length,
                    const char *needle) {
  size_t n = strlen(needle);
  if (!n || n > length) return 0;
  for (uint32_t i = 0; i + n <= length; i++)
    if (!memcmp(request + i, needle, n)) return 1;
  return 0;
}

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "check failed: %s\n", #x); return 1; \
} } while (0)

int main(void) {
  struct aiueos_boot_info boot = {
    .magic = AIUEOS_BOOT_INFO_MAGIC,
    .version = AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED,
    .model_load_cycles = 8000000000ULL,
    .tsc_hz = 4000000000ULL
  };
  memset(boot.model_sha256, 0xaa, sizeof(boot.model_sha256));
  const uint8_t mac[6] = {0x70, 0x70, 0xfc, 0x0b, 0xb6, 0x32};
  uint8_t request[1024];
  char did[96];

  struct aiueos_device_result result = {
    .boot = &boot, .mac = mac, .token = 2005, .second_token = 17,
    .generated_tokens = 8, .decode_tokens = 7,
    .first_token_cycles = 12000000000ULL,
    .decode_cycles = 56000000000ULL,
    .inference_cycles = 68000000000ULL,
    .vector_bits = 256, .worker_threads = 2
  };
  uint32_t length = aiueos_device_result_http_request(
    &result, request, sizeof(request), did, sizeof(did));
  if (!length) fprintf(stderr, "result request refused signed-length=%u\n",
                       signed_length);
  CHECK(length && signed_length);
  CHECK(!strncmp(signed_text, "aiueos-k16-result-v2\n", 21));
  CHECK(strstr(signed_text,
    "\nQwen3.8-27B-UD-IQ3_XXS.gguf\n2\n2005\n17\n8\n7\n"
    "8000000000\n12000000000\n56000000000\n68000000000\n"
    "256\n2\n4000000000\n"));
  CHECK(contains(request, length, "\"protocol\":2"));
  CHECK(contains(request, length, "\"decode-tokens\":7"));
  CHECK(contains(request, length, "\"decode-cycles\":56000000000"));
  CHECK(contains(request, length, "User-Agent: aiueos-k16-node-v2"));

  struct aiueos_device_worker_request worker = {
    .boot = &boot, .mac = mac, .sequence = 7,
    .operation = AIUEOS_DEVICE_WORKER_RESULT, .job_id = 42,
    .token = 2005, .second_token = 17,
    .generated_tokens = 8, .decode_tokens = 7,
    .first_token_cycles = 12000000000ULL,
    .decode_cycles = 56000000000ULL,
    .inference_cycles = 68000000000ULL,
    .vector_bits = 256, .worker_threads = 2
  };
  length = aiueos_device_worker_http_request(
    &worker, request, sizeof(request), did, sizeof(did));
  CHECK(length && !strncmp(signed_text, "aiueos-k16-worker-v2\n", 21));
  CHECK(strstr(signed_text,
    "\n7\nresult\n42\n2\n2005\n17\n8\n7\n"
    "12000000000\n56000000000\n68000000000\n256\n2\n4000000000\n"));
  CHECK(contains(request, length, "User-Agent: aiueos-k16-worker-v2"));

  worker = (struct aiueos_device_worker_request){
    .boot = &boot, .mac = mac, .sequence = 8,
    .operation = AIUEOS_DEVICE_WORKER_POLL
  };
  length = aiueos_device_worker_http_request(
    &worker, request, sizeof(request), did, sizeof(did));
  CHECK(length && strstr(signed_text,
    "\n8\npoll\n-\n2\n0\n0\n0\n0\n0\n0\n0\n0\n0\n4000000000\n"));
  puts("AIUEOS_DEVICE_RESULT_V2_OK signed=decode-boundary backward-server=v1+v2");
  return 0;
}
