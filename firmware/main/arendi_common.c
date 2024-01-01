/*
 * arendi_common.c — shared Arendi framing and crypto (mbedTLS).
 */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS /* reach ecp_point .X/.Z on mbedTLS 3.x */
#include "arendi_common.h"
#include <string.h>
#include "esp_random.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/cmac.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/md.h"

/* aquacleanBridgeId: the shared Geberit client credential that keys the handshake authentication.
 * Its definition lives in aquaclean_bridge_id.c, which is git-ignored and never published. Copy
 * aquaclean_bridge_id.example.c to aquaclean_bridge_id.c and fill in the 16 bytes before building
 * (see firmware/README.md). Without it the v2 handshake will not authenticate. */

void ac_random(uint8_t *out, size_t len) { esp_fill_random(out, len); }
static int rng_cb(void *p, unsigned char *o, size_t n) {
  (void)p;
  esp_fill_random(o, n);
  return 0;
}

/* ---------- CRC16 / Kermit (poly 0x1021 reflected, init 0, LE in frame) ---- */
uint16_t ac_crc16_kermit(const uint8_t *d, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
  }
  return crc;
}

/* ---------- COBS ---------- */
size_t ac_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst) {
  size_t rp = 0, wp = 1, ci = 0;
  uint8_t code = 1;
  while (rp < len) {
    if (src[rp] == 0) {
      dst[ci] = code;
      code = 1;
      ci = wp++;
      rp++;
    } else {
      dst[wp++] = src[rp++];
      code++;
      if (code == 0xFF) {
        dst[ci] = code;
        code = 1;
        ci = wp++;
      }
    }
  }
  dst[ci] = code;
  return wp;
}
int ac_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstcap) {
  size_t rp = 0, wp = 0;
  while (rp < len) {
    uint8_t code = src[rp++];
    if (code == 0) return -1;
    for (uint8_t i = 1; i < code; i++) {
      if (rp >= len || wp >= dstcap) return -1;
      dst[wp++] = src[rp++];
    }
    if (code != 0xFF && rp < len) {
      if (wp >= dstcap) return -1;
      dst[wp++] = 0;
    }
  }
  return (int)wp;
}
size_t ac_inner_cobs_encode(const uint8_t *data, size_t len, uint8_t *out) {
  uint8_t tmp[600];
  if (len + 2 > sizeof(tmp)) return 0;
  memcpy(tmp, data, len);
  uint16_t c = ac_crc16_kermit(data, len);
  tmp[len] = c & 0xFF;
  tmp[len + 1] = (c >> 8) & 0xFF;
  out[0] = 0;
  size_t n = ac_cobs_encode(tmp, len + 2, out + 1);
  out[1 + n] = 0;
  return n + 2;
}
int ac_inner_cobs_decode(const uint8_t *frame, size_t len, uint8_t *out, size_t outcap) {
  size_t s = 0, e = len;
  if (s < e && frame[s] == 0) s++;
  if (e > s && frame[e - 1] == 0) e--;
  int dn = ac_cobs_decode(frame + s, e - s, out, outcap);
  if (dn < 3) return -1;
  if ((out[dn - 2] | (out[dn - 1] << 8)) != ac_crc16_kermit(out, dn - 2)) return -1;
  return dn - 2;
}

uint8_t ac_u_ctrl(int tc) { return (uint8_t)(((tc << 3) & 0xE0) | ((tc << 2) & 0x0C) | 0x03); }
size_t ac_build_frame(uint8_t ctrl, const uint8_t *payload, size_t plen, uint8_t *out) {
  uint8_t raw[600];
  if (1 + plen + 2 > sizeof(raw)) return 0;
  raw[0] = ctrl;
  if (plen) memcpy(raw + 1, payload, plen);
  uint16_t c = ac_crc16_kermit(raw, 1 + plen);
  raw[1 + plen] = c & 0xFF;
  raw[2 + plen] = (c >> 8) & 0xFF;
  out[0] = 0;
  size_t n = ac_cobs_encode(raw, 1 + plen + 2, out + 1);
  out[1 + n] = 0;
  return n + 2;
}

