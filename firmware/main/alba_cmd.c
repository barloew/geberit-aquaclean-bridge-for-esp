/*
 * alba_cmd.c — Alba Ble20 application-layer codec (see alba_cmd.h).
 */
#include "alba_cmd.h"
#include <string.h>
#include <stdio.h>
#include "alba_dp_table.h" /* ALBA_DPS[], ALBA_DP_COUNT (generated) */

const alba_dp_t *alba_lookup(uint16_t addr) {
  for (size_t i = 0; i < ALBA_DP_COUNT; i++)
    if (ALBA_DPS[i].addr == addr) return &ALBA_DPS[i];
  return NULL;
}

size_t alba_encode_write(uint16_t addr, int has_instance, uint8_t instance, const uint8_t *val, size_t vlen,
                         uint8_t *out, size_t outcap) {
  uint16_t a = (addr & 0x7FFF) | (has_instance ? 0x8000 : 0);
  size_t n = 3 + (has_instance ? 1 : 0) + vlen;
  if (n > outcap) return 0;
  size_t i = 0;
  out[i++] = ALBA_CMD_WRITE;
  out[i++] = a & 0xFF;
  out[i++] = (a >> 8) & 0xFF;
  if (has_instance) out[i++] = instance;
  if (vlen) memcpy(out + i, val, vlen);
  return n;
}

size_t alba_encode_read(uint16_t addr, uint8_t *out, size_t outcap) {
  if (outcap < 3) return 0;
  out[0] = ALBA_CMD_READ;
  out[1] = addr & 0xFF;
  out[2] = (addr >> 8) & 0xFF;
  return 3;
}

/* Copy the label for value v out of an "a=.. | b=.." enum string. */
static int enum_label(const char *e, unsigned v, char *buf, size_t buflen) {
  char key[8];
  int kl = snprintf(key, sizeof(key), "%u=", v);
  for (const char *p = e; (p = strstr(p, key)); p++) {
    /* must be at start or right after "| " so "1=" doesn't match "11=" */
    if (p != e && p[-1] != ' ') continue;
    const char *s = p + kl, *q = strstr(s, " | ");
    size_t len = q ? (size_t)(q - s) : strlen(s);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, s, len);
    buf[len] = 0;
    return 1;
  }
  return 0;
}

static const char *cmd_name(uint8_t c) {
  switch (c) {
    case ALBA_CMD_INVENTORY:
      return "Inventory";
    case ALBA_CMD_READ:
      return "Read";
    case ALBA_CMD_READ_ANS:
      return "ReadAns";
    case ALBA_CMD_WRITE:
      return "Write";
    case ALBA_CMD_WRITE_ACK:
      return "WriteAck";
    case ALBA_CMD_WRITE_ERR:
      return "WriteError";
    case ALBA_CMD_NOTIFY_EN:
      return "NotifyEnable";
    default:
      return "Cmd";
  }
}

void alba_describe(const uint8_t *app, size_t len, char *buf, size_t buflen) {
  if (len == 0) {
    snprintf(buf, buflen, "(empty)");
    return;
  }
  uint8_t cmd = app[0];

  if (cmd == ALBA_CMD_INVENTORY || len < 3) {
    snprintf(buf, buflen, "%s (0x%02X, %uB)", cmd_name(cmd), cmd, (unsigned)len);
    return;
  }

  uint16_t raw = app[1] | (app[2] << 8);
  int has_inst = (raw & 0x8000) != 0;
  uint16_t addr = raw & 0x7FFF;
  size_t vi = 3 + (has_inst ? 1 : 0);
  const alba_dp_t *dp = alba_lookup(addr);
  const char *nm = dp ? dp->name : "?";

  char pos[40] = "";
  if (has_inst) {
    uint8_t inst = app[3];
    snprintf(pos, sizeof(pos), inst == 0 ? " [basic]" : " [profiel %u]", inst);
  }

  /* value: single byte decoded via enum table when available */
  char val[48] = "";
  if (vi < len) {
    if (len - vi == 1 && dp && dp->enumvals) {
      char lbl[32];
      if (enum_label(dp->enumvals, app[vi], lbl, sizeof(lbl)))
        snprintf(val, sizeof(val), " = %s (%u)", lbl, app[vi]);
      else
        snprintf(val, sizeof(val), " = %u", app[vi]);
    } else if (len - vi == 1) {
      snprintf(val, sizeof(val), " = %u", app[vi]);
    } else {
      snprintf(val, sizeof(val), " = %u bytes", (unsigned)(len - vi));
    }
  }

  snprintf(buf, buflen, "%s %s (0x%04X)%s%s", cmd_name(cmd), nm, addr, pos, val);
}
