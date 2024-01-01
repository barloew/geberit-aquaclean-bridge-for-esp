/*
 * arendi_client.h — Arendi client (central) role: connect to the toilet and
 * drive SABM -> VERSION -> EP -> KE -> ENCRYPTED, then exchange Ble20 commands.
 */
#ifndef ARENDI_CLIENT_H
#define ARENDI_CLIENT_H
#include "arendi_common.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ACLI_IDLE,
  ACLI_WAIT_UA,
  ACLI_WAIT_VERSION,
  ACLI_WAIT_EP,
  ACLI_WAIT_KE,
  ACLI_DONE,
  ACLI_FAILED
} acli_state_t;

typedef int (*acli_send_fn)(const uint8_t *att, size_t len, void *user);
typedef void (*acli_app_fn)(const uint8_t *plaintext, size_t len, void *user);

typedef struct acli_ctx acli_ctx_t;

acli_ctx_t *acli_new(acli_send_fn send, acli_app_fn app_rx, void *user);
void acli_free(acli_ctx_t *c);
int acli_start(acli_ctx_t *c); /* sends SABM */
void acli_feed(acli_ctx_t *c, const uint8_t *data, size_t len);
int acli_send_app(acli_ctx_t *c, const uint8_t *app, size_t len);
int acli_unlock(acli_ctx_t *c, const char *pairing_secret_ascii);
acli_state_t acli_state(const acli_ctx_t *c);

#ifdef __cplusplus
}
#endif
#endif
