// Apple Home (HomeKit) directly on the ESP (esp-homekit-sdk, non-commercial HAP): one accessory with the switches
// shower, lady shower, dryer and odor extraction. The HomeKit task only stores requests; the main loop carries
// them out and reports the real states back.
#include "hub.h"

#ifdef USE_AQUACLEAN_HOMEKIT

#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>
#include <cstdarg>
#include <hap.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>
#include <sodium.h>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/helpers.h"
#include "esphome/core/version.h"
#ifdef USE_API_HOMEASSISTANT_SERVICES
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_WEBSERVER
#include "esphome/components/web_server_base/web_server_base.h"
#endif

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean.homekit";

static int hk_identify(hap_acc_t *) {
  ESP_LOGI(TAG, LT("HomeKit: identify"));
  return HAP_SUCCESS;
}

struct HkServ {
  AquaCleanHub *hub;
  uint8_t index;
};
static HkServ hk_servs[4];

// ConfiguredName (HAP 000000E3): since iOS 17 the Home app shows this per service instead of Name; without it
// the switches are called "Schakelaar", "Schakelaar 2". Same approach as homekit_adjustable_bed.
static const char *const HK_UUID_CONFIGURED_NAME = "E3";

static int hk_write(hap_write_data_t write_data[], int count, void *serv_priv, void *) {
  auto *s = static_cast<HkServ *>(serv_priv);
  for (int i = 0; i < count; i++) {
    hap_write_data_t *w = &write_data[i];
    if (strcmp(hap_char_get_type_uuid(w->hc), HAP_CHAR_UUID_ON) == 0) {
      s->hub->homekit_request(s->index, w->val.b);
      hap_char_update_val(w->hc, &w->val);  // shown at once; the main loop corrects it when nothing happens
      *(w->status) = HAP_STATUS_SUCCESS;
    } else if (strcmp(hap_char_get_type_uuid(w->hc), HK_UUID_CONFIGURED_NAME) == 0) {
      // The Home app writes its own default ("Schakelaar 2") or a rename; keep it for now (the other
      // controllers hear it) and let the main loop put our name back after 2 s, which the Home app then adopts
      hap_char_update_val(w->hc, &w->val);
      s->hub->homekit_renamed(s->index);
      *(w->status) = HAP_STATUS_SUCCESS;
    } else {
      *(w->status) = HAP_STATUS_RES_ABSENT;
    }
  }
  return HAP_SUCCESS;
}

// Setup code and id from the MAC address when none is configured (the same at every start)
static std::string mac_setup_code() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  uint32_t h = 2166136261u;
  for (uint8_t b : mac) h = (h ^ b) * 16777619u;
  for (;;) {
    char digits[9];
    snprintf(digits, sizeof(digits), "%08u", (unsigned)(h % 100000000u));
    std::string d(digits);
    bool same = d.find_first_not_of(d[0]) == std::string::npos;
    if (!same && d != "12345678" && d != "87654321")
      return d.substr(0, 3) + "-" + d.substr(3, 2) + "-" + d.substr(5, 3);
    h = h * 16777619u + 1;
  }
}

static std::string mac_setup_id() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  static const char *const chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  uint32_t v = (mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5];
  std::string id;
  for (int i = 0; i < 4; i++) {
    id += chars[v % 36];
    v /= 36;
  }
  return id;
}

bool AquaCleanHub::homekit_state_(uint8_t i) const {
  if (i < 3) return this->hk_flags_[i] != nullptr && this->hk_flags_[i]->has_state() && this->hk_flags_[i]->state;
  return this->hk_odor_ != nullptr && this->hk_odor_->state;
}

// The SDK's own messages (pair setup, pair verify, HTTP, mDNS) in the ESPHome log
extern "C" void hap_log_forward(uint32_t level, const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  size_t n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
  // the SDK's info messages (every request, event and session) only at log level VERBOSE
  int lvl = level >= 3 ? ESPHOME_LOG_LEVEL_ERROR : level == 2 ? ESPHOME_LOG_LEVEL_WARN : ESPHOME_LOG_LEVEL_VERBOSE;
  esp_log_printf_(lvl, "geberit_aquaclean.hap", __LINE__, "%s", buf);
}

std::string homekit_qr_svg(const char *text, int size_px);  // homekit_qr.cpp

static std::atomic<bool> hk_paired_event{false};  // set in the HomeKit task, handled in the main loop
static std::string hk_payload;                    // X-HM://... (content of the QR code)
static std::string hk_acc_name;

