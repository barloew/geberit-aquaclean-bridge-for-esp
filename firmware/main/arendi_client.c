/*
 * arendi_client.c — Arendi client (central) handshake + encrypted channel.
 */
#include "arendi_client.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
static const char *TAG = "arendi.cli";

struct acli_ctx {
  acli_send_fn send;
  acli_app_fn app_rx;
  void *user;
  acli_state_t state;
  uint8_t tx_seq, rx_ack;
  uint8_t acc[1024];
  size_t acc_len;
  uint8_t nonce1[16], nonce2[16], auth_key[16];
  ac_x25519_t x;
  ac_aesctr_t tx, rx;
  bool keys;
};

static int send_frame(acli_ctx_t *c, uint8_t ctrl, const uint8_t *pl, size_t n) {
  uint8_t out[700];
  size_t L = ac_build_frame(ctrl, pl, n, out);
  return c->send(out, L, c->user);
}
static int send_i(acli_ctx_t *c, const uint8_t *pl, size_t n) {
  uint8_t ctrl = (uint8_t)(((c->rx_ack << 5) & 0xE0) | ((c->tx_seq << 1) & 0x0E));
  int r = send_frame(c, ctrl, pl, n);
  c->tx_seq = (c->tx_seq + 1) & 7;
  return r;
}
static void fail(acli_ctx_t *c, const char *w) {
  ESP_LOGE(TAG, "handshake failed: %s", w);
  c->state = ACLI_FAILED;
}

static void on_frame(uint8_t ctrl, const uint8_t *pl, size_t plen, void *user) {
  acli_ctx_t *c = user;
  bool is_i = (ctrl & 1) == 0, is_u = (ctrl & 3) == 3, is_s = (ctrl & 3) == 1;
  if (is_i)
    c->rx_ack = (((ctrl >> 1) & 7) + 1) & 7;
  else if (is_s)
    return;

  switch (c->state) {
    case ACLI_WAIT_UA:
      if (is_u && ctrl == ac_u_ctrl(HDLC_UA_TYPE)) {
        uint8_t p = SEC_VERSION_REQ;
        send_i(c, &p, 1);
        c->state = ACLI_WAIT_VERSION;
      }
      break;
    case ACLI_WAIT_VERSION:
      if (is_i && plen && pl[0] == SEC_VERSION_RESP) {
        uint8_t p = SEC_EP_REQ;
        send_i(c, &p, 1);
        c->state = ACLI_WAIT_EP;
      }
      break;
    case ACLI_WAIT_EP:
      if (is_i && plen >= 33 && pl[0] == SEC_EP_RESP) {
        memcpy(c->nonce1, pl + 1, 16);
        memcpy(c->nonce2, pl + 17, 16);
        if (ac_x25519_keygen(&c->x)) {
          fail(c, "keygen");
          return;
        }
        if (ac_hkdf_sha256(c->nonce1, 16, AQUACLEAN_BRIDGE_ID, AQUACLEAN_BRIDGE_ID_LEN, c->auth_key, 16)) {
          fail(c, "hkdf");
          return;
        }
        uint8_t cmac[16];
        if (ac_cmac16(c->auth_key, c->x.pub, 32, cmac)) {
          fail(c, "cmac");
          return;
        }
        uint8_t ke[1 + 32 + 16 + 1];
        ke[0] = SEC_KE_REQ;
        memcpy(ke + 1, c->x.pub, 32);
        memcpy(ke + 33, cmac, 16);
        ke[49] = 0x00;
        send_i(c, ke, sizeof(ke));
        c->state = ACLI_WAIT_KE;
      }
      break;
    case ACLI_WAIT_KE:
      if (is_i && plen >= 49 && pl[0] == SEC_KE_RESP) {
        const uint8_t *spub = pl + 1, *scmac = pl + 33;
        uint8_t exp[16];
        if (ac_cmac16(c->auth_key, spub, 32, exp) || memcmp(exp, scmac, 16)) {
          fail(c, "server cmac");
          return;
        }
        uint8_t sh[32], km[32];
        if (ac_x25519_shared(&c->x, spub, sh)) {
          fail(c, "ecdh");
          return;
        }
        if (ac_hkdf_sha256(c->nonce1, 16, sh, 32, km, 32)) {
          fail(c, "hkdf km");
          return;
        }
        ac_aesctr_init(&c->tx, km + 16, c->nonce2); /* client tx = server rx */
        ac_aesctr_init(&c->rx, km + 0, c->nonce2);  /* client rx = server tx */
        c->keys = true;
        c->state = ACLI_DONE;
        ESP_LOGI(TAG, "handshake complete");
      }
      break;
    case ACLI_DONE:
      if (is_i && plen && pl[0] == SEC_ENCRYPTED) {
        uint8_t buf[600];
        size_t cl = plen - 1;
        if (cl > sizeof(buf)) return;
        memcpy(buf, pl + 1, cl);
        ac_aesctr_process(&c->rx, buf, cl);
        uint8_t app[600];
        int an = ac_inner_cobs_decode(buf, cl, app, sizeof(app));
        if (an >= 0 && c->app_rx) c->app_rx(app, (size_t)an, c->user);
      }
      break;
    default:
      break;
  }
}

acli_ctx_t *acli_new(acli_send_fn s, acli_app_fn a, void *u) {
  acli_ctx_t *c = calloc(1, sizeof(*c));
  if (!c) return NULL;
  c->send = s;
  c->app_rx = a;
  c->user = u;
  c->state = ACLI_IDLE;
  return c;
}
void acli_free(acli_ctx_t *c) {
  if (!c) return;
  if (c->keys) {
    ac_aesctr_free(&c->tx);
    ac_aesctr_free(&c->rx);
  }
  ac_x25519_free(&c->x);
  free(c);
}
int acli_start(acli_ctx_t *c) {
  c->tx_seq = c->rx_ack = 0;
  c->acc_len = 0;
  c->keys = false;
  c->state = ACLI_WAIT_UA;
  return send_frame(c, ac_u_ctrl(HDLC_SABM_TYPE), NULL, 0);
}
void acli_feed(acli_ctx_t *c, const uint8_t *d, size_t n) {
  ac_rx_feed(c->acc, &c->acc_len, sizeof(c->acc), d, n, on_frame, c);
}
int acli_send_app(acli_ctx_t *c, const uint8_t *app, size_t n) {
  if (c->state != ACLI_DONE || !c->keys) return -1;
  uint8_t inner[600];
  size_t il = ac_inner_cobs_encode(app, n, inner);
  if (!il) return -1;
  ac_aesctr_process(&c->tx, inner, il);
  uint8_t sec[601];
  sec[0] = SEC_ENCRYPTED;
  memcpy(sec + 1, inner, il);
  return send_i(c, sec, il + 1);
}
int acli_unlock(acli_ctx_t *c, const char *pin) {
  if (c->state != ACLI_DONE) return -1;
  size_t n = pin ? strlen(pin) : 0;
  if (n > 32) return -1;
  uint8_t app[33];
  app[0] = CMD_UNLOCK;
  memcpy(app + 1, pin, n);
  return acli_send_app(c, app, 1 + n);
}
acli_state_t acli_state(const acli_ctx_t *c) { return c->state; }
