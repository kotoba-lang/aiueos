#include "tls_aes_gcm.h"

/* AES-128-GCM is `os/aiueos/kotoba/aes128-gcm.kotoba`, compiled to
   `kotoba_aiueos_aes128_gcm` (ADR-0132).  What is left in this file is the
   marshalling between the two shapes and nothing else: the S-box, the key
   schedule, the cipher, GHASH, CTR and the tag comparison -- 185 lines that
   held the whole confidentiality and integrity decision of every TLS 1.3
   record and every SSH packet this kernel sends -- are gone.

   TWO SHAPES, AND THE DIFFERENCE IS NOT COSMETIC.

   * The object returns a REASON CODE and ZERO IS SUCCESS.  This file's two
     entry points keep returning 1 for success because `kernel/tls13.c` and
     `kernel/pci.c` have twenty call sites between them that read
     `if (!aes(...)) return 0;`, and a silent convention flip is the one thing
     worse than a loud one.  The inversion happens HERE, once, on the two
     `return` lines below, and it is the only place in the kernel that knows
     the two conventions differ.
   * The object works IN PLACE on one region; these entry points take separate
     input and output pointers.  So the input is copied to the output first and
     the object is asked to transform the output.  That is exact -- CTR is an
     XOR -- and it needs no bounce buffer: every caller here already owns an
     output region of the right size.
   * The object AUTHENTICATES BEFORE IT DECRYPTS.  The C this replaces
     decrypted first and compared the tag afterwards, so a forged record left
     attacker-chosen plaintext in the caller's buffer for as long as the caller
     took to look at the return value.  A refused record now leaves the
     CIPHERTEXT there instead. */

extern uint64_t kotoba_aiueos_aes128_gcm(uint8_t *, uint64_t, uint8_t *,
                                         uint64_t, uint64_t);

/* The object's one caller-owned region: key, nonce, AAD, tag and every scratch
   buffer it uses.  1280 bytes, laid out by aes128-gcm.kotoba's header.  It is
   `.bss` rather than a stack frame because the kernel's stacks are 4 KiB and
   this is a third of one. */
static uint8_t gcm_ctx[1280];

static void gcm_copy(uint8_t *d, const uint8_t *s, uint32_t n) {
  uint32_t i;
  if (d == s) return;
  for (i = 0; i < n; i++) d[i] = s[i];
}

static uint64_t gcm_call(const uint8_t key[16], const uint8_t nonce[12],
                         const uint8_t *aad, uint32_t aad_len,
                         uint8_t *data, uint32_t data_len, int seal) {
  uint32_t i;
  for (i = 0; i < sizeof(gcm_ctx); i++) gcm_ctx[i] = 0;
  gcm_copy(gcm_ctx, key, 16);
  gcm_copy(gcm_ctx + 16, nonce, 12);
  gcm_ctx[28] = (uint8_t)aad_len;
  gcm_copy(gcm_ctx + 64, aad, aad_len);
  return kotoba_aiueos_aes128_gcm(gcm_ctx, sizeof(gcm_ctx), data,
                                  (uint64_t)data_len, seal ? 1u : 0u);
}

int aiueos_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                              const uint8_t *aad, uint32_t aad_len,
                              const uint8_t *pt, uint32_t pt_len,
                              uint8_t *ct, uint8_t tag[16]) {
  uint32_t i;
  if (aad_len > 64) return 0;
  gcm_copy(ct, pt, pt_len);
  if (gcm_call(key, nonce, aad, aad_len, ct, pt_len, 1) != 0) return 0;
  for (i = 0; i < 16; i++) tag[i] = gcm_ctx[32 + i];
  return 1;
}

int aiueos_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                              const uint8_t *aad, uint32_t aad_len,
                              const uint8_t *ct, uint32_t ct_len,
                              const uint8_t tag[16], uint8_t *pt) {
  uint32_t i;
  if (aad_len > 64) return 0;
  gcm_copy(pt, ct, ct_len);
  for (i = 0; i < sizeof(gcm_ctx); i++) gcm_ctx[i] = 0;
  gcm_copy(gcm_ctx, key, 16);
  gcm_copy(gcm_ctx + 16, nonce, 12);
  gcm_ctx[28] = (uint8_t)aad_len;
  gcm_copy(gcm_ctx + 64, aad, aad_len);
  gcm_copy(gcm_ctx + 32, tag, 16);
  return kotoba_aiueos_aes128_gcm(gcm_ctx, sizeof(gcm_ctx), pt,
                                  (uint64_t)ct_len, 0) == 0;
}