static void hk_event(hap_event_t event, void *data) {
  const char *ctrl = data != nullptr ? static_cast<const char *>(data) : "";
  switch (event) {
    case HAP_EVENT_CTRL_PAIRED:
      ESP_LOGI(TAG, LT("HomeKit: paired with controller %s"), ctrl);
      hk_paired_event.store(true);
      break;
    case HAP_EVENT_CTRL_UNPAIRED:
      ESP_LOGI(TAG, LT("HomeKit: controller %s removed"), ctrl);
      break;
    case HAP_EVENT_CTRL_CONNECTED:
      ESP_LOGV(TAG, LT("HomeKit: controller %s connected"), ctrl);
      break;
    case HAP_EVENT_CTRL_DISCONNECTED:
      ESP_LOGV(TAG, LT("HomeKit: controller %s disconnected"), ctrl);
      break;
    case HAP_EVENT_PAIRING_STARTED:
      ESP_LOGI(TAG, LT("HomeKit: pairing started (free heap %u, largest block %u)"), (unsigned)esp_get_free_heap_size(),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      break;
    case HAP_EVENT_PAIRING_ABORTED:
      ESP_LOGW(TAG, LT("HomeKit: pairing aborted (wrong setup code or no answer)"));
      break;
    case HAP_EVENT_GET_ACC_COMPLETED:
      ESP_LOGV(TAG, LT("HomeKit: accessory list read by the controller"));
      break;
    case HAP_EVENT_ACC_REBOOTING:
      ESP_LOGW(TAG, LT("HomeKit: reboot (%s)"), data != nullptr ? static_cast<const char *>(data) : "-");
      break;
    case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
      ESP_LOGW(TAG, LT("HomeKit: pairing mode timed out; restart the ESP to pair"));
      break;
    default:
      break;
  }
}

static std::string hk_code_shown;

// "Add <name> in the Home app ..." with the name in bold: ** in the text marks it (Markdown); <b> for HTML
static std::string hk_intro(bool html) {
  std::string t = LT("Add **%s** in the Home app: scan the QR code or enter this code:");
  size_t p = t.find("%s");
  if (p != std::string::npos) t.replace(p, 2, hk_acc_name);
  if (html) {
    bool open = true;
    for (size_t q; (q = t.find("**")) != std::string::npos; open = !open) t.replace(q, 2, open ? "<b>" : "</b>");
  }
  return t;
}

#ifdef USE_WEBSERVER
// MQTT route: the QR code as a page on the ESP's web server, http://<ESP>/homekit (with its password, if set)
class HomekitQrHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET) return false;
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(url) == "/homekit";
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    std::string html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'><title>HomeKit</title><style>body{font-family:"
        "sans-serif;text-align:center;margin:2em}h1{font-size:2.2em;letter-spacing:.05em}</style>"
        "</head><body><p>";
    html += hk_intro(true);
    html += "</p><h1>" + hk_code_shown + "</h1>";
    html += homekit_qr_svg(hk_payload.c_str(), 256);
    html += "</body></html>";
    request->send(200, "text/html; charset=utf-8", html.c_str());
  }
};
#endif

// Native API route: the QR code as a Home Assistant notification (needs "Allow the device to perform Home
// Assistant actions" in the ESPHome integration's options)
bool AquaCleanHub::homekit_notify_qr_(bool show) {
#ifdef USE_API_HOMEASSISTANT_SERVICES
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected()) return false;
  api::HomeassistantActionRequest req;
  std::string title, message;
  if (show) {
    const std::string svg = homekit_qr_svg(hk_payload.c_str(), 256);
    title = LT("AquaClean: pair with HomeKit");
    message = hk_intro(false) + "\n### " + hk_code_shown + "\n![QR](data:image/svg+xml;base64," +
              base64_encode(reinterpret_cast<const uint8_t *>(svg.data()), svg.size()) + ")";
    req.service = StringRef("persistent_notification.create");
    req.data.init(3);
    auto &t = req.data.emplace_back();
    t.key = StringRef("title");
    t.value = StringRef(title);
    auto &m = req.data.emplace_back();
    m.key = StringRef("message");
    m.value = StringRef(message);
  } else {
    req.service = StringRef("persistent_notification.dismiss");
    req.data.init(1);
  }
  auto &n = req.data.emplace_back();
  n.key = StringRef("notification_id");
  n.value = StringRef("aquaclean_homekit");
  api::global_api_server->send_homeassistant_action(req);
  if (show) ESP_LOGI(TAG, LT("HomeKit: QR code sent to Home Assistant as a notification"));
  return true;
#else
  return false;
#endif
}

