/* Host-only measurement of the same TLS 1.3 + GET path the guest uses.
   Not a P2 success: Mac sockets do not count. Compile:
     cc -O2 -DAIUEOS_TLS13_HOST_PROBE \
        os/aiueos/kernel/tls13.c os/aiueos/kernel/tls_aes_gcm.c \
        os/aiueos/kernel/tls13_host_probe.c -lcrypto -o /tmp/aiueos-tls13-host
*/
#ifdef AIUEOS_TLS13_HOST_PROBE
#include "tls13.h"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

uint64_t kotoba_aiueos_sha256(const uint8_t *in, uint64_t n, uint8_t out[32],
                              uint8_t *ws, uint64_t wsl) {
  static const uint8_t empty[1] = {0};
  (void)ws;
  (void)wsl;
  SHA256(n ? in : empty, (size_t)n, out);
  return 1;
}

uint64_t kotoba_aiueos_x25519(const uint8_t *scalar, const uint8_t *peer,
                              uint8_t *out, uint8_t *ws) {
  EVP_PKEY *priv = 0, *pub = 0;
  EVP_PKEY_CTX *ctx = 0;
  size_t outlen = 32;
  int ok = 0;
  (void)ws;
  priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, 0, scalar, 32);
  pub = priv ? EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, 0, peer, 32) : 0;
  ctx = pub ? EVP_PKEY_CTX_new(priv, 0) : 0;
  if (ctx && EVP_PKEY_derive_init(ctx) == 1 &&
      EVP_PKEY_derive_set_peer(ctx, pub) == 1 &&
      EVP_PKEY_derive(ctx, out, &outlen) == 1 && outlen == 32)
    ok = 1;
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(pub);
  EVP_PKEY_free(priv);
  return (uint64_t)ok;
}

uint64_t kotoba_aiueos_ecdsa_p256_sha256_verify(
    const uint8_t *sig, const uint8_t *digest, const uint8_t *pub,
    uint8_t *ws, uint64_t wslen) {
  EC_KEY *key = 0;
  BIGNUM *x = 0, *y = 0, *r = 0, *s = 0;
  ECDSA_SIG *esig = 0;
  int ok = 0;
  (void)ws;
  (void)wslen;
  key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  x = BN_bin2bn(pub, 32, 0);
  y = BN_bin2bn(pub + 32, 32, 0);
  r = BN_bin2bn(sig, 32, 0);
  s = BN_bin2bn(sig + 32, 32, 0);
  esig = ECDSA_SIG_new();
  if (key && x && y && r && s && esig &&
      EC_KEY_set_public_key_affine_coordinates(key, x, y) == 1 &&
      ECDSA_SIG_set0(esig, r, s) == 1) {
    r = 0;
    s = 0;
    ok = ECDSA_do_verify(digest, 32, esig, key) == 1;
  }
  BN_free(x);
  BN_free(y);
  BN_free(r);
  BN_free(s);
  ECDSA_SIG_free(esig);
  EC_KEY_free(key);
  return (uint64_t)ok;
}

static int tcp_kotobase(void) {
  struct addrinfo hints, *res = 0, *rp;
  int fd = -1;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_INET;
  if (getaddrinfo("kotobase.net", "443", &hints, &res)) return -1;
  for (rp = res; rp; rp = rp->ai_next) {
    fd = (int)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

int main(void) {
  uint8_t ch[160], flight[512], rx[8192];
  uint32_t ch_len = 0, fin_len = 0, get_len = 0;
  int fd, n, i;
  if (!aiueos_tls13_aes_selftest() || !aiueos_tls13_hmac_selftest() ||
      !aiueos_tls13_ecdsa_selftest()) {
    puts("selftest fail");
    return 1;
  }
  fd = tcp_kotobase();
  if (fd < 0) {
    puts("connect fail");
    return 1;
  }
  aiueos_tls13_reset();
  if (!aiueos_tls13_clienthello(ch, &ch_len) ||
      send(fd, ch, ch_len, 0) != (ssize_t)ch_len) {
    puts("clienthello fail");
    return 1;
  }
  for (i = 0; i < 64 && !aiueos_tls13_handshake_ready(); i++) {
    n = (int)recv(fd, rx, sizeof(rx), 0);
    if (n <= 0) break;
    if (!aiueos_tls13_feed(rx, (uint32_t)n)) {
      printf("feed fail stage=%u rec=%u inner=%u\n",
             aiueos_tls13_stage(), aiueos_tls13_last_record_type(),
             aiueos_tls13_last_inner_type());
      return 1;
    }
  }
  if (!aiueos_tls13_handshake_ready()) {
    printf("handshake incomplete stage=%u rec=%u certverify=%d\n",
           aiueos_tls13_stage(), aiueos_tls13_last_record_type(),
           aiueos_tls13_certverify_ok());
    return 1;
  }
  if (!aiueos_tls13_run_certverify() ||
      !aiueos_tls13_certverify_ok() ||
      aiueos_tls13_certverify_scheme() != 0x0403) {
    printf("certverify fail scheme=%u\n", aiueos_tls13_certverify_scheme());
    return 1;
  }
  puts("handshake ok");
  puts("certverify ok scheme=ecdsa_secp256r1_sha256");
  if (!aiueos_tls13_take_finished(flight, &fin_len) ||
      !aiueos_tls13_take_http(flight + fin_len, &get_len)) {
    puts("take fail");
    return 1;
  }
  printf("fin=%u get=%u\n", fin_len, get_len);
  if (send(fd, flight, fin_len + get_len, 0) != (ssize_t)(fin_len + get_len)) {
    puts("send fin+get fail");
    return 1;
  }
  for (i = 0; i < 64; i++) {
    n = (int)recv(fd, rx, sizeof(rx), 0);
    if (n <= 0) break;
    if (!aiueos_tls13_feed(rx, (uint32_t)n)) {
      printf("app feed fail rec=%u inner=%u fail=%d alert=%u/%u app=%u\n",
             aiueos_tls13_last_record_type(), aiueos_tls13_last_inner_type(),
             aiueos_tls13_failed(), aiueos_tls13_alert_level(),
             aiueos_tls13_alert_desc(), aiueos_tls13_app_len());
      if (aiueos_tls13_app_len() >= 12 && aiueos_tls13_app()[0] == 'H') {
        puts("http before alert");
        close(fd);
        return 0;
      }
      return 1;
    }
    if (aiueos_tls13_app_len() >= 12) {
      uint32_t k, shown = aiueos_tls13_app_len();
      if (shown > 80) shown = 80;
      printf("app len=%u nst=%u first=", aiueos_tls13_app_len(),
             aiueos_tls13_nst_count());
      for (k = 0; k < shown; k++) {
        unsigned c = aiueos_tls13_app()[k];
        putchar((c >= 32 && c < 127) ? (int)c : '.');
      }
      putchar('\n');
      close(fd);
      return (aiueos_tls13_app()[0] == 'H') ? 0 : 2;
    }
  }
  printf("no http app=%u nst=%u rec=%u inner=%u fail=%d\n",
         aiueos_tls13_app_len(), aiueos_tls13_nst_count(),
         aiueos_tls13_last_record_type(), aiueos_tls13_last_inner_type(),
         aiueos_tls13_failed());
  close(fd);
  return 1;
}
#endif
