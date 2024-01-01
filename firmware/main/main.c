/*
 * main.c — AquaClean Alba BLE bridge on ESP-IDF + NimBLE.
 *
 * Dual role:
 *   CENTRAL    -> connects to the real toilet, holds a persistent secure link
 *                 (arendi_client).
 *   PERIPHERAL -> advertises as the toilet so the remote control connects here
 *                 instead, and terminates the remote's secure link (arendi_server).
 *
 * Decrypted application frames are relayed between the two links, with a hook
 * for local (Home Assistant) injection. Each link is an independent Arendi
 * session with its own ephemeral keys.
 */
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_hs_adv.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "arendi_client.h"
#include "arendi_server.h"
#include "alba_cmd.h"
#include "web_portal.h"

/* Set to 0 to build without the WiFi setup/log portal (pure BLE, radio to
 * itself). With it on, the toilet address can also be entered at runtime via
 * the web page; the compile-time default below is the fallback. */
#define ENABLE_WEB_PORTAL 1

static const char *TAG = "aqc-bridge";

/* ---- configuration ---------------------------------------------------- */
/* The toilet's public BD_ADDR. Standard order e4:85:01:cd:c4:1e. Default only;
 * the web portal can override it at runtime (stored in NVS). */
#define TOILET_MAC_STD \
  { 0xe4, 0x85, 0x01, 0xcd, 0xc4, 0x1e }
/* Same address in NimBLE little-endian order, for the central scan/connect.
 * Not const: app_main() may replace it with the NVS-stored value. */
static uint8_t TOILET_ADDR[6] = {0x1e, 0xc4, 0xcd, 0x01, 0x85, 0xe4};
#define TOILET_ADDR_TYPE BLE_ADDR_PUBLIC
/* Advertised name presented to the remote. */
#define ADV_NAME "AC250"
/* A distinct random-static address used as the central's own address, so the
 * initiator address differs from the cloned toilet address we advertise with. */
static uint8_t s_central_rnd[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC0}; /* LE, MSB bits set = static */

/* ---- 128-bit characteristic UUIDs (NimBLE little-endian) -------------- */
#define UUID128_559EB(xx) \
  BLE_UUID128_INIT(0x8b, 0x71, 0x9f, 0xf8, 0xd5, 0x0e, 0x67, 0xb4, 0xe8, 0x11, 0x90, 0x23, (xx), 0xb0, 0x9e, 0x55)
static const ble_uuid128_t UUID_WRITE = UUID128_559EB(0x01);
static const ble_uuid128_t UUID_NOTIFY = UUID128_559EB(0x02);
static const ble_uuid16_t UUID_SVC = BLE_UUID16_INIT(GEBERIT_SVC_UUID16);
static const ble_uuid16_t UUID_CCCD = BLE_UUID16_INIT(0x2902);

/* ---- state ------------------------------------------------------------ */
static uint16_t s_remote_conn = BLE_HS_CONN_HANDLE_NONE; /* peripheral link */
static uint16_t s_toilet_conn = BLE_HS_CONN_HANDLE_NONE; /* central link    */
static uint16_t s_notify_val_handle;                     /* our 559eb002 value handle (server) */
static uint16_t s_toilet_write_h, s_toilet_notify_h, s_toilet_cccd_h;
static acli_ctx_t *s_cli; /* toilet-facing session */
static asrv_ctx_t *s_srv; /* remote-facing session */

static void start_adv(void);
static void start_scan(void);

/* ---- health & recovery ladder (ported from the 248 bridge) ------------------
 * Health here = a completed secure link to the toilet (this relay does not poll,
 * so a finished Arendi handshake is the liveness signal). The ladder escalates a
 * stuck/absent link: handshake watchdog -> BLE-stack reinit -> gated reboot ->
 * sticky "needs power-cycle". A persisted counter bounds reboots so a wedge can
 * never loop. All timers use esp_timer_get_time() (µs, monotonic). UNVERIFIED. */
#define HS_TIMEOUT_US (20LL * 1000000)       /* handshake must finish within 20 s of connect  */
#define UNHEALTHY_US (300LL * 1000000)       /* 5 min unhealthy before escalating              */
#define FLAP_WINDOW_US (600LL * 1000000)     /* 10 min reconnect-flap window                   */
#define FLAP_THRESHOLD 5                     /* reconnects in the window that count as flapping */
#define HEALTHY_CLEAR_US (600LL * 1000000)   /* 10 min healthy clears the episode              */
#define RES_SAMPLE_US (30LL * 1000000)       /* memory sample interval                         */
#define MAX_RECOVERY_REBOOTS 2               /* gated reboots per episode before giving up     */