void ac_rx_feed(uint8_t *acc, size_t *acc_len, size_t cap, const uint8_t *data, size_t len, ac_frame_cb cb,
                void *user) {
  if (*acc_len + len > cap) *acc_len = 0;
  memcpy(acc + *acc_len, data, len);
  *acc_len += len;
  for (;;) {
    size_t s = 0;
    while (s < *acc_len && acc[s] != 0) s++;
    if (s >= *acc_len) {
      *acc_len = 0;
      return;
    }
    size_t e = s + 1;
    while (e < *acc_len && acc[e] != 0) e++;
    if (e >= *acc_len) {
      memmove(acc, acc + s, *acc_len - s);
      *acc_len -= s;
      return;
    }
    uint8_t dec[600];
    int dn = ac_cobs_decode(acc + s + 1, e - (s + 1), dec, sizeof(dec));
    memmove(acc, acc + e, *acc_len - e);
    *acc_len -= e;
    if (dn < 3) continue;
    if ((dec[dn - 2] | (dec[dn - 1] << 8)) != ac_crc16_kermit(dec, dn - 2)) continue;
    cb(dec[0], dec + 1, (size_t)dn - 3, user);
  }
}

/* ---------- crypto ---------- */
int ac_hkdf_sha256(const uint8_t *salt, size_t sl, const uint8_t *ikm, size_t il, uint8_t *okm, size_t ol) {
  const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return mbedtls_hkdf(md, salt, sl, ikm, il, NULL, 0, okm, ol);
}
int ac_cmac16(const uint8_t key[16], const uint8_t *msg, size_t ml, uint8_t out[16]) {
  const mbedtls_cipher_info_t *ci = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
  return mbedtls_cipher_cmac(ci, key, 128, msg, ml, out);
}
int ac_x25519_keygen(ac_x25519_t *x) {
  mbedtls_ecp_group_init(&x->grp);
  mbedtls_mpi_init(&x->d);
  mbedtls_ecp_group_load(&x->grp, MBEDTLS_ECP_DP_CURVE25519);
  mbedtls_ecp_point Q;
  mbedtls_ecp_point_init(&Q);
  int rc = mbedtls_ecp_gen_keypair(&x->grp, &x->d, &Q, rng_cb, NULL);
  if (rc == 0) rc = mbedtls_mpi_write_binary_le(&Q.MBEDTLS_PRIVATE(X), x->pub, 32);
  mbedtls_ecp_point_free(&Q);
  return rc;
}
int ac_x25519_shared(ac_x25519_t *x, const uint8_t peer[32], uint8_t out[32]) {
  mbedtls_ecp_point P;
  mbedtls_ecp_point_init(&P);
  mbedtls_mpi z;
  mbedtls_mpi_init(&z);
  int rc = mbedtls_mpi_read_binary_le(&P.MBEDTLS_PRIVATE(X), peer, 32);
  if (rc == 0) rc = mbedtls_mpi_lset(&P.MBEDTLS_PRIVATE(Z), 1);
  if (rc == 0) rc = mbedtls_ecdh_compute_shared(&x->grp, &z, &P, &x->d, rng_cb, NULL);
  if (rc == 0) rc = mbedtls_mpi_write_binary_le(&z, out, 32);
  mbedtls_mpi_free(&z);
  mbedtls_ecp_point_free(&P);
  return rc;
}
void ac_x25519_free(ac_x25519_t *x) {
  mbedtls_mpi_free(&x->d);
  mbedtls_ecp_group_free(&x->grp);
}

void ac_aesctr_init(ac_aesctr_t *s, const uint8_t key[16], const uint8_t nonce[16]) {
  mbedtls_aes_init(&s->aes);
  mbedtls_aes_setkey_enc(&s->aes, key, 128);
  memcpy(s->ctr, nonce, 16);
  memset(s->sb, 0, 16);
  s->off = 0;
}
void ac_aesctr_process(ac_aesctr_t *s, uint8_t *buf, size_t len) {
  mbedtls_aes_crypt_ctr(&s->aes, len, &s->off, s->ctr, s->sb, buf, buf);
}
void ac_aesctr_free(ac_aesctr_t *s) { mbedtls_aes_free(&s->aes); }
