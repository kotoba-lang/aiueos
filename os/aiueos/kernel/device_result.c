#include <stdint.h>
#include "device_result.h"

#define DEVICE_CANONICAL_MAX 512U
#define DEVICE_BODY_MAX 768U

extern uint64_t kotoba_aiueos_sha256(
    const uint8_t *, uint64_t, uint8_t[32], uint8_t *, uint64_t);
extern uint64_t kotoba_aiueos_ecdsa_p256_sign(
    const uint8_t *, const uint8_t *, const uint8_t *, uint8_t *, uint8_t *);
extern uint64_t kotoba_aiueos_ecdsa_p256_public(
    const uint8_t *, uint8_t *, uint8_t *);
extern int aiueos_device_p256_key_load(uint8_t key[32]);
extern int aiueos_device_p256_key_save(const uint8_t key[32]);

static const uint8_t p256_order[32] = {
  0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,
  0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51
};
static const char base58_alphabet[] =
  "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char hex_alphabet[] = "0123456789abcdef";
static uint8_t sha_workspace[512];
static uint8_t p256_workspace[2048];

struct bounded_buffer {
  uint8_t *bytes;
  uint32_t length, capacity;
  int ok;
};

static void secure_zero(void *memory, uint32_t bytes) {
  volatile uint8_t *out = (volatile uint8_t *)memory;
  while (bytes--) *out++ = 0;
}

static void put_byte(struct bounded_buffer *b, uint8_t value) {
  if (!b->ok || b->length >= b->capacity) { b->ok = 0; return; }
  b->bytes[b->length++] = value;
}

static void put_text(struct bounded_buffer *b, const char *text) {
  if (!text) { b->ok = 0; return; }
  while (*text) put_byte(b, (uint8_t)*text++);
}

static void put_decimal(struct bounded_buffer *b, uint64_t value) {
  char digits[20];
  uint32_t n = 0;
  do {
    digits[n++] = (char)('0' + value % 10U);
    value /= 10U;
  } while (value && n < sizeof(digits));
  while (n) put_byte(b, (uint8_t)digits[--n]);
}

static void hex_encode(const uint8_t *in, uint32_t bytes, char *out) {
  for (uint32_t i = 0; i < bytes; i++) {
    out[i * 2] = hex_alphabet[in[i] >> 4];
    out[i * 2 + 1] = hex_alphabet[in[i] & 15U];
  }
  out[bytes * 2] = 0;
}

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
  __asm__ volatile("cpuid"
                   : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                   : "a"(leaf), "c"(subleaf));
}

static int rdrand64(uint64_t *value) {
  for (uint32_t attempt = 0; attempt < 32; attempt++) {
    uint8_t ready;
    uint64_t candidate;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(candidate), "=qm"(ready));
    if (ready) { *value = candidate; return 1; }
    __asm__ volatile("pause");
  }
  return 0;
}

int aiueos_cpu_random_bytes(uint8_t *out, uint32_t bytes) {
  uint32_t a, b, c, d;
  if (!out || !bytes) return 0;
  cpuid(1, 0, &a, &b, &c, &d);
  if (!(c & (1U << 30))) return 0;
  uint32_t offset = 0;
  while (offset < bytes) {
    uint64_t word;
    if (!rdrand64(&word)) return 0;
    for (uint32_t i = 0; i < 8 && offset < bytes; i++, offset++)
      out[offset] = (uint8_t)(word >> (i * 8));
  }
  return 1;
}

static int scalar_valid(const uint8_t scalar[32]) {
  int nonzero = 0;
  for (uint32_t i = 0; i < 32; i++) nonzero |= scalar[i];
  if (!nonzero) return 0;
  for (uint32_t i = 0; i < 32; i++) {
    if (scalar[i] < p256_order[i]) return 1;
    if (scalar[i] > p256_order[i]) return 0;
  }
  return 0;
}

