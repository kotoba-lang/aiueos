#include "tls_aes_gcm.h"

/* Compact AES-128 and GHASH. No libc. Loops only; the tables are the
   published FIPS 197 S-box and the GF(2^8) reduction is xtime. */

static const uint8_t sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[10] = {
  0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static uint8_t xtime(uint8_t x) {
  return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void aes_key_expand(const uint8_t key[16], uint8_t rk[176]) {
  uint32_t i;
  for (i = 0; i < 16; i++) rk[i] = key[i];
  for (i = 16; i < 176; i += 4) {
    uint8_t t0 = rk[i - 4], t1 = rk[i - 3], t2 = rk[i - 2], t3 = rk[i - 1];
    if ((i % 16) == 0) {
      uint8_t k = t0;
      t0 = (uint8_t)(sbox[t1] ^ rcon[(i / 16) - 1]);
      t1 = sbox[t2];
      t2 = sbox[t3];
      t3 = sbox[k];
    }
    rk[i] = (uint8_t)(rk[i - 16] ^ t0);
    rk[i + 1] = (uint8_t)(rk[i - 15] ^ t1);
    rk[i + 2] = (uint8_t)(rk[i - 14] ^ t2);
    rk[i + 3] = (uint8_t)(rk[i - 13] ^ t3);
  }
}

static void aes_encrypt_block(const uint8_t rk[176], const uint8_t in[16],
                              uint8_t out[16]) {
  uint8_t s[16];
  uint32_t round, i;
  for (i = 0; i < 16; i++) s[i] = (uint8_t)(in[i] ^ rk[i]);
  for (round = 1; round < 10; round++) {
    uint8_t t[16];
    for (i = 0; i < 16; i++) t[i] = sbox[s[i]];
    s[0] = t[0];  s[1] = t[5];  s[2] = t[10]; s[3] = t[15];
    s[4] = t[4];  s[5] = t[9];  s[6] = t[14]; s[7] = t[3];
    s[8] = t[8];  s[9] = t[13]; s[10] = t[2]; s[11] = t[7];
    s[12] = t[12]; s[13] = t[1]; s[14] = t[6]; s[15] = t[11];
    for (i = 0; i < 16; i += 4) {
      uint8_t a = s[i], b = s[i + 1], c = s[i + 2], d = s[i + 3];
      s[i]     = (uint8_t)(xtime(a) ^ xtime(b) ^ b ^ c ^ d);
      s[i + 1] = (uint8_t)(a ^ xtime(b) ^ xtime(c) ^ c ^ d);
      s[i + 2] = (uint8_t)(a ^ b ^ xtime(c) ^ xtime(d) ^ d);
      s[i + 3] = (uint8_t)(xtime(a) ^ a ^ b ^ c ^ xtime(d));
    }
    for (i = 0; i < 16; i++) s[i] ^= rk[round * 16 + i];
  }
  {
    uint8_t t[16];
    for (i = 0; i < 16; i++) t[i] = sbox[s[i]];
    s[0] = t[0];  s[1] = t[5];  s[2] = t[10]; s[3] = t[15];
    s[4] = t[4];  s[5] = t[9];  s[6] = t[14]; s[7] = t[3];
    s[8] = t[8];  s[9] = t[13]; s[10] = t[2]; s[11] = t[7];
    s[12] = t[12]; s[13] = t[1]; s[14] = t[6]; s[15] = t[11];
  }
  for (i = 0; i < 16; i++) out[i] = (uint8_t)(s[i] ^ rk[160 + i]);
}

static void ghash_mult(uint8_t x[16], const uint8_t y[16]) {
  uint8_t z[16], v[16];
  uint32_t i, j;
  for (i = 0; i < 16; i++) {
    z[i] = 0;
    v[i] = y[i];
  }
  for (i = 0; i < 16; i++) {
    for (j = 0; j < 8; j++) {
      uint8_t bit = (uint8_t)((x[i] >> (7 - j)) & 1);
      uint32_t k;
      uint8_t lsb = (uint8_t)(v[15] & 1);
      if (bit) {
        for (k = 0; k < 16; k++) z[k] ^= v[k];
      }
      for (k = 15; k > 0; k--) v[k] = (uint8_t)((v[k] >> 1) | (v[k - 1] << 7));
      v[0] >>= 1;
      if (lsb) v[0] ^= 0xe1;
    }
  }
  for (i = 0; i < 16; i++) x[i] = z[i];
}

static void ghash_update(uint8_t y[16], const uint8_t h[16],
                         const uint8_t *data, uint32_t len) {
  uint32_t off = 0;
  while (off < len) {
    uint8_t block[16];
    uint32_t n = len - off;
    uint32_t i;
    if (n > 16) n = 16;
    for (i = 0; i < 16; i++) block[i] = 0;
    for (i = 0; i < n; i++) block[i] = data[off + i];
    for (i = 0; i < 16; i++) y[i] ^= block[i];
    ghash_mult(y, h);
    off += n;
  }
}

static void store_be64(uint8_t *at, uint64_t v) {
  at[0] = (uint8_t)(v >> 56); at[1] = (uint8_t)(v >> 48);
  at[2] = (uint8_t)(v >> 40); at[3] = (uint8_t)(v >> 32);
  at[4] = (uint8_t)(v >> 24); at[5] = (uint8_t)(v >> 16);
  at[6] = (uint8_t)(v >> 8);  at[7] = (uint8_t)v;
}

static void inc32(uint8_t ctr[16]) {
  uint32_t n = ((uint32_t)ctr[12] << 24) | ((uint32_t)ctr[13] << 16) |
               ((uint32_t)ctr[14] << 8) | ctr[15];
  n++;
  ctr[12] = (uint8_t)(n >> 24); ctr[13] = (uint8_t)(n >> 16);
  ctr[14] = (uint8_t)(n >> 8);  ctr[15] = (uint8_t)n;
}

static void gcm_crypt(const uint8_t rk[176], uint8_t ctr[16],
                      const uint8_t *in, uint8_t *out, uint32_t len) {
  uint32_t off = 0;
  while (off < len) {
    uint8_t ks[16];
    uint32_t n = len - off;
    uint32_t i;
    if (n > 16) n = 16;
    inc32(ctr);
    aes_encrypt_block(rk, ctr, ks);
    for (i = 0; i < n; i++) out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
    off += n;
  }
}

static int gcm_run(const uint8_t key[16], const uint8_t nonce[12],
                   const uint8_t *aad, uint32_t aad_len,
                   const uint8_t *in, uint32_t len, uint8_t *out,
                   uint8_t tag[16], int encrypt) {
  uint8_t rk[176], h[16], j0[16], ctr[16], y[16], s[16], lenblk[16];
  uint32_t i;
  if (len > 12288 || aad_len > 64) return 0;
  aes_key_expand(key, rk);
  for (i = 0; i < 16; i++) h[i] = 0;
  aes_encrypt_block(rk, h, h);
  for (i = 0; i < 12; i++) j0[i] = nonce[i];
  j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
  for (i = 0; i < 16; i++) ctr[i] = j0[i];
  for (i = 0; i < 16; i++) y[i] = 0;
  if (encrypt) {
    gcm_crypt(rk, ctr, in, out, len);
    ghash_update(y, h, aad, aad_len);
    ghash_update(y, h, out, len);
  } else {
    ghash_update(y, h, aad, aad_len);
    ghash_update(y, h, in, len);
    gcm_crypt(rk, ctr, in, out, len);
  }
  for (i = 0; i < 16; i++) lenblk[i] = 0;
  store_be64(lenblk, (uint64_t)aad_len * 8);
  store_be64(lenblk + 8, (uint64_t)len * 8);
  for (i = 0; i < 16; i++) y[i] ^= lenblk[i];
  ghash_mult(y, h);
  aes_encrypt_block(rk, j0, s);
  for (i = 0; i < 16; i++) tag[i] = (uint8_t)(s[i] ^ y[i]);
  return 1;
}

int aiueos_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                              const uint8_t *aad, uint32_t aad_len,
                              const uint8_t *pt, uint32_t pt_len,
                              uint8_t *ct, uint8_t tag[16]) {
  return gcm_run(key, nonce, aad, aad_len, pt, pt_len, ct, tag, 1);
}

int aiueos_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                              const uint8_t *aad, uint32_t aad_len,
                              const uint8_t *ct, uint32_t ct_len,
                              const uint8_t tag[16], uint8_t *pt) {
  uint8_t got[16];
  uint32_t i, diff = 0;
  if (!gcm_run(key, nonce, aad, aad_len, ct, ct_len, pt, got, 0)) return 0;
  for (i = 0; i < 16; i++) diff |= (uint32_t)(got[i] ^ tag[i]);
  return diff == 0;
}