static bool s_ever_connected;
static int64_t s_toilet_connected_us;  /* when the central link opened (0 = not up)        */
static int64_t s_unhealthy_since_us;   /* 0 = healthy                                      */
static int64_t s_healthy_since_us;     /* 0 = not currently healthy                        */
static int64_t s_res_last_us;          /* last memory sample                               */
static int64_t s_reconn_us[8];         /* ring of recent toilet-link reconnect times       */
static int s_reconn_i;
static uint8_t s_recovery_reboots;     /* persisted breadcrumb (web_portal NVS)            */
static bool s_reinited_episode;        /* BLE-stack reinit already tried this episode      */
static bool s_needs_power_cycle;       /* sticky: the toilet itself seems wedged           */
static void ble_stack_reinit(void);
static void health_tick(void *arg);

/* ===================== relay hooks ===================== */
/* Decrypted command from the remote -> forward to the toilet. */
static void on_remote_app(const uint8_t *pt, size_t len, void *u) {
  (void)u;
  char d[128];
  alba_describe(pt, len, d, sizeof(d));
  ESP_LOGI(TAG, "remote->toilet: %s", d);
  /* HA injection point: inspect/modify/suppress here before forwarding. */
  if (s_cli && acli_state(s_cli) == ACLI_DONE) acli_send_app(s_cli, pt, len);
}
/* Decrypted response from the toilet -> forward to the remote. */
static void on_toilet_app(const uint8_t *pt, size_t len, void *u) {
  (void)u;
  char d[128];
  alba_describe(pt, len, d, sizeof(d));
  ESP_LOGI(TAG, "toilet->remote: %s", d);
  if (s_srv && asrv_state(s_srv) == ASRV_DONE) asrv_send_app(s_srv, pt, len);
}

/* ===================== peripheral (server) ===================== */
/* Max ATT payload for a connection (MTU-3), floored at 20 (ATT_MTU 23).
 * The device keeps MTU at 23, so an Arendi frame (e.g. a ~56-byte KE frame)
 * must be split across several ATT packets. The receiver reassembles on the
 * 0x00 frame delimiter, so any split point is fine. */
static size_t att_chunk(uint16_t conn) {
  uint16_t m = ble_att_mtu(conn);
  if (m < 23) m = 23;
  return (size_t)(m - 3);
}

static int srv_send(const uint8_t *att, size_t len, void *u) {
  (void)u;
  if (s_remote_conn == BLE_HS_CONN_HANDLE_NONE) return -1;
  size_t ch = att_chunk(s_remote_conn);
  for (size_t off = 0; off < len; off += ch) {
    size_t n = (len - off < ch) ? (len - off) : ch;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(att + off, n);
    int rc = ble_gatts_notify_custom(s_remote_conn, s_notify_val_handle, om);
    if (rc) return rc; /* pacing may be needed on BLE_HS_ENOMEM */
  }
  return 0;
}

static int gatt_write_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t buf[256];
    uint16_t n = OS_MBUF_PKTLEN(ctxt->om);
    if (n > sizeof(buf)) n = sizeof(buf);
    ble_hs_mbuf_to_flat(ctxt->om, buf, n, &n);
    if (s_srv) asrv_feed(s_srv, buf, n);
  }
  return 0;
}

static const struct ble_gatt_svc_def GATT_SVCS[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &UUID_SVC.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &UUID_WRITE.u, .access_cb = gatt_write_cb, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP},
             {.uuid = &UUID_NOTIFY.u,
              .access_cb = gatt_write_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_notify_val_handle},
             {0},
         }},
    {0},
};

/* ===================== central (client) ===================== */
static int cli_send(const uint8_t *att, size_t len, void *u) {
  (void)u;
  if (s_toilet_conn == BLE_HS_CONN_HANDLE_NONE || !s_toilet_write_h) return -1;
  size_t ch = att_chunk(s_toilet_conn);
  for (size_t off = 0; off < len; off += ch) {
    size_t n = (len - off < ch) ? (len - off) : ch;
    int rc = ble_gattc_write_no_rsp_flat(s_toilet_conn, s_toilet_write_h, att + off, n);
    if (rc) return rc; /* pacing may be needed on BLE_HS_ENOMEM */
  }
  return 0;
}