void AquaCleanHub::homekit_show_qr() {
  if (!this->hk_started_) return;
#ifdef USE_API_HOMEASSISTANT_SERVICES
  this->hk_qr_due_ = true;
#endif
#ifdef USE_WEBSERVER
  for (auto &ip : network::get_ip_addresses()) {
    if (ip.is_ip4()) {
      char buf[network::IP_ADDRESS_BUFFER_SIZE];
      ESP_LOGI(TAG, LT("HomeKit QR code: http://%s/homekit"), ip.str_to(buf));
      break;
    }
  }
#endif
}

// libsodium's random numbers. ESPHome's libsodium has no ESP32 source for them: it would open /dev/urandom,
// which ESP-IDF does not have, and abort (crash at the first HomeKit key). Use the hardware RNG instead.
static const char *hk_rng_name() { return "esp_fill_random"; }
static void hk_rng_buf(void *const buf, const size_t size) { esp_fill_random(buf, size); }
static uint32_t hk_rng_random() { return esp_random(); }
static randombytes_implementation hk_rng = {
    hk_rng_name, hk_rng_random, nullptr, nullptr, hk_rng_buf, nullptr,
};

void AquaCleanHub::homekit_start_() {
  this->hk_started_ = true;
  const uint32_t heap_before = esp_get_free_heap_size();
  randombytes_set_implementation(&hk_rng);
  // Ed25519 check (keys and signatures for pairing; see homekit_ed25519.c)
  {
    unsigned char pk[32], sk[64], sig[64];
    static const unsigned char msg[] = "AquaClean HomeKit";
    bool ok = crypto_sign_ed25519_keypair(pk, sk) == 0 &&
              crypto_sign_ed25519_detached(sig, nullptr, msg, sizeof(msg), sk) == 0 &&
              crypto_sign_ed25519_verify_detached(sig, msg, sizeof(msg), pk) == 0;
    sig[5] ^= 1;
    ok = ok && crypto_sign_ed25519_verify_detached(sig, msg, sizeof(msg), pk) != 0;
    sodium_memzero(sk, sizeof(sk));
    if (ok) {
      ESP_LOGI(TAG, LT("HomeKit: Ed25519 self-test OK"));
    } else {
      ESP_LOGE(TAG, LT("HomeKit: Ed25519 self-test FAILED, pairing will not work"));
    }
  }
  hap_set_debug_level(HAP_DEBUG_LEVEL_INFO);
  hap_init(HAP_TRANSPORT_WIFI);
  hap_cfg_t hap_cfg;
  hap_get_config(&hap_cfg);
  hap_cfg.task_stack_size = 8192;  // SDK default 4 KB; 8 KB as in hap-esphome
  hap_set_config(&hap_cfg);
  hap_register_event_handler(hk_event);
  // reset_pairings: once per flashed firmware; carried out from the main loop once the HomeKit task runs
  // (the SDK refuses it before that) and the SDK then restarts the ESP
  if (this->hk_reset_) {
    uint32_t done = 0;
    this->hk_reset_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("geberit_aquaclean_hk_reset"));
    this->hk_reset_pref_.load(&done);
    this->hk_reset_due_ = done != (uint32_t)App.get_build_time();
    if (!this->hk_reset_due_) ESP_LOGI(TAG, LT("HomeKit: reset_pairings already done for this firmware"));
  }
  static std::string name = App.get_friendly_name().empty() ? App.get_name() : App.get_friendly_name();
  static std::string serial = App.get_name();
  hap_acc_cfg_t cfg = {};
  cfg.name = const_cast<char *>(name.c_str());
  cfg.model = const_cast<char *>("Geberit AquaClean Bridge");
  cfg.manufacturer = const_cast<char *>("ESPHome");
  cfg.serial_num = const_cast<char *>(serial.c_str());
  cfg.fw_rev = const_cast<char *>("1.36.0");  // HomeKit wants x.y.z
  cfg.hw_rev = const_cast<char *>("1.0");
  cfg.pv = const_cast<char *>("1.1.0");
  cfg.cid = HAP_CID_SWITCH;
  cfg.identify_routine = hk_identify;
  hap_acc_t *acc = hap_acc_create(&cfg);
  if (acc == nullptr) {
    ESP_LOGE(TAG, LT("HomeKit: accessory not created"));
    return;
  }
  uint8_t product_data[] = {'A', 'Q', 'C', 'L', 'E', 'A', 'N', '1'};
  hap_acc_add_product_data(acc, product_data, sizeof(product_data));
  hap_acc_add_wifi_transport_service(acc, 0);

  // Only the functions this toilet has (the same entities Home Assistant shows)
  int offered = 0;
  uint32_t layout = 2166136261u ^ 1u;  // ^ version of the service layout
  for (uint8_t i = 0; i < 4; i++) {
    EntityBase *e = i < 3 ? static_cast<EntityBase *>(this->hk_flags_[i]) : static_cast<EntityBase *>(this->hk_odor_);
    if (e == nullptr || e->is_internal()) continue;
    hap_serv_t *serv = hap_serv_switch_create(this->homekit_state_(i));
    hap_serv_add_char(serv, hap_char_name_create(const_cast<char *>(this->hk_names_[i])));
    hap_char_t *cn = hap_char_string_create(const_cast<char *>(HK_UUID_CONFIGURED_NAME),
                                            HAP_CHAR_PERM_PR | HAP_CHAR_PERM_PW | HAP_CHAR_PERM_EV,
                                            const_cast<char *>(this->hk_names_[i]));
    hap_serv_add_char(serv, cn);
    this->hk_cn_[i] = cn;
    if (offered == 0) hap_serv_mark_primary(serv);
    hk_servs[i] = {this, i};
    hap_serv_set_priv(serv, &hk_servs[i]);
    hap_serv_set_write_cb(serv, hk_write);
    hap_acc_add_serv(acc, serv);
    this->hk_chars_[i] = hap_serv_get_char_by_uuid(serv, HAP_CHAR_UUID_ON);
    this->hk_published_[i] = this->homekit_state_(i);
    offered++;
    layout = (layout ^ (i + 1)) * 16777619u;
    for (const char *c = this->hk_names_[i]; *c; c++) layout = (layout ^ (uint8_t)*c) * 16777619u;
  }
  hap_add_accessory(acc);
  // Different services or names than at the last start (other language, ConfiguredName added): raise the
  // configuration number (c#) so paired controllers read the accessory again
  {
    uint32_t stored = 0;
    this->hk_layout_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("geberit_aquaclean_hk_layout"));
    this->hk_layout_pref_.load(&stored);
    this->hk_layout_ = layout;
    this->hk_layout_due_ = stored != layout;
  }

  static std::string code = this->hk_setup_code_.empty() ? mac_setup_code() : this->hk_setup_code_;
  hk_code_shown = code;
  static std::string id = mac_setup_id();
  hap_set_setup_code(code.c_str());
  hap_set_setup_id(id.c_str());
  hap_start();
  char *payload = esp_hap_get_setup_payload(const_cast<char *>(code.c_str()), const_cast<char *>(id.c_str()), false,
                                            HAP_CID_SWITCH);
  hk_payload = payload != nullptr ? payload : "";
  if (payload != nullptr) free(payload);
  hk_acc_name = name;
  ESP_LOGI(TAG, LT("HomeKit started: %d switches, setup code %s, %u paired; QR code content: %s"), offered,
           code.c_str(), (unsigned)hap_get_paired_controller_count(), hk_payload.c_str());
