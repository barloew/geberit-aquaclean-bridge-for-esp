// See web_portal.h. TEST-ONLY, UNVERIFIED (ESP-IDF 5.x, not compiled here).
#include "web_portal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "web-portal"

// SoftAP the tester joins. WPA2 needs a >=8-char password; documented in the README.
#define AP_SSID "AquaClean-Setup"
#define AP_PASS "aquaclean250"

#define NVS_NS "aqc"
#define NVS_KEY_ADDR "toilet_addr"  // 6 bytes, little-endian (TOILET_ADDR form)
#define NVS_KEY_RECCNT "rec_cnt"    // u8: recovery reboots this episode
#define NVS_KEY_REASON "last_reas"  // str: reason for the last self-reboot

// ---- health & recovery state -------------------------------------------------
static char s_health[32] = "starting";
static char s_last_reason[64] = "";

// ---- log ring buffer ---------------------------------------------------------
#define RING_SZ 16384
static char s_ring[RING_SZ];
static volatile uint32_t s_total;  // total bytes ever written
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_scan_lock;
static int (*s_prev_vprintf)(const char *, va_list);

static void ring_write(const char *p, int n) {
  if (n <= 0) return;
  if (!s_lock) return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < n; i++) {
    s_ring[s_total % RING_SZ] = p[i];
    s_total++;
  }
  xSemaphoreGive(s_lock);
}

static int log_vprintf(const char *fmt, va_list ap) {
  char buf[256];
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap2);
  va_end(ap2);
  if (n > 0) ring_write(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
  return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : n;  // keep serial output
}

void web_portal_install_log_hook(void) {
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  if (!s_scan_lock) s_scan_lock = xSemaphoreCreateMutex();
  s_prev_vprintf = esp_log_set_vprintf(log_vprintf);
}

// ---- scanned-device table (the "pick your toilet" list) ----------------------
#define SCAN_MAX 24
typedef struct {
  uint8_t addr[6];  // little-endian (NimBLE)
  uint8_t type;
  int rssi;
  char name[32];
  bool used;
} scan_dev_t;
static scan_dev_t s_scan[SCAN_MAX];

void web_portal_report_device(const uint8_t addr_le[6], uint8_t addr_type, int rssi, const char *name) {
  if (!s_scan_lock) return;
  xSemaphoreTake(s_scan_lock, portMAX_DELAY);
  int slot = -1, weakest = 0;
  for (int i = 0; i < SCAN_MAX; i++) {
    if (s_scan[i].used && memcmp(s_scan[i].addr, addr_le, 6) == 0) { slot = i; break; }
    if (!s_scan[i].used) { slot = i; break; }
    if (s_scan[i].rssi < s_scan[weakest].rssi) weakest = i;
  }
  if (slot < 0) slot = weakest;  // table full: replace the weakest signal
  s_scan[slot].used = true;
  memcpy(s_scan[slot].addr, addr_le, 6);
  s_scan[slot].type = addr_type;
  s_scan[slot].rssi = rssi;
  if (name && name[0]) {
    strncpy(s_scan[slot].name, name, sizeof(s_scan[slot].name) - 1);
    s_scan[slot].name[sizeof(s_scan[slot].name) - 1] = 0;
  }
  xSemaphoreGive(s_scan_lock);
}

// ---- config in NVS -----------------------------------------------------------
bool web_portal_get_toilet_addr(uint8_t addr[6]) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 6;
  esp_err_t e = nvs_get_blob(h, NVS_KEY_ADDR, addr, &len);
  nvs_close(h);
  return e == ESP_OK && len == 6;
}

static bool set_toilet_addr(const uint8_t addr[6]) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = nvs_set_blob(h, NVS_KEY_ADDR, addr, 6);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}

uint8_t web_portal_recovery_count_get(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
  uint8_t v = 0;
  nvs_get_u8(h, NVS_KEY_RECCNT, &v);
  nvs_close(h);
  return v;
}

void web_portal_recovery_count_set(uint8_t n) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_u8(h, NVS_KEY_RECCNT, n) == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