/* Start the handshake only once notifications are actually enabled, so the
 * UA notification can never arrive before the CCCD write has completed. */
static int cccd_done(uint16_t conn, const struct ble_gatt_error *e, struct ble_gatt_attr *attr, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (e->status != 0 && e->status != BLE_HS_EDONE) {
    ESP_LOGE(TAG, "CCCD write failed: %d", e->status);
    return 0;
  }
  s_cli = acli_new(cli_send, on_toilet_app, NULL);
  acli_start(s_cli);
  ESP_LOGI(TAG, "toilet link: notifications on, handshake started");
  return 0;
}
static int on_dsc(uint16_t ch, const struct ble_gatt_error *e, uint16_t vh, const struct ble_gatt_dsc *dsc, void *arg) {
  (void)ch;
  (void)vh;
  (void)arg;
  if (e->status == 0 && dsc && ble_uuid_cmp(&dsc->uuid.u, &UUID_CCCD.u) == 0)
    s_toilet_cccd_h = dsc->handle;
  else if (e->status == BLE_HS_EDONE && s_toilet_cccd_h) {
    static uint8_t v[2] = {0x01, 0x00};
    ble_gattc_write_flat(s_toilet_conn, s_toilet_cccd_h, v, 2, cccd_done, NULL);
  }
  return 0;
}
static int on_chr(uint16_t ch, const struct ble_gatt_error *e, const struct ble_gatt_chr *chr, void *arg) {
  (void)ch;
  (void)arg;
  if (e->status == 0 && chr) {
    if (ble_uuid_cmp(&chr->uuid.u, &UUID_WRITE.u) == 0)
      s_toilet_write_h = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &UUID_NOTIFY.u) == 0)
      s_toilet_notify_h = chr->val_handle;
  } else if (e->status == BLE_HS_EDONE && s_toilet_notify_h) {
    ble_gattc_disc_all_dscs(s_toilet_conn, s_toilet_notify_h, 0xFFFF, on_dsc, NULL);
  }
  return 0;
}
static int on_svc(uint16_t ch, const struct ble_gatt_error *e, const struct ble_gatt_svc *svc, void *arg) {
  (void)ch;
  (void)arg;
  if (e->status == 0 && svc) ble_gattc_disc_all_chrs(s_toilet_conn, svc->start_handle, svc->end_handle, on_chr, NULL);
  return 0;
}

#if ENABLE_WEB_PORTAL
/* Feed every scan result to the web portal's "pick your toilet" list. */
static void report_discovery(const struct ble_gap_disc_desc *d) {
  struct ble_hs_adv_fields f;
  char name[32] = {0};
  if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0 && f.name_len) {
    int n = f.name_len < 31 ? f.name_len : 31;
    memcpy(name, f.name, n);
    name[n] = 0;
  }
  web_portal_report_device(d->addr.val, d->addr.type, d->rssi, name);
}
#endif

