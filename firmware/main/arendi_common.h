/*
 * arendi_common.h — shared Arendi protocol constants, framing and crypto.
 *
 * The Arendi secure channel (protocol v2) runs over BLE GATT:
 *   HDLC framing + COBS byte-stuffing + CRC16/Kermit, carrying a security
 *   sub-protocol: VERSION -> EP (nonces) -> KE (X25519 + AES-CMAC) -> ENCRYPTED
 *   (AES-128-CTR). This header is used by both the client (central) and the
 *   server (peripheral) roles.
 */
#ifndef ARENDI_COMMON_H
#define ARENDI_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "mbedtls/aes.h"
#include "mbedtls/ecp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HDLC U-frame type codes (fed to ac_u_ctrl): SABM=0x2F, UA=0x63, DISC=0x43 */
#define HDLC_SABM_TYPE 7
#define HDLC_UA_TYPE 12
#define HDLC_DISC_TYPE 8

/* ---- Security sub-protocol type bytes (first byte of an I-frame payload) */
#define SEC_VERSION_REQ 0x00
#define SEC_VERSION_RESP 0x01
#define SEC_EP_REQ 0x10
#define SEC_EP_RESP 0x11
#define SEC_KE_REQ 0x12
#define SEC_KE_RESP 0x13
#define SEC_ENCRYPTED 0x20

/* VERSION_RESP body advertising protocol v2 */
#define SEC_VERSION_RESP_BODY {0x00, 0x05, 0x00, 0x00, 0x00, 0x01}

/* Key sets the peripheral advertises in EP_RESP (bit0 + bit1) */
#define KEYSET_MASK 0x0003

/* ---- Ble20 application-layer command IDs (first byte of a decrypted app frame) */
#define CMD_INVENTORY 0x00
#define CMD_READ 0x10
#define CMD_WRITE 0x20
#define CMD_NOTIFY_ENABLE 0x30
#define CMD_UNLOCK 0x40
#define CMD_UNLOCK_ACK 0x41
#define CMD_UNLOCK_ERROR 0x42

/* ---- GATT identifiers of the AquaClean data channel */
#define GEBERIT_SVC_UUID16 0xFD48 /* service */
/* 559eb001-2390-11e8-b467-0ed5f89f718b  write (no response) */
/* 559eb002-2390-11e8-b467-0ed5f89f718b  notify                */

/* ---- aquacleanBridgeId: HKDF input keying the AES-CMAC that authenticates the
 *      KE public keys. Shared Geberit-client credential. */
#define AQUACLEAN_BRIDGE_ID_LEN 16
extern const uint8_t AQUACLEAN_BRIDGE_ID[AQUACLEAN_BRIDGE_ID_LEN];

/* ===================== framing ===================== */
uint16_t ac_crc16_kermit(const uint8_t *d, size_t n);
size_t ac_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst);
int ac_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstcap);
size_t ac_inner_cobs_encode(const uint8_t *data, size_t len, uint8_t *out);
int ac_inner_cobs_decode(const uint8_t *frame, size_t len, uint8_t *out, size_t outcap);
uint8_t ac_u_ctrl(int type_code);
/* Build one ATT frame: 0x00 + COBS(ctrl + payload + crc16_le) + 0x00. Returns len. */
size_t ac_build_frame(uint8_t ctrl, const uint8_t *payload, size_t plen, uint8_t *out);

/* Accumulate incoming bytes and pull out complete 0x00-delimited, CRC-checked
 * frames. `acc`/`acc_len` is caller-owned state (size >= 1024). Calls `cb(ctrl,
 * payload, plen, user)` per valid frame. */
typedef void (*ac_frame_cb)(uint8_t ctrl, const uint8_t *pl, size_t plen, void *user);
void ac_rx_feed(uint8_t *acc, size_t *acc_len, size_t acc_cap, const uint8_t *data, size_t len, ac_frame_cb cb,
                void *user);

/* ===================== crypto ===================== */
int ac_hkdf_sha256(const uint8_t *salt, size_t slen, const uint8_t *ikm, size_t ilen, uint8_t *okm, size_t olen);
int ac_cmac16(const uint8_t key[16], const uint8_t *msg, size_t mlen, uint8_t out[16]);

typedef struct {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  uint8_t pub[32];
} ac_x25519_t;
int ac_x25519_keygen(ac_x25519_t *x);
int ac_x25519_shared(ac_x25519_t *x, const uint8_t peer_pub[32], uint8_t out[32]);
void ac_x25519_free(ac_x25519_t *x);

typedef struct {
  mbedtls_aes_context aes;
  uint8_t ctr[16];
  uint8_t sb[16];
  size_t off;
} ac_aesctr_t;
void ac_aesctr_init(ac_aesctr_t *s, const uint8_t key[16], const uint8_t nonce16[16]);
void ac_aesctr_process(ac_aesctr_t *s, uint8_t *buf, size_t len);
void ac_aesctr_free(ac_aesctr_t *s);

void ac_random(uint8_t *out, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* ARENDI_COMMON_H */