int aiueos_aes128_gcm_selftest(void) {
  /* SP 800-38D Appendix A: AES-128, 96-bit IV, empty AAD, empty PT.
     Since ADR-0132 this is a KNOWN-ANSWER TEST ON THE OBJECT, run at boot on
     the machine that will use it -- which is a stronger claim than the one it
     made when the cipher was the C below it. */
  static const uint8_t key[16] = {0};
  static const uint8_t nonce[12] = {0};
  static const uint8_t expect_tag[16] = {
    0x58,0xe2,0xfc,0xce,0xfa,0x7e,0x30,0x61,
    0x36,0x7f,0x1d,0x57,0xa4,0xe7,0x45,0x5a};
  /* A zero-length body never reaches `kernel-subregion`, but the object is
     handed a real region rather than a null one: the contract records that a
     null base is a machine trap and not a reason code. */
  static uint8_t empty_region[16];
  uint8_t tag[16];
  uint32_t i, diff = 0;
  if (!aiueos_aes128_gcm_encrypt(key, nonce, 0, 0, empty_region, 0,
                                 empty_region, tag))
    return 0;
  for (i = 0; i < 16; i++) diff |= (uint32_t)(tag[i] ^ expect_tag[i]);
  if (diff) return 0;
  /* SP 800-38D: 64-byte PT, 96-bit IV, empty AAD. */
  {
    static const uint8_t k2[16] = {
      0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,
      0x6d,0x6a,0x8d,0x03,0x40,0x31,0x4d,0xe8};
    static const uint8_t n2[12] = {
      0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,0xde,0xca,0xf8,0x88};
    static const uint8_t pt[64] = {
      0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
      0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
      0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
      0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,0xba,0x63,0x7b,0x39,0x1a,0xaf,0xd2,0x55};
    /* Empty-AAD companion of the SP 800-38D 64-byte PT (the published
       appendix vector includes 20 bytes of AAD). Measured against
       cryptography.hazmat AESGCM. */
    static const uint8_t expect_ct[64] = {
      0x4f,0xaa,0x3c,0xcb,0xf0,0x7a,0x91,0xcf,0xdd,0xce,0x27,0xc8,0x57,0x8a,0xba,0xa1,
      0xed,0x73,0xbe,0xd1,0xc3,0xd0,0x8e,0xb8,0xb0,0x80,0x21,0xff,0x5d,0x35,0x49,0xd4,
      0xa2,0x82,0xd3,0xa8,0x58,0x35,0xe5,0x2d,0x2d,0xa9,0xe4,0x96,0x27,0x34,0x74,0xdc,
      0xcc,0xdb,0xc9,0xa2,0x86,0x61,0xdd,0x66,0xb8,0x7c,0x44,0x1b,0xd9,0xeb,0xbd,0xe7};
    static const uint8_t expect_t2[16] = {
      0x97,0xf7,0xb2,0x91,0x18,0xc8,0xfb,0x54,0x94,0x35,0x9b,0x19,0x1d,0x97,0x00,0x3e};
    static uint8_t ct[64], pt2[64];
    uint8_t t2[16];
    if (!aiueos_aes128_gcm_encrypt(k2, n2, 0, 0, pt, 64, ct, t2)) return 0;
    diff = 0;
    for (i = 0; i < 64; i++) diff |= (uint32_t)(ct[i] ^ expect_ct[i]);
    for (i = 0; i < 16; i++) diff |= (uint32_t)(t2[i] ^ expect_t2[i]);
    if (diff) return 0;
    if (!aiueos_aes128_gcm_decrypt(k2, n2, 0, 0, ct, 64, t2, pt2)) return 0;
    diff = 0;
    for (i = 0; i < 64; i++) diff |= (uint32_t)(pt2[i] ^ pt[i]);
    if (diff) return 0;
    /* A flipped tag bit must be REFUSED, and the buffer must still hold the
       ciphertext: authenticate-before-decrypt is a property of the object and
       this is the boot-time assertion of it. */
    {
      uint8_t bad[16];
      for (i = 0; i < 16; i++) bad[i] = t2[i];
      bad[15] = (uint8_t)(bad[15] ^ 1);
      if (aiueos_aes128_gcm_decrypt(k2, n2, 0, 0, ct, 64, bad, pt2)) return 0;
      diff = 0;
      for (i = 0; i < 64; i++) diff |= (uint32_t)(pt2[i] ^ ct[i]);
      if (diff) return 0;
    }
    /* AAD changes the tag only — the TLS record header is 5-byte AAD. */
    {
      static const uint8_t aad[20] = {
        0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xfe,0xed,
        0xfa,0xce,0xde,0xad,0xbe,0xef,0xab,0xad,0xda,0xd2};
      static const uint8_t expect_t3[16] = {
        0x2b,0x95,0x6e,0x81,0x82,0x06,0x64,0x24,0x2d,0x50,0x87,0x2b,0xa9,0xf5,0x21,0xee};
      uint8_t t3[16];
      if (!aiueos_aes128_gcm_encrypt(k2, n2, aad, 20, pt, 64, ct, t3)) return 0;
      diff = 0;
      for (i = 0; i < 16; i++) diff |= (uint32_t)(t3[i] ^ expect_t3[i]);
      if (diff) return 0;
    }
  }
  return 1;
}

#ifdef AIUEOS_TLS_AES_GCM_HOST_TEST
#include <stdio.h>
int main(void) {
  if (!aiueos_aes128_gcm_selftest()) {
    puts("AES-GCM selftest FAIL");
    return 1;
  }
  puts("AES-GCM selftest OK");
  return 0;
}
#endif