/* ===================== GAP events ===================== */
static int gap_event(struct ble_gap_event *ev, void *arg) {
  (void)arg;
  switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
#if ENABLE_WEB_PORTAL
      report_discovery(&ev->disc);
#endif
      if (ev->disc.addr.type == TOILET_ADDR_TYPE && memcmp(ev->disc.addr.val, TOILET_ADDR, 6) == 0) {
        ble_gap_disc_cancel();
        /* central uses a random own-address so the initiator address differs
         * from the (public) toilet address we clone on the peripheral side. */
        ble_gap_connect(BLE_OWN_ADDR_RANDOM, &ev->disc.addr, 10000, NULL, gap_event, NULL);
      }
      return 0;

    case BLE_GAP_EVENT_CONNECT:
      if (ev->connect.status == 0) {
        /* Which role? The toilet link is the one we initiated (central). */
        struct ble_gap_conn_desc d;
        ble_gap_conn_find(ev->connect.conn_handle, &d);
        if (d.role == BLE_GAP_ROLE_MASTER) {
          s_toilet_conn = ev->connect.conn_handle;
          s_ever_connected = true;                        /* recovery evaluation may start now */
          s_toilet_connected_us = esp_timer_get_time();   /* for the handshake watchdog        */
          ESP_LOGI(TAG, "connected to toilet; discovering service");
          ble_gattc_disc_svc_by_uuid(s_toilet_conn, &UUID_SVC.u, on_svc, NULL);
        } else {
          s_remote_conn = ev->connect.conn_handle;
          ESP_LOGI(TAG, "remote connected; presenting toilet");
          if (!s_srv) s_srv = asrv_new(srv_send, on_remote_app, NULL);
          asrv_reset(s_srv);
        }
      } else {
        start_scan();
        start_adv();
      }
      return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: /* from the toilet (central link) */
      if (ev->notify_rx.attr_handle == s_toilet_notify_h && s_cli) {
        uint8_t tmp[300];
        uint16_t n = OS_MBUF_PKTLEN(ev->notify_rx.om);
        if (n > sizeof(tmp)) n = sizeof(tmp);
        ble_hs_mbuf_to_flat(ev->notify_rx.om, tmp, n, &n);
        acli_feed(s_cli, tmp, n);
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      if (ev->disconnect.conn.conn_handle == s_toilet_conn) {
        s_toilet_conn = BLE_HS_CONN_HANDLE_NONE;
        s_toilet_connected_us = 0;
        s_reconn_us[s_reconn_i] = esp_timer_get_time();  /* recovery ladder: track flapping */
        s_reconn_i = (s_reconn_i + 1) % 8;
        s_toilet_write_h = s_toilet_notify_h = s_toilet_cccd_h = 0;
        if (s_cli) {
          acli_free(s_cli);
          s_cli = NULL;
        }
        start_scan();
      } else if (ev->disconnect.conn.conn_handle == s_remote_conn) {
        s_remote_conn = BLE_HS_CONN_HANDLE_NONE;
        start_adv(); /* wait for the remote to reconnect */
      }
      return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      start_adv();
      return 0;
    default:
      return 0;
  }
}

/* ===================== advertising / scanning ===================== */
static void start_adv(void) {
  if (s_remote_conn != BLE_HS_CONN_HANDLE_NONE) return;
  struct ble_hs_adv_fields f = {0};
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  f.name = (uint8_t *)ADV_NAME;
  f.name_len = strlen(ADV_NAME);
  f.name_is_complete = 1;
  f.uuids16 = (ble_uuid16_t[]){UUID_SVC};
  f.num_uuids16 = 1;
  f.uuids16_is_complete = 1;
  ble_gap_adv_set_fields(&f);
  struct ble_gap_adv_params p = {0};
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  /* advertise with the cloned PUBLIC toilet address so the (bonded) remote
   * recognises us as its toilet. */
  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
  ESP_LOGI(TAG, "advertising as " ADV_NAME " (cloned toilet address)");
}
static void start_scan(void) {
  if (s_toilet_conn != BLE_HS_CONN_HANDLE_NONE) return;
  struct ble_gap_disc_params p = {0};
  p.passive = 0;
  ble_gap_disc(BLE_OWN_ADDR_RANDOM, BLE_HS_FOREVER, &p, gap_event, NULL);
  ESP_LOGI(TAG, "scanning for toilet");
}

static void log_addr(const char *tag, uint8_t type) {
  uint8_t a[6];
  int rc = ble_hs_id_copy_addr(type, a, NULL);
  if (rc) {
    ESP_LOGW(TAG, "%s addr (type %u): not set (rc=%d)", tag, type, rc);
    return;
  }
  /* print in standard (human) order, i.e. reversed from NimBLE's LE bytes */
  ESP_LOGI(TAG, "%s addr (type %u): %02x:%02x:%02x:%02x:%02x:%02x", tag, type, a[5], a[4], a[3], a[2], a[1], a[0]);
}

/* Self-test banner. Re-logged on a timer because the ESP32-S3 native USB
 * re-enumerates when BLE starts ("device has been lost"), so a one-shot line at
 * boot is easily missed by the serial console. */
static void selftest_log(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "---- address self-test ----");
  log_addr("PUBLIC (should equal your toilet's address)", BLE_ADDR_PUBLIC);
  log_addr("RANDOM (central role)", BLE_ADDR_RANDOM);
}

