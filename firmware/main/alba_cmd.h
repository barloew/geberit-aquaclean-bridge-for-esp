/*
 * alba_cmd.h — Alba (AquaClean Series 250) Ble20 application-layer codec.
 *
 * App frame (inside the decrypted Arendi payload), from Geberit.ComLib.Core
 * DataPointAddress.GetRaw + FrameHandler.Write:
 *   [cmd] [addr_lo] [addr_hi] ( [instance] ) ( [value..] )
 *   cmd    0x00 Inventory, 0x10 Read(->0x11 ReadAns), 0x20 Write(->0x21 ack /
 *          0x22 error), 0x30 NotifyEnable; the toilet pushes NotifyData frames.
 *   addr   uint16 LE of (DpId & 0xFFFF); bit 0x8000 = "has instance", then one
 *          instance byte (0 = basic, 1..4 = user profile).
 *   value  setting value (enum/number/on-off); empty for command buttons.
 */
#ifndef ALBA_CMD_H
#define ALBA_CMD_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* datapoint category */
enum { ALBA_CMD, ALBA_SET, ALBA_SETP, ALBA_STATUS };

typedef struct {
  uint16_t addr;        /* wire address, low 16 bits of the DpId */
  const char *name;     /* human label */
  uint8_t cat;          /* ALBA_CMD / ALBA_SET / ALBA_SETP / ALBA_STATUS */
  uint8_t has_instance; /* profile datapoint: an instance byte follows */
  uint8_t writable;     /* 0 for read-only status datapoints */
  const char *enumvals; /* "0=Off | 1=Level 1 | ..." or NULL */
} alba_dp_t;

/* Ble20 command ids */
#define ALBA_CMD_INVENTORY 0x00
#define ALBA_CMD_READ 0x10
#define ALBA_CMD_READ_ANS 0x11
#define ALBA_CMD_WRITE 0x20
#define ALBA_CMD_WRITE_ACK 0x21
#define ALBA_CMD_WRITE_ERR 0x22
#define ALBA_CMD_NOTIFY_EN 0x30

/* Look up a datapoint by its wire address (low 16 bits). NULL if unknown. */
const alba_dp_t *alba_lookup(uint16_t addr);

/* Build a Write frame: 0x20 addr_lo addr_hi [instance] [value..]. Returns len. */
size_t alba_encode_write(uint16_t addr, int has_instance, uint8_t instance, const uint8_t *val, size_t vlen,
                         uint8_t *out, size_t outcap);
/* Build a Read frame: 0x10 addr_lo addr_hi. Returns len. */
size_t alba_encode_read(uint16_t addr, uint8_t *out, size_t outcap);

/* Describe a decrypted app frame as a human string (command, datapoint name,
 * instance, value with enum label where known). Always NUL-terminates. */
void alba_describe(const uint8_t *app, size_t len, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif /* ALBA_CMD_H */