int aiueos_aes128_gcm_selftest(void) {
  /* SP 800-38D Appendix A: AES-128, 96-bit IV, empty AAD, empty PT. */
  static const uint8_t key[16] = {0};
  static const uint8_t nonce[12] = {0};
  static const uint8_t expect_tag[16] = {
    0x58,0xe2,0xfc,0xce,0xfa,0x7e,0x30,0x61,
    0x36,0x7f,0x1d,0x57,0xa4,0xe7,0x45,0x5a};
  uint8_t tag[16];
  uint32_t i, diff = 0;
  if (!aiueos_aes128_gcm_encrypt(key, nonce, 0, 0, 0, 0, 0, tag)) return 0;
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
    uint8_t ct[64], pt2[64], t2[16];
    if (!aiueos_aes128_gcm_encrypt(k2, n2, 0, 0, pt, 64, ct, t2)) return 0;
    diff = 0;
    for (i = 0; i < 64; i++) diff |= (uint32_t)(ct[i] ^ expect_ct[i]);
    for (i = 0; i < 16; i++) diff |= (uint32_t)(t2[i] ^ expect_t2[i]);
    if (diff) return 0;
    if (!aiueos_aes128_gcm_decrypt(k2, n2, 0, 0, ct, 64, t2, pt2)) return 0;
    diff = 0;
    for (i = 0; i < 64; i++) diff |= (uint32_t)(pt2[i] ^ pt[i]);
    if (diff) return 0;
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