static void on_sync(void) {
  /* own addresses: PUBLIC = cloned toilet address (peripheral), RANDOM static
   * = a distinct address for the central role. */
  ble_hs_id_set_rnd(s_central_rnd);

  selftest_log(NULL);
  /* and keep printing it every 5 s so the console can't miss it */
  static esp_timer_handle_t t;
  const esp_timer_create_args_t a = {.callback = selftest_log, .name = "selftest"};
  if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_periodic(t, 5000000);

  /* health & recovery ladder: evaluate every 2 s (runs in the esp_timer task) */
  static esp_timer_handle_t ht;
  const esp_timer_create_args_t ha = {.callback = health_tick, .name = "health"};
  if (esp_timer_create(&ha, &ht) == ESP_OK) esp_timer_start_periodic(ht, 2000000);

  start_scan();
  start_adv();
}
static void host_task(void *p) {
  (void)p;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

/* ===================== health & recovery ladder ===================== */
static int recent_reconnects(int64_t now) {
  int c = 0;
  for (int i = 0; i < 8; i++)
    if (s_reconn_us[i] && now - s_reconn_us[i] <= FLAP_WINDOW_US) c++;
  return c;
}

/* Recovery rung 3: tear the NimBLE stack down and bring it back up without a full
 * reboot. Runs from the esp_timer task (NOT the host task), which is the correct
 * context to stop/deinit the host. UNVERIFIED: the stop/deinit/init sequencing and
 * the controller teardown may need adjustment on your ESP-IDF. */
static void ble_stack_reinit(void) {
  ESP_LOGW(TAG, "BLE stack reinit (recovery rung 3) -- UNVERIFIED");
  web_portal_set_health("recovering");
  ble_gap_adv_stop();
  ble_gap_disc_cancel();
  s_toilet_conn = BLE_HS_CONN_HANDLE_NONE;
  s_remote_conn = BLE_HS_CONN_HANDLE_NONE;
  s_toilet_write_h = s_toilet_notify_h = s_toilet_cccd_h = 0;
  s_toilet_connected_us = 0;
  if (s_cli) { acli_free(s_cli); s_cli = NULL; }
  if (s_srv) { asrv_free(s_srv); s_srv = NULL; }
  nimble_port_stop();
  nimble_port_deinit();
  int rc = nimble_port_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "reinit: nimble_port_init failed (%d); rebooting instead", rc);
    web_portal_set_last_reason("ble reinit failed");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
  }
  ble_hs_cfg.sync_cb = on_sync;  /* on_sync re-runs start_scan()/start_adv() */
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_gatts_count_cfg(GATT_SVCS);
  ble_gatts_add_svcs(GATT_SVCS);
  ble_svc_gap_device_name_set(ADV_NAME);
  nimble_port_freertos_init(host_task);
}

/* Periodic (2 s) health evaluation. Health = a completed secure toilet link (this
 * relay does not poll). Escalation: handshake watchdog -> stack reinit (once per
 * episode) -> gated reboot (bounded by the persisted breadcrumb) -> sticky
 * "needs power-cycle". Runs only after the first successful connection, so a
 * missing/unconfigured toilet never triggers a reboot loop. */
