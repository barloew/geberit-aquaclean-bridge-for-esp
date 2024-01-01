/*
 * arendi_server.c — Arendi server (peripheral) handshake + encrypted channel.
 * Presents the toilet's secure data channel to a connecting remote control.
 */
#include "arendi_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
static const char *TAG = "arendi.srv";

struct asrv_ctx {
  asrv_send_fn send;
  asrv_app_fn app_rx;
  void *user;
  asrv_state_t state;
  uint8_t tx_seq, rx_ack;
  uint8_t acc[1024];
  size_t acc_len;
  uint8_t nonce1[16], nonce2[16], auth_key[16];
  ac_x25519_t x;
  ac_aesctr_t tx, rx;
  bool keys;
};

static int send_frame(asrv_ctx_t *c, uint8_t ctrl, const uint8_t *pl, size_t n) {
  uint8_t out[700];
  size_t L = ac_build_frame(ctrl, pl, n, out);
  return c->send(out, L, c->user);
}
static int send_i(asrv_ctx_t *c, const uint8_t *pl, size_t n) {
  uint8_t ctrl = (uint8_t)(((c->rx_ack << 5) & 0xE0) | ((c->tx_seq << 1) & 0x0E));
  int r = send_frame(c, ctrl, pl, n);
  c->tx_seq = (c->tx_seq + 1) & 7;
  return r;
}
static void fail(asrv_ctx_t *c, const char *w) {
  ESP_LOGE(TAG, "handshake failed: %s", w);
  c->state = ASRV_FAILED;
}

static void on_frame(uint8_t ctrl, const uint8_t *pl, size_t plen, void *user) {
  asrv_ctx_t *c = user;
  bool is_i = (ctrl & 1) == 0, is_u = (ctrl & 3) == 3, is_s = (ctrl & 3) == 1;
  if (is_i)
    c->rx_ack = (((ctrl >> 1) & 7) + 1) & 7;
  else if (is_s)
    return;

  /* link setup: SABM -> UA */
  if (is_u && ctrl == ac_u_ctrl(HDLC_SABM_TYPE)) {
    c->tx_seq = 0;
    c->rx_ack = 0;
    send_frame(c, ac_u_ctrl(HDLC_UA_TYPE), NULL, 0);
    c->state = ASRV_VERSION;
    return;
  }
  if (!is_i || !plen) return;
  uint8_t sec = pl[0];

  if (c->state == ASRV_VERSION && sec == SEC_VERSION_REQ) {
    uint8_t body[] = SEC_VERSION_RESP_BODY;
    uint8_t resp[1 + sizeof(body)];
    resp[0] = SEC_VERSION_RESP;
    memcpy(resp + 1, body, sizeof(body));
    send_i(c, resp, sizeof(resp));
    c->state = ASRV_EP;
  } else if (c->state == ASRV_EP && sec == SEC_EP_REQ) {
    ac_random(c->nonce1, 16);
    ac_random(c->nonce2, 16);
    uint8_t resp[1 + 16 + 16 + 2];
    resp[0] = SEC_EP_RESP;
    memcpy(resp + 1, c->nonce1, 16);
    memcpy(resp + 17, c->nonce2, 16);
    resp[33] = KEYSET_MASK & 0xFF;
    resp[34] = (KEYSET_MASK >> 8) & 0xFF;
    send_i(c, resp, sizeof(resp));
    c->state = ASRV_KE;
  } else if (c->state == ASRV_KE && sec == SEC_KE_REQ && plen >= 49) {
    const uint8_t *cpub = pl + 1, *ccmac = pl + 33;
    if (ac_hkdf_sha256(c->nonce1, 16, AQUACLEAN_BRIDGE_ID, AQUACLEAN_BRIDGE_ID_LEN, c->auth_key, 16)) {
      fail(c, "hkdf");
      return;
    }
    uint8_t exp[16];
    if (ac_cmac16(c->auth_key, cpub, 32, exp) || memcmp(exp, ccmac, 16)) {
      fail(c, "client cmac");
      return;
    }
    if (ac_x25519_keygen(&c->x)) {
      fail(c, "keygen");
      return;
    }
    uint8_t scmac[16];
    if (ac_cmac16(c->auth_key, c->x.pub, 32, scmac)) {
      fail(c, "scmac");
      return;
    }
    uint8_t resp[1 + 32 + 16];
    resp[0] = SEC_KE_RESP;
    memcpy(resp + 1, c->x.pub, 32);
    memcpy(resp + 33, scmac, 16);
    send_i(c, resp, sizeof(resp));
    uint8_t sh[32], km[32];
    if (ac_x25519_shared(&c->x, cpub, sh)) {
      fail(c, "ecdh");
      return;
    }
    if (ac_hkdf_sha256(c->nonce1, 16, sh, 32, km, 32)) {
      fail(c, "hkdf km");
      return;
    }
    ac_aesctr_init(&c->tx, km + 0, c->nonce2);  /* server tx = client rx */
    ac_aesctr_init(&c->rx, km + 16, c->nonce2); /* server rx = client tx */
    c->keys = true;
    c->state = ASRV_DONE;
    ESP_LOGI(TAG, "remote handshake complete");
  } else if (c->state == ASRV_DONE && sec == SEC_ENCRYPTED) {
    uint8_t buf[600];
    size_t cl = plen - 1;
    if (cl > sizeof(buf)) return;
    memcpy(buf, pl + 1, cl);
    ac_aesctr_process(&c->rx, buf, cl);
    uint8_t app[600];
    int an = ac_inner_cobs_decode(buf, cl, app, sizeof(app));
    if (an >= 0 && c->app_rx) c->app_rx(app, (size_t)an, c->user);
  }
}

asrv_ctx_t *asrv_new(asrv_send_fn s, asrv_app_fn a, void *u) {
  asrv_ctx_t *c = calloc(1, sizeof(*c));
  if (!c) return NULL;
  c->send = s;
  c->app_rx = a;
  c->user = u;
  c->state = ASRV_IDLE;
  return c;
}
void asrv_free(asrv_ctx_t *c) {
  if (!c) return;
  if (c->keys) {
    ac_aesctr_free(&c->tx);
    ac_aesctr_free(&c->rx);
  }
  ac_x25519_free(&c->x);
  free(c);
}
void asrv_reset(asrv_ctx_t *c) {
  if (c->keys) {
    ac_aesctr_free(&c->tx);
    ac_aesctr_free(&c->rx);
    c->keys = false;
  }
  c->state = ASRV_LINK;
  c->acc_len = 0;
  c->tx_seq = c->rx_ack = 0;
}
void asrv_feed(asrv_ctx_t *c, const uint8_t *d, size_t n) {
  ac_rx_feed(c->acc, &c->acc_len, sizeof(c->acc), d, n, on_frame, c);
}
int asrv_send_app(asrv_ctx_t *c, const uint8_t *app, size_t n) {
  if (c->state != ASRV_DONE || !c->keys) return -1;
  uint8_t inner[600];
  size_t il = ac_inner_cobs_encode(app, n, inner);
  if (!il) return -1;
  ac_aesctr_process(&c->tx, inner, il);
  uint8_t sec[601];
  sec[0] = SEC_ENCRYPTED;
  memcpy(sec + 1, inner, il);
  return send_i(c, sec, il + 1);
}
asrv_state_t asrv_state(const asrv_ctx_t *c) { return c->state; }