void web_portal_set_last_reason(const char *reason) {
  if (!reason) return;
  strncpy(s_last_reason, reason, sizeof(s_last_reason) - 1);
  s_last_reason[sizeof(s_last_reason) - 1] = 0;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_str(h, NVS_KEY_REASON, s_last_reason) == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

void web_portal_set_health(const char *state) {
  if (!state) return;
  strncpy(s_health, state, sizeof(s_health) - 1);
  s_health[sizeof(s_health) - 1] = 0;
}

static void load_last_reason(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  size_t len = sizeof(s_last_reason);
  nvs_get_str(h, NVS_KEY_REASON, s_last_reason, &len);
  nvs_close(h);
}

// ---- HTTP handlers -----------------------------------------------------------
static const char PAGE[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>AquaClean Series 250 bridge</title>"
    "<style>body{font:15px system-ui;margin:1rem;max-width:48rem}"
    "pre{background:#111;color:#0f0;padding:.5rem;height:45vh;overflow:auto;white-space:pre-wrap}"
    "input,button{font:inherit;padding:.4rem}h4{margin:.8rem 0 .2rem}.t{margin:.2rem 0}</style>"
    "<h2>AquaClean Series 250 bridge</h2>"
    "<p>Bridge health: <b id=h>…</b><span id=lr></span></p>"
    "<h3>1. Pick your toilet</h3>"
    "<p>Nearby Bluetooth devices (strongest signal first). Click <b>use</b> next to your AquaClean:</p>"
    "<div id=scan>scanning…</div>"
    "<details><summary>…or type the address manually</summary>"
    "<form onsubmit=\"save();return false\"><input id=a size=24 placeholder=e4:85:01:cd:c4:1e>"
    "<button>Save &amp; use (reboots)</button></form></details>"
    "<h3>2. Test the toilet</h3>"
    "<p>For each item: click <b>Press now</b>, then do exactly that on the remote or in the app. The log labels "
    "it, so each captured command is easy to match. Click <b>Undo</b> if you clicked the wrong one. Maintenance "
    "items are fine to just open — you don't have to run them.</p>"
    "<div id=tests></div>"
    "<p><button onclick=radiooff()>Stop WiFi (optional)</button> "
    "(takes WiFi out of the picture; you lose this page until you power-cycle)</p>"
    "<h3>3. Live log</h3>"
    "<p><button onclick=dl()>Download log (.txt)</button> — save this before you power-cycle the board.</p>"
    "<pre id=l></pre>"
    "<script>let o=0;const L=document.getElementById('l'),S=document.getElementById('scan');"
    "function esc(s){return (s||'').replace(/[<>&]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;'}[c]))}"
    "function dl(){const b=new Blob([L.textContent],{type:'text/plain'});const u=URL.createObjectURL(b);"
    "const a=document.createElement('a');a.href=u;a.download='aquaclean-log.txt';a.click();URL.revokeObjectURL(u);}"
    "const TESTS={"
    "'Remote - sit on the seat first':["
    "'Shower (Douche): start','Shower (Douche): stop (press Douche again)',"
    "'Jet strength + (while showering)','Jet strength - (while showering)',"
    "'Oscillation toggle: + and - together (while showering)'],"
    "'Remote - maintenance (optional)':["
    "'Clean shower head: Douche + + together (arm extends)','Retract arm: Douche',"
    "'Descaling: Douche + - held 5 s'],"
    "'App - same functions (pure-app session)':["
    "'Anal shower start/stop','Shower jet strength (1-5)','Oscillation on/off',"
    "'Shower-arm position (1-5)','Water temperature (1-5)','Water heating on/off',"
    "'Descaling (Maintenance and care -> Descaling)'],"
    "'App - profiles and info':['Switch user profile / personal settings',"
    "'Open device info / error messages (capture)']};"
    "function t(l,e){fetch('/test?ev='+e,{method:'POST',body:l});}"
    "function renderTests(){let h='';for(const g in TESTS){h+=`<h4>${esc(g)}</h4>`;"
    "for(const it of TESTS[g]){h+=`<div class=t>${esc(it)} `"
    "+`<button onclick=\"t('${it}','press')\">Press now</button> `"
    "+`<button onclick=\"t('${it}','undo')\">Undo</button></div>`;}}"
    "document.getElementById('tests').innerHTML=h;}"
    "async function poll(){try{let r=await fetch('/log?since='+o);o=+r.headers.get('X-Next')||o;"
    "let t=await r.text();if(t){L.textContent+=t;L.scrollTop=L.scrollHeight}}catch(e){}setTimeout(poll,1000)}"
    "async function scan(){try{let d=await (await fetch('/scan')).json();d.sort((a,b)=>b.r-a.r);"
    "S.innerHTML=d.length?d.map(x=>`<div><button onclick=\"pick('${x.a}')\">use</button> `"
    "+`${esc(x.n)||'(no name)'} — <code>${x.a}</code> (${x.r} dBm)</div>`).join(''):'scanning…';}"
    "catch(e){}setTimeout(scan,2000)}"
    "async function save(){let v=document.getElementById('a').value.trim();"
    "await fetch('/config',{method:'POST',body:v});alert('Saved. Rebooting.');}"
    "async function pick(a){await fetch('/config',{method:'POST',body:a});alert('Saved '+a+'. Rebooting.');}"
    "async function radiooff(){await fetch('/radio',{method:'POST',body:'off'});}"
    "async function hp(){try{let d=await (await fetch('/health')).json();"
    "document.getElementById('h').textContent=d.h;"
    "document.getElementById('lr').textContent=d.r?(' \\u00b7 last restart: '+d.r):'';}catch(e){}setTimeout(hp,2000)}"
    "renderTests();poll();scan();hp();</script>";

static esp_err_t h_root(httpd_req_t *r) {
  httpd_resp_set_type(r, "text/html");
  return httpd_resp_send(r, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_log(httpd_req_t *r) {
  // ?since=N -> send bytes [max(N, total-RING), total)
  uint32_t since = 0;
  char q[32];
  if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
    char v[16];
    if (httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK) since = (uint32_t)strtoul(v, NULL, 10);
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  uint32_t total = s_total;
  uint32_t oldest = total > RING_SZ ? total - RING_SZ : 0;
  if (since < oldest) since = oldest;
  uint32_t n = total - since;
  static char out[RING_SZ];
  for (uint32_t i = 0; i < n; i++) out[i] = s_ring[(since + i) % RING_SZ];
  xSemaphoreGive(s_lock);
  char nexthdr[16];
  snprintf(nexthdr, sizeof(nexthdr), "%u", (unsigned)total);
  httpd_resp_set_hdr(r, "X-Next", nexthdr);
  httpd_resp_set_type(r, "text/plain");
  return httpd_resp_send(r, out, n);
}

static esp_err_t h_scan(httpd_req_t *r) {
  char out[1800];
  int p = 0;
  out[p++] = '[';
  bool first = true;
  if (s_scan_lock) {
    xSemaphoreTake(s_scan_lock, portMAX_DELAY);
    for (int i = 0; i < SCAN_MAX; i++) {
      if (!s_scan[i].used) continue;
      const uint8_t *a = s_scan[i].addr;
      char nm[32];
      int k = 0;
      for (const char *s = s_scan[i].name; *s && k < 31; s++)
        nm[k++] = (*s == '"' || *s == '\\' || (unsigned char)*s < 0x20) ? ' ' : *s;
      nm[k] = 0;
      int w = snprintf(out + p, sizeof(out) - p,
                       "%s{\"a\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"r\":%d,\"n\":\"%s\"}",
                       first ? "" : ",", a[5], a[4], a[3], a[2], a[1], a[0], s_scan[i].rssi, nm);
      if (w < 0 || p + w >= (int)sizeof(out) - 2) break;
      p += w;
      first = false;
    }
    xSemaphoreGive(s_scan_lock);
  }
  out[p++] = ']';
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, out, p);
}

static int hex2(const char *p) {
  int hi = (*p <= '9') ? *p - '0' : (*p | 0x20) - 'a' + 10;
  int lo = (p[1] <= '9') ? p[1] - '0' : (p[1] | 0x20) - 'a' + 10;
  return (hi << 4) | lo;
}

static esp_err_t h_config(httpd_req_t *r) {
  char body[64] = {0};
  int n = r->content_len < (int)sizeof(body) - 1 ? r->content_len : (int)sizeof(body) - 1;
  httpd_req_recv(r, body, n);
  // parse "aa:bb:cc:dd:ee:ff" into the TOILET_MAC order, then reverse to addr form.
  uint8_t mac[6];
  int got = 0;
  for (const char *p = body; *p && got < 6; p++) {
    if (((*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f')) &&
        ((p[1] >= '0' && p[1] <= '9') || ((p[1] | 0x20) >= 'a' && (p[1] | 0x20) <= 'f'))) {
      mac[got++] = (uint8_t)hex2(p);
      p++;
    }
  }
  if (got != 6) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "need 6 hex bytes");
  uint8_t addr[6];
  for (int i = 0; i < 6; i++) addr[i] = mac[5 - i];  // addr is little-endian of the MAC
  set_toilet_addr(addr);
  httpd_resp_sendstr(r, "ok");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();  // apply the clone cleanly on next boot
  return ESP_OK;
}

static esp_err_t h_health(httpd_req_t *r) {
  // health/reason are our own short ASCII strings; blank out any JSON-breaking chars defensively.
  char hh[32], rr[64];
  int k = 0;
  for (const char *s = s_health; *s && k < (int)sizeof(hh) - 1; s++) hh[k++] = (*s == '"' || *s == '\\') ? ' ' : *s;
  hh[k] = 0;
  k = 0;
  for (const char *s = s_last_reason; *s && k < (int)sizeof(rr) - 1; s++) rr[k++] = (*s == '"' || *s == '\\') ? ' ' : *s;
  rr[k] = 0;
  char out[128];
  int w = snprintf(out, sizeof(out), "{\"h\":\"%s\",\"r\":\"%s\"}", hh, rr);
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, out, w);
}

static esp_err_t h_radio(httpd_req_t *r) {
  httpd_resp_sendstr(r, "stopping wifi");
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_wifi_stop();  // free the radio for an uncontended BLE measurement
  return ESP_OK;
}

// Test marker: the tester clicks "Press now" (or "Undo") for a named command,
// then performs it; the marker lands in the log right before the captured event,
// so each command self-labels in the log. ?ev=press|undo, body = the label.
static esp_err_t h_test(httpd_req_t *r) {
  char label[96] = {0};
  int n = r->content_len < (int)sizeof(label) - 1 ? r->content_len : (int)sizeof(label) - 1;
  if (n > 0) httpd_req_recv(r, label, n);
  for (char *p = label; *p; p++)
    if (*p == '\n' || *p == '\r') *p = ' ';
  char ev[8] = {0}, q[32];
  if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) httpd_query_key_value(q, "ev", ev, sizeof(ev));
  if (strcmp(ev, "undo") == 0)
    ESP_LOGW("TEST", "<<<< UNDO: %s", label);
  else
    ESP_LOGI("TEST", ">>>> PRESS NOW: %s", label);
  httpd_resp_sendstr(r, "ok");
  return ESP_OK;
}

// ---- bring-up ----------------------------------------------------------------
void web_portal_start(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  wifi_config_t ap = {0};
  strncpy((char *)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen(AP_SSID);
  strncpy((char *)ap.ap.password, AP_PASS, sizeof(ap.ap.password));
  ap.ap.max_connection = 2;
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "SoftAP '%s' up; open http://192.168.4.1/", AP_SSID);
  load_last_reason();  // show why the board last restarted (survives the RAM log being cleared)

  httpd_handle_t srv = NULL;
  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&srv, &hc) == ESP_OK) {
    httpd_uri_t u_root = {.uri = "/", .method = HTTP_GET, .handler = h_root};
    httpd_uri_t u_log = {.uri = "/log", .method = HTTP_GET, .handler = h_log};
    httpd_uri_t u_scan = {.uri = "/scan", .method = HTTP_GET, .handler = h_scan};
    httpd_uri_t u_cfg = {.uri = "/config", .method = HTTP_POST, .handler = h_config};
    httpd_uri_t u_rad = {.uri = "/radio", .method = HTTP_POST, .handler = h_radio};
    httpd_uri_t u_tst = {.uri = "/test", .method = HTTP_POST, .handler = h_test};
    httpd_uri_t u_hea = {.uri = "/health", .method = HTTP_GET, .handler = h_health};
    httpd_register_uri_handler(srv, &u_root);
    httpd_register_uri_handler(srv, &u_log);
    httpd_register_uri_handler(srv, &u_scan);
    httpd_register_uri_handler(srv, &u_cfg);
    httpd_register_uri_handler(srv, &u_rad);
    httpd_register_uri_handler(srv, &u_tst);
    httpd_register_uri_handler(srv, &u_hea);
  }
}