static void health_tick(void *arg) {
  (void)arg;
  const int64_t now = esp_timer_get_time();

  if (s_res_last_us == 0 || now - s_res_last_us >= RES_SAMPLE_US) {
    s_res_last_us = now;
    ESP_LOGI(TAG, "resources: free heap %u, lowest %u, largest block %u, uptime %llu s",
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned long long)(now / 1000000));
  }

  const bool connected = s_toilet_conn != BLE_HS_CONN_HANDLE_NONE;
  const bool hs_done = connected && s_cli && acli_state(s_cli) == ACLI_DONE;

  /* Handshake watchdog: connected but stuck -> terminate, so the disconnect path
   * rescans and starts a fresh Arendi handshake (crypto-session recovery). */
  if (connected && !hs_done && s_toilet_connected_us && now - s_toilet_connected_us > HS_TIMEOUT_US) {
    ESP_LOGW(TAG, "handshake stuck (%ds); terminating to re-handshake", (int)(HS_TIMEOUT_US / 1000000));
    s_toilet_connected_us = now;  /* avoid repeat; the disconnect handler rescans */
    ble_gap_terminate(s_toilet_conn, BLE_ERR_REM_USER_CONN_TERM);
    return;
  }

  if (hs_done) {
    s_unhealthy_since_us = 0;
    if (s_healthy_since_us == 0) s_healthy_since_us = now;
    if (now - s_healthy_since_us >= HEALTHY_CLEAR_US) {  /* sustained healthy ends the episode */
      if (s_recovery_reboots) {
        s_recovery_reboots = 0;
        web_portal_recovery_count_set(0);
        ESP_LOGI(TAG, "recovery: healthy again, episode cleared");
      }
      s_reinited_episode = false;
      s_needs_power_cycle = false;
    }
    web_portal_set_health("ok");
    return;
  }

  /* Unhealthy. Never connected yet this boot -> do not escalate. */
  s_healthy_since_us = 0;
  if (!s_ever_connected) {
    web_portal_set_health("connecting");
    return;
  }
  if (s_unhealthy_since_us == 0) s_unhealthy_since_us = now;

  if (s_needs_power_cycle) {
    web_portal_set_health("needs power-cycle");
    return;
  }
  if (s_recovery_reboots >= MAX_RECOVERY_REBOOTS) {
    s_needs_power_cycle = true;
    web_portal_set_health("needs power-cycle");
    web_portal_set_last_reason("recovery exhausted - power-cycle the toilet");
    ESP_LOGE(TAG, "recovery exhausted; the toilet seems wedged -- power-cycle the toilet");
    return;
  }

  web_portal_set_health("recovering");
  const int64_t bad = now - s_unhealthy_since_us;
  const int flaps = recent_reconnects(now);
  if (bad <= UNHEALTHY_US && flaps < FLAP_THRESHOLD) return;

  if (!s_reinited_episode) {  /* rung 3: one stack reinit before resorting to a reboot */
    s_reinited_episode = true;
    s_unhealthy_since_us = now;  /* give the reinit time to recover before the next rung */
    ble_stack_reinit();
    return;
  }

  /* rung 4: gated reboot, bounded by the persisted breadcrumb */
  s_recovery_reboots++;
  web_portal_recovery_count_set(s_recovery_reboots);
  char reason[64];
  snprintf(reason, sizeof(reason), "recovery reboot %u/%u (%s)", s_recovery_reboots, MAX_RECOVERY_REBOOTS,
           flaps >= FLAP_THRESHOLD ? "flapping" : "toilet unreachable");
  web_portal_set_last_reason(reason);
  ESP_LOGE(TAG, "%s; rebooting", reason);
  vTaskDelay(pdMS_TO_TICKS(300));  /* let the final log line flush to serial/portal */
  esp_restart();
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());

#if ENABLE_WEB_PORTAL
  /* Capture the log for the web page first, so boot lines are visible there. */
  web_portal_install_log_hook();
  /* Runtime toilet address (from the web form) overrides the compile-time
   * default; TOILET_ADDR is the little-endian form used by the scanner. */
  uint8_t stored[6];
  if (web_portal_get_toilet_addr(stored)) {
    memcpy(TOILET_ADDR, stored, 6);
    ESP_LOGI(TAG, "toilet address loaded from NVS (web portal)");
  }
  /* Recovery breadcrumb: how many recovery reboots this bad episode (bounds the ladder). */
  s_recovery_reboots = web_portal_recovery_count_get();
  if (s_recovery_reboots) ESP_LOGW(TAG, "recovery: resumed after a recovery reboot (%u this episode)", s_recovery_reboots);
#endif

  /* Clone the toilet's PUBLIC address onto the BT interface before the stack
   * starts. ESP-IDF 5.x; the controller then uses this as its public address.
   * (If a build target rejects esp_iface_mac_addr_set for BT, set it via the
   * base MAC instead so the derived BT address equals the toilet's.)
   * bt_mac is the standard-order MAC = TOILET_ADDR reversed. */
  uint8_t bt_mac[6];
  for (int i = 0; i < 6; i++) bt_mac[i] = TOILET_ADDR[5 - i];
  esp_err_t mrc = esp_iface_mac_addr_set(bt_mac, ESP_MAC_BT);
  ESP_LOGI(TAG, "clone toilet BT MAC: esp_iface_mac_addr_set -> %s", esp_err_to_name(mrc));

#if ENABLE_WEB_PORTAL
  /* Bring up the SoftAP + web page (live log, address form, health, WiFi-off button).
   * WiFi and BLE coexist fine on this hardware (the main bridge runs its BLE dual-role
   * alongside WiFi); the page's "Stop WiFi" button only exists to take WiFi out of the
   * picture entirely if you want a fully uncontended measurement — see TEST_PLAN.md. */
  web_portal_start();
#endif

  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = on_sync;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_gatts_count_cfg(GATT_SVCS);
  ble_gatts_add_svcs(GATT_SVCS);
  ble_svc_gap_device_name_set(ADV_NAME);
  nimble_port_freertos_init(host_task);
}
