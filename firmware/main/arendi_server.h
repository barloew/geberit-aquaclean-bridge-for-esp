/*
 * arendi_server.h — Arendi server (peripheral) role: present the toilet's data
 * channel to a connecting remote control and run the server side of the
 * handshake (UA / VERSION_RESP / EP_RESP / KE_RESP), then the encrypted channel.
 */
#ifndef ARENDI_SERVER_H
#define ARENDI_SERVER_H
#include "arendi_common.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ASRV_IDLE, ASRV_LINK, ASRV_VERSION, ASRV_EP, ASRV_KE, ASRV_DONE, ASRV_FAILED } asrv_state_t;

/* Send a notification value on 559eb002 to the connected remote. */
typedef int (*asrv_send_fn)(const uint8_t *att, size_t len, void *user);
/* Called with each decrypted Ble20 app frame from the remote. */
typedef void (*asrv_app_fn)(const uint8_t *plaintext, size_t len, void *user);

typedef struct asrv_ctx asrv_ctx_t;

asrv_ctx_t *asrv_new(asrv_send_fn send, asrv_app_fn app_rx, void *user);
void asrv_free(asrv_ctx_t *c);
void asrv_reset(asrv_ctx_t *c);                                 /* on new remote connection */
void asrv_feed(asrv_ctx_t *c, const uint8_t *data, size_t len); /* writes on 559eb001 */
int asrv_send_app(asrv_ctx_t *c, const uint8_t *app, size_t len);
asrv_state_t asrv_state(const asrv_ctx_t *c);

#ifdef __cplusplus
}
#endif
#endif