static int random_scalar(uint8_t scalar[32]) {
  for (uint32_t attempt = 0; attempt < 16; attempt++)
    if (aiueos_cpu_random_bytes(scalar, 32) && scalar_valid(scalar)) return 1;
  return 0;
}

static int device_private_key(uint8_t key[32]) {
  if (aiueos_device_p256_key_load(key) && scalar_valid(key)) return 1;
  if (!random_scalar(key)) return 0;
  return aiueos_device_p256_key_save(key);
}

static int base58_encode(const uint8_t *input, uint32_t input_bytes,
                         char *out, uint32_t capacity) {
  uint8_t digits[64] = {0};
  uint32_t digit_count = 1;
  for (uint32_t j = 0; j < input_bytes; j++) {
    uint32_t carry = input[j];
    for (uint32_t i = 0; i < digit_count; i++) {
      uint32_t value = (uint32_t)digits[i] * 256U + carry;
      digits[i] = (uint8_t)(value % 58U);
      carry = value / 58U;
    }
    while (carry) {
      if (digit_count >= sizeof(digits)) return 0;
      digits[digit_count++] = (uint8_t)(carry % 58U);
      carry /= 58U;
    }
  }
  uint32_t leading = 0;
  while (leading < input_bytes && !input[leading]) leading++;
  uint32_t needed = leading + digit_count;
  if (needed + 1 > capacity) return 0;
  uint32_t at = 0;
  for (uint32_t i = 0; i < leading; i++) out[at++] = '1';
  while (digit_count) out[at++] = base58_alphabet[digits[--digit_count]];
  out[at] = 0;
  return 1;
}

static int p256_did(const uint8_t public_key[64], char *out, uint32_t capacity) {
  uint8_t multikey[35];
  char encoded[64];
  multikey[0] = 0x80;
  multikey[1] = 0x24;
  multikey[2] = (public_key[63] & 1U) ? 0x03 : 0x02;
  for (uint32_t i = 0; i < 32; i++) multikey[3 + i] = public_key[i];
  if (!base58_encode(multikey, sizeof(multikey), encoded, sizeof(encoded))) return 0;
  struct bounded_buffer b = {(uint8_t *)out, 0, capacity, 1};
  put_text(&b, "did:key:z");
  put_text(&b, encoded);
  put_byte(&b, 0);
  return b.ok;
}

static void canonical_field(struct bounded_buffer *b, const char *text) {
  put_byte(b, '\n');
  put_text(b, text);
}

static void canonical_number(struct bounded_buffer *b, uint64_t number) {
  put_byte(b, '\n');
  put_decimal(b, number);
}