#ifdef USE_WEBSERVER
  if (web_server_base::global_web_server_base != nullptr)
    web_server_base::global_web_server_base->add_handler(new HomekitQrHandler());  // NOLINT
#endif
  // not paired yet: show the QR code in Home Assistant (as soon as it is connected)
  if (hap_get_paired_controller_count() == 0) this->homekit_show_qr();
  ESP_LOGD(TAG, LT("HomeKit memory: %u bytes used at start, %u free, largest block %u"),
           (unsigned)(heap_before - esp_get_free_heap_size()), (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void AquaCleanHub::homekit_dump_config_() {
  static const char *const reasons[] = {"unknown",    "power on",           "external",      "software",
                                        "crash",      "interrupt watchdog", "task watchdog", "other watchdog",
                                        "deep sleep", "brownout",           "SDIO"};
  int r = esp_reset_reason();
  ESP_LOGCONFIG(TAG,
                "  HomeKit: %s\n"
                "    Setup code: %s\n"
                "    Paired controllers: %u\n"
                "    Last reset: %s\n"
                "    Free heap: %u, largest block %u",
                this->hk_started_ ? "started" : "waiting for the network",
                hk_code_shown.empty() ? "-" : hk_code_shown.c_str(),
                this->hk_started_ ? (unsigned)hap_get_paired_controller_count() : 0u,
                r >= 0 && r < (int)(sizeof(reasons) / sizeof(reasons[0])) ? reasons[r] : "?",
                (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void AquaCleanHub::homekit_loop_(uint32_t now) {
  if (this->hk_reset_due_ && this->hk_started_ && hap_reset_pairings() == HAP_SUCCESS) {
    this->hk_reset_due_ = false;
    uint32_t build = (uint32_t)App.get_build_time();
    this->hk_reset_pref_.save(&build);
    global_preferences->sync();
    ESP_LOGW(TAG, LT("HomeKit: all pairings removed (reset_pairings), the ESP restarts; set it back to false"));
  }
  if (this->hk_started_) {
    if (hk_paired_event.exchange(false)) this->homekit_notify_qr_(false);  // paired: remove the notification
    if (this->hk_qr_due_ && this->homekit_notify_qr_(true)) this->hk_qr_due_ = false;
  }
  if (!this->hk_started_) {
    // after the stored feature set decided which entities exist, and with a network; only when enabled
    // (always-on builds set this at codegen, the runtime-toggle build from the HomeKit switch)
    if (this->hk_runtime_enabled_ && now > 5000 && network::is_connected()) this->homekit_start_();
    return;
  }
  if (this->hk_layout_due_ && this->hk_started_ && hap_update_config_number() == HAP_SUCCESS) {
    this->hk_layout_due_ = false;
    this->hk_layout_pref_.save(&this->hk_layout_);
    global_preferences->sync();
    ESP_LOGI(TAG, LT("HomeKit: accessory changed, Apple Home reads it again"));
  }
  // Put our names back after the Home app wrote ConfiguredName
  for (uint8_t i = 0; i < 4; i++) {
    if (this->hk_rename_[i].exchange(false)) this->hk_rename_at_[i] = now + 2000;
    if (this->hk_rename_at_[i] != 0 && static_cast<int32_t>(now - this->hk_rename_at_[i]) >= 0) {
      this->hk_rename_at_[i] = 0;
      auto *cn = static_cast<hap_char_t *>(this->hk_cn_[i]);
      const hap_val_t *cur = cn != nullptr ? hap_char_get_val(cn) : nullptr;
      if (cur != nullptr && (cur->s == nullptr || strcmp(cur->s, this->hk_names_[i]) != 0)) {
        ESP_LOGI(TAG, LT("HomeKit: Apple Home named the switch '%s', set back to '%s'"), cur->s ? cur->s : "",
                 this->hk_names_[i]);
        hap_val_t v;
        v.s = const_cast<char *>(this->hk_names_[i]);
        hap_char_update_val(cn, &v);
      }
    }
  }
  // Memory every 10 minutes (log level VERBOSE), with the lowest free memory since the start
  static uint32_t last_mem = 0;
  if (now - last_mem >= 600000) {
    last_mem = now;
    ESP_LOGV(TAG, LT("HomeKit memory: %u free, lowest %u, largest block %u, %u paired"),
             (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)hap_get_paired_controller_count());
  }
  // Requests from Apple Home
  for (uint8_t i = 0; i < 4; i++) {
    int8_t req = this->hk_request_[i].exchange(-1);
    if (req < 0 || this->hk_chars_[i] == nullptr) continue;
    const bool want = req == 1, is = this->homekit_state_(i);
    this->hk_resync_at_ = now + 6000;  // nothing happened by then: show the real state again
    if (want == is) continue;
    if (!this->is_ready()) {
      ESP_LOGW(TAG, LT("HomeKit: %s not possible, toilet not connected"), this->hk_names_[i]);
      continue;
    }
    // shower, Lady shower and dryer only run while someone is seated (Geberit: the arms are blocked otherwise);
    // odor extraction can also be started by hand without anyone seated
    if (i < 3 && !this->is_seated()) {
      ESP_LOGW(TAG, LT("HomeKit: %s only works while someone is seated"), this->hk_names_[i]);
      continue;
    }
    ESP_LOGI(TAG, LT("HomeKit: %s %s"), this->hk_names_[i], want ? LT("on") : LT("off"));
    if (i == 3)
      want ? this->hk_odor_->turn_on() : this->hk_odor_->turn_off();
    else
      this->send_command(i == 0 ? 0x00 : (i == 1 ? 0x01 : 0x02));  // toggles
    // Switching off: the toilet reports the shower active until the arm is back in (about 10-12 s); without
    // the longer wait the switch would jump back to on for a moment
    if (!want) this->hk_resync_at_ = now + 20000;
  }
  // Report the real states (changes at once, everything again after a request)
  const bool resync = this->hk_resync_at_ != 0 && static_cast<int32_t>(now - this->hk_resync_at_) >= 0;
  if (resync) this->hk_resync_at_ = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (this->hk_chars_[i] == nullptr) continue;
    const int8_t st = this->homekit_state_(i);
    if (st == this->hk_published_[i] && !resync) continue;
    this->hk_published_[i] = st;
    hap_val_t v;
    v.b = st != 0;
    hap_char_update_val(static_cast<hap_char_t *>(this->hk_chars_[i]), &v);
  }
}

}  // namespace esphome::geberit_aquaclean

#endif