uint32_t aiueos_device_result_http_request(
    const struct aiueos_device_result *result, uint8_t *out, uint32_t capacity,
    char *did_out, uint32_t did_capacity) {
  if (!result || !result->boot || !result->mac || !out || capacity < 512 ||
      !did_out || did_capacity < 48 ||
      result->boot->version < AIUEOS_BOOT_INFO_VERSION_TSC_CALIBRATED ||
      !result->boot->tsc_hz) return 0;

  uint8_t private_key[32] = {0}, public_key[64] = {0}, nonce_k[32] = {0};
  uint8_t boot_id[8] = {0}, nonce[16] = {0}, digest[32] = {0};
  uint8_t signature[64] = {0};
  uint32_t request_length = 0;
  if (!device_private_key(private_key) ||
      !kotoba_aiueos_ecdsa_p256_public(private_key, public_key, p256_workspace) ||
      !random_scalar(nonce_k) ||
      !aiueos_cpu_random_bytes(boot_id, sizeof(boot_id)) ||
      !aiueos_cpu_random_bytes(nonce, sizeof(nonce))) goto cleanup;
  if (!p256_did(public_key, did_out, did_capacity)) goto cleanup;

  char node[32] = "aiueos-k16-";
  for (uint32_t i = 0; i < 6; i++) {
    node[11 + i * 2] = hex_alphabet[result->mac[i] >> 4];
    node[12 + i * 2] = hex_alphabet[result->mac[i] & 15U];
  }
  node[23] = 0;
  char public_hex[129], boot_hex[17], model_hex[65], nonce_hex[33];
  hex_encode(public_key, 64, public_hex);
  hex_encode(boot_id, 8, boot_hex);
  hex_encode(result->boot->model_sha256, 32, model_hex);
  hex_encode(nonce, 16, nonce_hex);

  uint8_t canonical_bytes[DEVICE_CANONICAL_MAX];
  struct bounded_buffer canonical = {
    canonical_bytes, 0, sizeof(canonical_bytes), 1
  };
  put_text(&canonical, "aiueos-k16-result-v1");
  canonical_field(&canonical, node);
  canonical_field(&canonical, public_hex);
  canonical_field(&canonical, boot_hex);
  canonical_field(&canonical, model_hex);
  canonical_field(&canonical, "Qwen3.8-27B-UD-IQ3_XXS.gguf");
  canonical_number(&canonical, result->token);
  canonical_number(&canonical, result->second_token);
  canonical_number(&canonical, result->boot->model_load_cycles);
  canonical_number(&canonical, result->inference_cycles);
  canonical_number(&canonical, result->boot->tsc_hz);
  canonical_field(&canonical, nonce_hex);
  if (!canonical.ok ||
      !kotoba_aiueos_sha256(canonical.bytes, canonical.length, digest,
                            sha_workspace, sizeof(sha_workspace)) ||
      !kotoba_aiueos_ecdsa_p256_sign(private_key, digest, nonce_k,
                                     signature, p256_workspace)) goto cleanup;
  char signature_hex[129];
  hex_encode(signature, 64, signature_hex);

  uint8_t body_bytes[DEVICE_BODY_MAX];
  struct bounded_buffer body = {body_bytes, 0, sizeof(body_bytes), 1};
  put_text(&body, "{\"node\":\""); put_text(&body, node);
  put_text(&body, "\",\"public-key\":\""); put_text(&body, public_hex);
  put_text(&body, "\",\"boot\":\""); put_text(&body, boot_hex);
  put_text(&body, "\",\"model-sha256\":\""); put_text(&body, model_hex);
  put_text(&body, "\",\"model\":\"Qwen3.8-27B-UD-IQ3_XXS.gguf\",\"token\":");
  put_decimal(&body, result->token);
  put_text(&body, ",\"second-token\":"); put_decimal(&body, result->second_token);
  put_text(&body, ",\"model-load-cycles\":");
  put_decimal(&body, result->boot->model_load_cycles);
  put_text(&body, ",\"inference-cycles\":");
  put_decimal(&body, result->inference_cycles);
  put_text(&body, ",\"tsc-hz\":"); put_decimal(&body, result->boot->tsc_hz);
  put_text(&body, ",\"nonce\":\""); put_text(&body, nonce_hex);
  put_text(&body, "\",\"signature\":\""); put_text(&body, signature_hex);
  put_text(&body, "\"}");
  if (!body.ok) goto cleanup;

  struct bounded_buffer request = {out, 0, capacity, 1};
  put_text(&request, "POST /infer/nodes/device-p256-result HTTP/1.1\r\n");
  put_text(&request, "Host: api.murakumo.cloud\r\n");
  put_text(&request, "User-Agent: aiueos-k16-node-v1\r\n");
  put_text(&request, "Content-Type: application/json\r\n");
  put_text(&request, "Accept: application/json\r\nContent-Length: ");
  put_decimal(&request, body.length);
  put_text(&request, "\r\nConnection: close\r\n\r\n");
  for (uint32_t i = 0; i < body.length; i++) put_byte(&request, body.bytes[i]);
  if (request.ok) request_length = request.length;

cleanup:
  secure_zero(private_key, sizeof(private_key));
  secure_zero(nonce_k, sizeof(nonce_k));
  secure_zero(digest, sizeof(digest));
  secure_zero(p256_workspace, sizeof(p256_workspace));
  return request_length;
}
