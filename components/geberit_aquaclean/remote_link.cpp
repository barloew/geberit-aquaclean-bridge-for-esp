// The ESP as "toilet" towards the real remote control, and the app relay's advertising.
#include "log_text.h"
#include "remote_link.h"

#include <esp_log.h>
#include "esphome/core/application.h"
#include "hub.h"

#ifdef USE_ESP32

#include <cinttypes>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

// Bluedroid starts a full service discovery after every GATT connection; all requests wait for it. On the
// remote control it takes ~8 s, too long. There is no esp_ API for it, but this function exists.
extern "C" uint8_t BTA_GATTC_AutoDiscoverEnable(uint8_t enable);
// ESP-IDF 5.5.x wipes the LE keys from memory on every resumed encryption where the central starts and we are
// the peripheral (smp_check_auth_req). They stay in NVS; this loads them again right away.
extern "C" void btc_config_lock(void);
extern "C" void btc_config_unlock(void);
extern "C" int _btc_storage_in_fetch_bonded_ble_device(const char *remote_bd_addr, int add);

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean.remote";

// GATT of the remote control
static const uint16_t HANDLE_FIRMWARE = 0x0010;      // Device Information, firmware version
static const uint16_t HANDLE_BUTTONS = 0x001d;       // read/notify: <code> <state>
static const uint16_t HANDLE_NOTIFY2 = 0x0021;       // read/notify, purpose unknown
static const uint16_t HANDLE_CCCD_BUTTONS = 0x001e;  // button notifications on (after encryption)
static const uint16_t HANDLE_CCCD_NOTIFY2 = 0x0022;  // notifications of 0x0021 on (the toilet does this first)
static const uint16_t HANDLE_SCREEN = 0x0024;        // screen, write without response: <offset> <19 bytes>
static const uint16_t HANDLE_STATUS = 0x002a;        // status bytes, only after pairing
static const uint32_t OPEN_DELAY = 20;               // the toilet starts ~10 ms after connecting
static const uint32_t CCCD2_DELAY = 600;             // the toilet enables 0x0022 ~0.6 s after 0x001e
static const uint16_t REMOTE_APP_ID = 0x7A;          // own GATT client app (ESPHome clients count from 1)

// Screens as the toilet writes them (decrypted recordings sela_f5 and sela_pair_2)
static const uint8_t SCREEN_PAIRING_OK[3][20] = {
    // "      Pairing ok      " as UTF-16LE, 44 bytes in three chunks
    {0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
     0x00, 0x20, 0x00, 0x50, 0x00, 0x61, 0x00, 0x69, 0x00, 0x72},
    {0x13, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x67, 0x00, 0x20, 0x00,
     0x6f, 0x00, 0x6b, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00},
    {0x26, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00},
};
static const uint8_t STATUS_ZERO[3][20] = {
    {0x00},
    {0x13},
    {0x26, 0, 0, 0, 0, 0, 0},
};
static const uint8_t CHUNK_LEN[3] = {20, 20, 7};

// Advertisement and scan response of the toilet.
// Manufacturer data: <mode> 01 <app> "14622". Mode 0xAA = pairing window after power-on (the remote control
// only connects to a toilet that sends 0xAA in pairing mode); app 0x01 = open for adding in the app, after a
// long press of + on the remote control (the app waits for this advertisement).
static uint8_t adv_data[] = {
    0x02, 0x01, 0x05,                                            // flags 05
    0x09, 0xFF, 0x00, 0x01, 0x00, 0x31, 0x34, 0x36, 0x32, 0x32,  // manufacturer data
    0x03, 0x02, 0xA0, 0x3E,                                      // 16-bit service UUID 0x3EA0 (incomplete list)
};
static const size_t ADV_MODE_INDEX = 5;
static const uint8_t ADV_MODE_NORMAL = 0x00;
static const uint8_t ADV_MODE_PAIRING = 0xAA;
static const size_t ADV_APP_INDEX = 7;
static const uint32_t APP_PAIR_MS = 60000;
static const uint8_t SCAN_RSP_DATA[] = {
    0x0F, 0x09, 'G',  'e',  'b',  'e',  'r', 'i', 't', ' ', 'A', 'C', ' ', 'P', 'R', 'O',  // name
    0x05, 0x12, 0x1E, 0x00, 0x1E, 0x00,                                                    // preferred interval 37.5 ms
    0x02, 0x0A, 0x00,                                                                      // tx power 0 dBm
    0x04, 0xFF, 0x00, 0x30, 0x38,                                                          // manufacturer data
};

// Button codes in the order of BUTTON_KEYS in __init__.py (event types and lang "buttons")
static const uint8_t BUTTON_CODES[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x09, 0x0a, 0x0b, 0x0d,
                                       0x0e, 0x0f, 0x10, 0x12, 0x13, 0x16, 0x17, 0x18, 0x19};
static const char *const BUTTON_KEYS[] = {
    "shower",      "spray_minus", "spray_plus", "arm_back",    "arm_forward",
    "lady_shower", "profile_1",   "profile_2",  "profile_3",   "arrow_right",
    "arrow_left",  "arrow_down",  "arrow_up",   "oscillation", "orientation_light",
    "ok",          "temperature", "home",       "maintenance",
};
static const size_t BUTTON_COUNT = sizeof(BUTTON_CODES);

static int button_index(uint8_t code) {
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
    if (BUTTON_CODES[i] == code) return static_cast<int>(i);
  }
  return -1;
}

static uint64_t bda_to_u64(const uint8_t *bda) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++) v = (v << 8) | bda[i];
  return v;
}

// ---------------------------------------------------------------------------------------------------
// Setup and advertising
// ---------------------------------------------------------------------------------------------------

void RemoteLink::set_remote_address(uint64_t address) {
  this->address_ = address;
  for (int i = 0; i < 6; i++) this->remote_bda_[i] = static_cast<uint8_t>(address >> (8 * (5 - i)));
}

bool RemoteLink::is_remote_(const uint8_t *bda) const {
  return this->has_remote_() && bda_to_u64(bda) == this->address_;
}

static std::string mac_str(uint64_t a) {
  return str_sprintf("%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)(a >> 40) & 0xFF, (unsigned)(a >> 32) & 0xFF,
                     (unsigned)(a >> 24) & 0xFF, (unsigned)(a >> 16) & 0xFF, (unsigned)(a >> 8) & 0xFF,
                     (unsigned)a & 0xFF);
}

void RemoteLink::publish_addresses_() {
  if (this->toilet_address_sensor_ != nullptr && this->hub_ != nullptr && this->hub_->parent() != nullptr) {
    const uint64_t a = this->hub_->parent()->get_address();
    this->toilet_address_sensor_->publish_state(a == 0 ? std::string("...") : mac_str(a));
  }
  if (this->remote_address_sensor_ != nullptr)
    this->remote_address_sensor_->publish_state(this->address_ == 0 ? std::string("...") : mac_str(this->address_));
}

void RemoteLink::setup() {
  // Without a security record for the toilet (after a failed encryption) Bluedroid logs an error on every GATT
  // operation (BTM_GetSecurityFlags failed), several per second; our own log says what happened
  esp_log_level_set("BT_BTM", ESP_LOG_NONE);
  // No address in YAML (00:00:00:00:00:00): use the one found before, or search
  if (this->hub_ != nullptr && this->hub_->parent() != nullptr && this->hub_->parent()->get_address() == 0) {
    this->auto_toilet_ = true;
    this->toilet_pref_ = global_preferences->make_preference<uint64_t>(fnv1_hash("geberit_aquaclean_toilet"));
    uint64_t a = 0;
    if (this->toilet_pref_.load(&a) && a != 0) {
      this->hub_->parent()->set_address(a);
      ESP_LOGI(TAG, LT("Toilet %s (found before)"), ob(mac_str(a)));
    } else {
      ESP_LOGI(TAG, LT("No toilet address: looking for the nearest toilet"));
    }
  }
  if (this->address_ == 0) {
    this->auto_remote_ = true;
    this->remote_pref_ = global_preferences->make_preference<uint64_t>(fnv1_hash("geberit_aquaclean_remote"));
    uint64_t a = 0;
    if (this->remote_pref_.load(&a) && a != 0) {
      this->set_remote_address(a);
      ESP_LOGI(TAG, LT("Remote control %s (paired before)"), ob(mac_str(a)));
    } else {
      ESP_LOGI(TAG, LT("No remote control address: the remote control that pairs in the pairing window is used"));
    }
  }
  this->publish_addresses_();

  if (this->hub_ != nullptr)
    this->hub_->set_app_sink(
        [this](uint8_t idx, const uint8_t *data, uint16_t len) { this->send_to_app_(idx, data, len); });
  this->set_remote_connected_(false);
}

void RemoteLink::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Geberit AquaClean link:\n"
                "  Remote control: %s\n"
                "  Advertise as toilet: %s\n"
                "  Pass through: %s\n"
                "  Idle timeout: %" PRIu32
                " ms\n"
                "  Services for the toilet (handles): DIS 0x%04x, buttons 0x%04x, screen 0x%04x, fourth 0x%04x, "
                "app 0x%04x",
                ob_mac(this->remote_bda_), YESNO(this->advertise_), YESNO(this->forward_), this->idle_timeout_,
                this->gatts_handles_[0][0], this->gatts_handles_[1][0], this->gatts_handles_[2][0],
                this->gatts_handles_[3][0], this->gatts_handles_[4][0]);
}

void RemoteLink::set_advertise(bool v) {
  this->advertise_ = v;
  if (!v && this->adv_running_) {
    esp_ble_gap_stop_advertising();
    this->adv_running_ = false;
  } else if (v && this->adv_configured_) {
    this->start_advertising_();
  }
}

void RemoteLink::start_advertising_() {
  if (!this->advertise_ || this->adv_running_ || !this->adv_configured_) return;
  if (this->irk_active_ && !this->irk_advertising_)
    return;  // IRK capture: wait for the random address and the services
  esp_ble_adv_params_t params{};
  params.adv_int_min = 0x64;  // 62.5 ms, like the toilet
  params.adv_int_max = 0x64;
  params.adv_type = ADV_TYPE_IND;
  params.own_addr_type = this->irk_active_ ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC;  // IRK capture: new identity
  params.channel_map = ADV_CHNL_ALL;
  params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
  esp_err_t err = esp_ble_gap_start_advertising(&params);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, LT("Starting advertising failed: %d"), err);
    return;
  }
  this->adv_running_ = true;
}

void RemoteLink::apply_adv_data_() {
  adv_data[ADV_MODE_INDEX] = this->pairing_ ? ADV_MODE_PAIRING : ADV_MODE_NORMAL;
  adv_data[ADV_APP_INDEX] = this->app_pair_until_ != 0 ? 0x01 : 0x00;
  esp_err_t err = esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));
  if (err != ESP_OK) ESP_LOGE(TAG, LT("Setting advertisement failed: %d"), err);
  // scan response and (re)start follow in gap_event_handler
}

// ---------------------------------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------------------------------

void RemoteLink::loop() {
  const uint32_t now = millis();
  if (this->reload_keys_due_) {
    this->reload_keys_due_ = false;
    App.safe_reboot();
    return;
  }
  // End of the search for the toilet: take the strongest one
  if (this->search_end_ != 0 && static_cast<int32_t>(now - this->search_end_) >= 0) {
    this->search_end_ = 0;
    if (this->cand_addr_ != 0 && this->hub_->parent()->get_address() == 0) {
      ESP_LOGI(TAG, LT("Toilet %s chosen (strongest signal, %d dBm), connecting"), ob(mac_str(this->cand_addr_)),
               this->cand_rssi_);
      this->toilet_pref_.save(&this->cand_addr_);
      global_preferences->sync();
      this->hub_->new_toilet();
      this->hub_->parent()->set_address(this->cand_addr_);
      this->publish_addresses_();
    }
  }
  const bool ble_up = esp32_ble::global_ble != nullptr && esp32_ble::global_ble->is_active();

  if (ble_up && !this->app_registered_) {
    this->app_registered_ = true;
    esp_err_t err = esp_ble_gattc_app_register(REMOTE_APP_ID);
    if (err != ESP_OK) ESP_LOGE(TAG, LT("Registering GATT client app failed: %d"), err);
  }
  if (ble_up && !this->adv_configured_ && this->gattc_if_ != ESP_GATT_IF_NONE) {
    this->adv_configured_ = true;
    this->apply_adv_data_();
  }
  // Register with the tracker as soon as it exists (its setup may run after ours)
  if (!this->listener_registered_ && esp32_ble_tracker::global_esp32_ble_tracker != nullptr) {
    this->listener_registered_ = true;
    esp32_ble_tracker::global_esp32_ble_tracker->register_listener(
        static_cast<esp32_ble_tracker::ESPBTDeviceListener *>(this));
  }
  this->toilet_loop_(now);
  this->irk_loop_(now);

  const bool pt = this->is_passthrough();
  if (this->passthrough_sensor_ != nullptr && this->passthrough_published_ != static_cast<int8_t>(pt)) {
    this->passthrough_published_ = pt;
    this->passthrough_sensor_->publish_state(pt);
  }

  // Only an app that connects while the window is open closes it early: an app that was already connected
  // (another phone) must not stop a new one from adding the toilet.
  if (this->app_pair_until_ != 0 &&
      (this->app_pair_joined_ || static_cast<int32_t>(now - this->app_pair_until_) >= 0)) {
    this->app_pair_until_ = 0;
    ESP_LOGI(TAG, LT("App adding window closed%s"),
             this->app_pair_joined_ ? LT(" (app connected)") : LT(" (no app connected)"));
    this->app_pair_joined_ = false;
    this->apply_adv_data_();
  }
  if (this->adv_restart_at_ != 0 && static_cast<int32_t>(now - this->adv_restart_at_) >= 0) {
    this->adv_restart_at_ = 0;
    this->start_advertising_();
  }
  // Advertising guardian. The connect/disconnect paths above re-advertise after every connection, so in
  // normal use advertising is always either running or scheduled to restart. If it is ever found down with
  // nothing scheduled to bring it back -- a silent peripheral wedge -- the bridge becomes unreachable to the
  // remote and the app while the toilet link stays perfectly healthy, and no request timeout would ever
  // reveal it. Re-assert advertising if that state persists past a grace window (well above the ~300 ms
  // connect gap). Pairing, IRK capture and the app-adding window drive advertising themselves and are skipped.
  constexpr uint32_t ADV_GUARD_MS = 5000;
  const bool adv_wanted =
      this->advertise_ && this->adv_configured_ && !this->pairing_ && !this->irk_active_ && this->app_pair_until_ == 0;
  if (adv_wanted && !this->adv_running_ && this->adv_restart_at_ == 0) {
    if (this->adv_down_since_ == 0) {
      this->adv_down_since_ = now;
    } else if (now - this->adv_down_since_ >= ADV_GUARD_MS) {
      this->adv_down_since_ = 0;
      ESP_LOGW(TAG, LT("Advertising unexpectedly down, re-asserting (peripheral guardian)"));
      this->start_advertising_();
    }
  } else {
    this->adv_down_since_ = 0;
  }
  if (this->pairing_ && now - this->pairing_started_ >= this->pairing_duration_) {
    ESP_LOGI(TAG, LT("Pairing window closed"));
    this->stop_pairing();
  }

  // Automatic discovery only while the toilet is not fully connected: the toilet link needs it, the remote
  // control does not. Bluedroid starts it as soon as a connection is established, so switching at open time
  // is too late.
  if (ble_up && this->gattc_if_ != ESP_GATT_IF_NONE) {
    const int8_t want = (this->hub_ != nullptr && this->hub_->is_ready()) ? 0 : 1;
    if (want != this->auto_disc_ && !this->remote_linked_) {
      this->auto_disc_ = want;
      BTA_GATTC_AutoDiscoverEnable(want);
      ESP_LOGD(TAG, LT("Automatic service discovery %s"),
               want ? LT("on (toilet connecting)") : LT("off (toilet connected)"));
    }
  }

  if (!this->remote_linked_) return;

  // Open GATT on the connection the remote control set up itself (no MTU request: it does not answer one)
  if (this->want_open_ && now - this->linked_at_ >= OPEN_DELAY) {
    this->want_open_ = false;
    esp_err_t err = esp_ble_gattc_open(this->gattc_if_, this->remote_bda_, BLE_ADDR_TYPE_PUBLIC, true);
    if (err != ESP_OK) ESP_LOGW(TAG, LT("GATT open not sent: %d"), err);
  }
  // Enable 0x0022 a little after 0x001e, like the toilet; read the firmware once
  if (this->cccd2_due_ != 0 && static_cast<int32_t>(now - this->cccd2_due_) >= 0) {
    this->cccd2_due_ = 0;
    this->write_cccd_(HANDLE_CCCD_NOTIFY2);
    if (this->firmware_sensor_ != nullptr && !this->firmware_sensor_->has_state())
      esp_ble_gattc_read_char(this->gattc_if_, this->conn_id_, HANDLE_FIRMWARE, ESP_GATT_AUTH_REQ_NONE);
  }
  // Safety net: the remote control closes the connection itself ~6 s after the last press
  if (now - this->last_activity_ >= this->idle_timeout_) {
    ESP_LOGD(TAG, LT("No button press for a while, closing the connection to the remote control"));
    this->last_activity_ = now;
    esp_ble_gap_disconnect(this->remote_bda_);
  }
}

void RemoteLink::set_remote_connected_(bool v) {
  if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(v);
}

void RemoteLink::reset_link_() {
  this->remote_linked_ = false;
  this->want_open_ = false;
  this->gatt_open_ = false;
  this->encrypted_ = false;
  this->pairing_seen_ = false;
  this->session_started_ = false;
  this->buttons_cccd_written_ = false;
  this->cccd2_due_ = 0;
  this->conn_id_ = 0xFFFF;
}

// ---------------------------------------------------------------------------------------------------
// GATT client towards the remote control
// ---------------------------------------------------------------------------------------------------

void RemoteLink::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                     esp_ble_gattc_cb_param_t *param) {
  if (event == ESP_GATTC_REG_EVT) {
    if (param->reg.app_id == REMOTE_APP_ID) {
      if (param->reg.status == ESP_GATT_OK) {
        this->gattc_if_ = gattc_if;
        ESP_LOGD(TAG, LT("GATT client app registered (if %d)"), gattc_if);
      } else {
        ESP_LOGE(TAG, LT("Registering GATT client app failed: status %d"), param->reg.status);
      }
    }
    return;
  }
  // Every event arrives once per registered app; only those of our own app count
  if (this->gattc_if_ == ESP_GATT_IF_NONE || gattc_if != this->gattc_if_) return;

  switch (event) {
    case ESP_GATTC_CONNECT_EVT: {
      if (param->connect.link_role != 1) break;  // we are central (connection to the toilet): not ours
      this->adv_running_ = false;                // connectable advertising stops on a connection
      // Keep advertising: the app must be able to connect while the remote control is connected and vice versa
      this->adv_restart_at_ = millis() + 300;
      if (this->irk_on_connect_(param->connect.remote_bda)) break;  // IRK capture: the phone, not the app
      if (!this->is_remote_(param->connect.remote_bda)) {
        // Any other device that connects to the "toilet" is the app
        memcpy(this->app_bda_, param->connect.remote_bda, 6);
        this->app_gattc_conn_id_ = param->connect.conn_id;
        this->app_linked_ = true;
        if (this->app_pair_until_ != 0) this->app_pair_joined_ = true;
        ESP_LOGI(TAG, LT("App connected (%s)"), ob_mac(param->connect.remote_bda));
        if (this->hub_ != nullptr) {
          this->hub_->set_app_connected(true);
          this->hub_->on_app_device(bda_to_u64(param->connect.remote_bda));
        }
        break;
      }
      ESP_LOGI(TAG, LT("Remote control connected"));
      this->remote_linked_ = true;
      this->want_open_ = true;
      this->conn_id_ = param->connect.conn_id;
      this->linked_at_ = millis();
      this->last_activity_ = this->linked_at_;
      this->set_remote_connected_(true);
      break;
    }

    case ESP_GATTC_OPEN_EVT: {
      if (!this->is_remote_(param->open.remote_bda)) break;
      if (param->open.status != ESP_GATT_OK && param->open.status != ESP_GATT_ALREADY_OPEN) {
        ESP_LOGW(TAG, LT("GATT open failed: status %d"), param->open.status);
        break;
      }
      this->conn_id_ = param->open.conn_id;
      this->gatt_open_ = true;
      ESP_LOGD(TAG, LT("GATT open on the remote control (conn %u)"), this->conn_id_);
      esp_ble_gattc_register_for_notify(this->gattc_if_, this->remote_bda_, HANDLE_BUTTONS);
      esp_ble_gattc_register_for_notify(this->gattc_if_, this->remote_bda_, HANDLE_NOTIFY2);
      if (this->encrypted_) {
        this->begin_session_();  // normal session: already encrypted with the stored key
      } else {
        // Not encrypted yet: probably pairing. The toilet enables 0x0022 first then.
        this->write_cccd_(HANDLE_CCCD_NOTIFY2);
      }
      break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT: {
      if (param->write.conn_id != this->conn_id_) break;
      const uint16_t h = param->write.handle;
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, LT("Write to 0x%04x failed: status 0x%02x"), h, param->write.status);
        if (h == HANDLE_CCCD_BUTTONS) this->buttons_cccd_written_ = false;
        break;
      }
      if (h == HANDLE_CCCD_BUTTONS) ESP_LOGI(TAG, LT("Remote control ready, receiving buttons"));
      break;
    }

    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.conn_id != this->conn_id_ || param->read.handle != HANDLE_FIRMWARE) break;
      if (param->read.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, LT("Reading firmware failed: %d"), param->read.status);
        break;
      }
      std::string fw;
      for (uint16_t i = 0; i < param->read.value_len; i++) {
        char c = static_cast<char>(param->read.value[i]);
        if (c >= 0x20 && c < 0x7f) fw += c;
      }
      ESP_LOGI(TAG, LT("Remote control firmware: %s"), fw.c_str());
      if (this->firmware_sensor_ != nullptr) this->firmware_sensor_->publish_state(fw);
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.conn_id != this->conn_id_) break;
      const uint8_t *v = param->notify.value;
      const uint16_t len = param->notify.value_len;
      if (this->is_passthrough()) this->forward_to_toilet_(param->notify.handle, v, len);
      if (param->notify.handle == HANDLE_BUTTONS && len >= 2) this->on_button_(v[0], v[1]);
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      if (this->irk_is_peer_(param->disconnect.remote_bda)) {
        this->irk_peer_set_ = false;
        ESP_LOGI(TAG, LT("IRK capture: phone disconnected (reason 0x%02x)"), param->disconnect.reason);
      }
      if (this->app_linked_ && memcmp(param->disconnect.remote_bda, this->app_bda_, 6) == 0) {
        ESP_LOGI(TAG, LT("App disconnected (reason 0x%02x)"), param->disconnect.reason);
        this->app_linked_ = false;
        if (this->hub_ != nullptr) this->hub_->set_app_connected(false);
      }
      if (this->is_remote_(param->disconnect.remote_bda)) {
        if (this->remote_linked_)
          ESP_LOGI(TAG, LT("Remote control disconnected (reason 0x%02x)"), param->disconnect.reason);
        this->reset_link_();
        this->set_remote_connected_(false);
      }
      this->start_advertising_();  // after every connection (also refused ones) advertise again
      break;
    }

    case ESP_GATTC_CLOSE_EVT: {
      if (this->is_remote_(param->close.remote_bda)) this->gatt_open_ = false;
      break;
    }

    default:
      break;
  }
}

void RemoteLink::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (this->irk_gap_event_(event, param)) return;
  if (this->toilet_gap_event_(event, param)) return;
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      esp_ble_gap_config_scan_rsp_data_raw(const_cast<uint8_t *>(SCAN_RSP_DATA), sizeof(SCAN_RSP_DATA));
      return;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
      this->start_advertising_();
      return;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGD(TAG, LT("Advertising as Geberit AC PRO%s"),
                 this->pairing_               ? LT(" (pairing window)")
                 : this->app_pair_until_ != 0 ? LT(" (app adding window)")
                                              : LT(""));
      } else {
        ESP_LOGW(TAG, LT("Advertising not started: %d"), param->adv_start_cmpl.status);
        this->adv_running_ = false;
      }
      return;
    case ESP_GAP_BLE_SEC_REQ_EVT:
      // Remote control not known yet: the device that pairs in the pairing window is the remote control
      // (the app never pairs). It connected as "app" a moment ago; take that connection over.
      if (this->auto_remote_ && !this->has_remote_() && this->pairing_ && this->app_linked_ &&
          memcmp(param->ble_security.ble_req.bd_addr, this->app_bda_, 6) == 0) {
        const uint64_t a = bda_to_u64(param->ble_security.ble_req.bd_addr);
        this->set_remote_address(a);
        this->remote_pref_.save(&a);
        global_preferences->sync();
        ESP_LOGI(TAG, LT("Remote control %s learned"), ob(mac_str(a)));
        this->app_linked_ = false;
        if (this->hub_ != nullptr) this->hub_->set_app_connected(false);
        this->remote_linked_ = true;
        this->want_open_ = true;
        this->conn_id_ = this->app_gattc_conn_id_;
        if (this->app_conn_id_ == static_cast<int>(this->app_gattc_conn_id_))
          this->app_conn_id_ = -1;  // GATT server side: no longer the app either
        this->linked_at_ = millis();
        this->last_activity_ = this->linked_at_;
        this->set_remote_connected_(true);
        this->publish_addresses_();
      }
      if (!this->is_remote_(param->ble_security.ble_req.bd_addr)) return;
      ESP_LOGI(TAG, LT("Remote control starts pairing"));
      this->pairing_seen_ = true;
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      return;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      break;
    default:
      return;
  }

  // AUTH_CMPL
  if (!this->is_remote_(param->ble_security.auth_cmpl.bd_addr)) return;
  if (!param->ble_security.auth_cmpl.success) {
    ESP_LOGW(TAG, LT("Pairing or encryption with the remote control failed, reason 0x%02x"),
             param->ble_security.auth_cmpl.fail_reason);
    return;
  }
  ESP_LOGI(TAG, LT("Remote control encrypted (%s, bonded devices: %d)"),
           this->pairing_seen_ ? LT("newly paired") : LT("stored key"), esp_ble_get_bond_device_num());
  this->encrypted_ = true;
  this->last_activity_ = millis();
  if (!this->pairing_seen_) this->restore_bond_();
  if (this->pairing_seen_ && this->pairing_) {
    ESP_LOGI(TAG, LT("Pairing done, pairing window closed"));
    this->stop_pairing();
  }
  if (this->gatt_open_) this->begin_session_();  // otherwise begin_session_ follows at OPEN_EVT
}

void RemoteLink::begin_session_() {
  if (this->session_started_) return;
  this->session_started_ = true;
  // Enable buttons: the remote control waits for this (the toilet does it ~50 ms after encryption)
  this->write_cccd_(HANDLE_CCCD_BUTTONS);
  if (this->pairing_seen_) {
    // Finish pairing like the toilet: show "Pairing ok" and clear the status bytes
    ESP_LOGI(TAG, LT("Finishing pairing: Pairing ok"));
    for (int i = 0; i < 3; i++) this->write_cmd_(HANDLE_SCREEN, SCREEN_PAIRING_OK[i], CHUNK_LEN[i]);
    for (int i = 0; i < 3; i++) this->write_cmd_(HANDLE_STATUS, STATUS_ZERO[i], CHUNK_LEN[i]);
  }
  this->cccd2_due_ = millis() + CCCD2_DELAY;
}

void RemoteLink::write_cccd_(uint16_t handle) {
  if (handle == HANDLE_CCCD_BUTTONS) {
    if (this->buttons_cccd_written_) return;
    this->buttons_cccd_written_ = true;
  }
  uint8_t value[2] = {0x01, 0x00};
  esp_err_t err = esp_ble_gattc_write_char_descr(this->gattc_if_, this->conn_id_, handle, sizeof(value), value,
                                                 ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) ESP_LOGW(TAG, LT("Write to 0x%04x not sent: %d"), handle, err);
}

void RemoteLink::write_cmd_(uint16_t handle, const uint8_t *data, uint16_t len) {
  uint8_t buf[64];
  if (len > sizeof(buf)) len = sizeof(buf);
  memcpy(buf, data, len);
  esp_err_t err = esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, handle, len, buf,
                                           ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) ESP_LOGW(TAG, LT("Write to 0x%04x not sent: %d"), handle, err);
}

void RemoteLink::start_pairing() {
  if (!this->has_remote_() && !this->auto_remote_) {
    ESP_LOGW(TAG, LT("No remote_mac_address configured"));
    return;
  }
  this->pairing_ = true;
  this->pairing_started_ = millis();
  ESP_LOGI(TAG, "Pairing window open for %" PRIu32 " s: hold + on the remote control for about 30 s",
           this->pairing_duration_ / 1000);
  if (this->adv_configured_) this->apply_adv_data_();
}

void RemoteLink::stop_pairing() {
  if (!this->pairing_) return;
  this->pairing_ = false;
  if (this->adv_configured_) this->apply_adv_data_();
}

void RemoteLink::restore_bond_() {
  char addr[18];
  snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", this->remote_bda_[0], this->remote_bda_[1],
           this->remote_bda_[2], this->remote_bda_[3], this->remote_bda_[4], this->remote_bda_[5]);
  btc_config_lock();
  const int ret = _btc_storage_in_fetch_bonded_ble_device(addr, 1);
  btc_config_unlock();
  if (ret != 0) ESP_LOGW(TAG, LT("Key of the remote control not found in NVS (%d)"), ret);
}

void RemoteLink::unpair() {
  if (!this->has_remote_()) return;
  esp_err_t err = esp_ble_remove_bond_device(this->remote_bda_);
  ESP_LOGI(TAG, LT("Bond with the remote control removed (%d)"), err);
  if (this->auto_remote_) {
    // learned automatically: forget it, the next one that pairs in the pairing window is used
    uint64_t zero = 0;
    this->remote_pref_.save(&zero);
    global_preferences->sync();
    this->set_remote_address(0);
    this->publish_addresses_();
    ESP_LOGI(TAG, LT("Remote control forgotten; pair a remote control in the pairing window"));
  }
}

void RemoteLink::search_toilet() {
  if (!this->auto_toilet_) {
    ESP_LOGW(TAG, LT("The toilet address is set in YAML; remove toilet_mac to search automatically"));
    return;
  }
  uint64_t zero = 0;
  this->toilet_pref_.save(&zero);
  global_preferences->sync();
  this->cand_addr_ = 0;
  this->cand_rssi_ = -128;
  this->search_end_ = 0;
  auto *client = this->hub_->parent();
  if (client->get_address() != 0) client->disconnect();
  client->set_address(0);
  this->publish_addresses_();
  ESP_LOGI(TAG, LT("Looking for the nearest toilet"));
}

void RemoteLink::toilet_candidate_(const esp32_ble_tracker::ESPBTDevice &device) {
  // Geberit toilets: manufacturer data <mode> 01 <app> "146xx" (mode 00 normal, aa pairing window).
  // Other ESPs with this component advertise the same; the strongest signal wins.
  bool geberit = false;
  for (const auto &m : device.get_manufacturer_datas()) {
    if (m.data.size() >= 4 && m.data[1] == '1' && m.data[2] == '4' && m.data[3] == '6') geberit = true;
  }
  if (!geberit) return;
  const uint64_t a = device.address_uint64();
  const int rssi = device.get_rssi();
  if (this->search_end_ == 0 && this->cand_addr_ == 0) {
    this->search_end_ = millis() + 10000;  // collect for 10 s, then take the strongest
    ESP_LOGI(TAG, LT("Geberit toilet seen, looking for 10 s for the nearest one"));
  }
  if (a == this->cand_addr_) {
    this->cand_rssi_ = std::max(this->cand_rssi_, rssi);
  } else if (rssi > this->cand_rssi_) {
    ESP_LOGI(TAG, LT("Toilet candidate %s, %d dBm"), ob(mac_str(a)), rssi);
    this->cand_addr_ = a;
    this->cand_rssi_ = rssi;
  }
}

void RemoteLink::on_button_(uint8_t code, uint8_t state) {
  // With passthrough the button already went to the toilet as a notify; the toilet handles screen and action.
  this->last_activity_ = millis();
  if (code == 0xFF) return;  // wake-up
  const bool passthrough = this->is_passthrough();
  const int idx = button_index(code);
  ESP_LOGI(TAG, LT("Button %02x %s state %u%s"), code, idx >= 0 ? BUTTON_KEYS[idx] : LT("unknown"), state,
           passthrough ? LT("") : LT(" (toilet not reachable as remote control)"));

  // A long press of + (passed through) opens the toilet for adding in the app. The toilet itself cannot
  // advertise while the ESP is connected, so the ESP advertises the app-adding state instead.
  if (code == 0x03 && state == 0x01 && passthrough) {
    ESP_LOGI(TAG, LT("+ long: the app can add the toilet for %u s"), static_cast<unsigned>(APP_PAIR_MS / 1000));
    this->app_pair_until_ = millis() + APP_PAIR_MS;
    this->app_pair_joined_ = false;
    this->apply_adv_data_();
  }
  // A long press of the maintenance key at the remote control shows (or hides) the physical maintenance device:
  // someone is there
  if (code == 0x19 && state == 0x01 && this->hub_ != nullptr) this->hub_->on_maintenance_button();
  if (state == 0x02) return;  // release: only logged
  // profile keys 1-3 (index 6-8): the user of that profile is at the toilet
  if (idx >= 6 && idx <= 8 && this->hub_ != nullptr) this->hub_->on_remote_profile(idx - 5);
  if (!passthrough && this->forward_ && this->hub_ != nullptr)
    this->hub_->remote_button(code);  // fallback without passthrough: app commands

  const uint8_t lang = this->hub_ != nullptr ? this->hub_->language() : 0;
  if (this->last_button_sensor_ != nullptr && this->button_texts_[lang] != nullptr) {
    const char *name = this->button_texts_[lang][idx >= 0 ? idx : BUTTON_COUNT];
    const char *st = this->state_texts_[lang][state <= 2 ? state : 0];
    this->last_button_sensor_->publish_state(str_sprintf("%s (%s)", name, st));
  }
  if (this->button_event_ != nullptr) {
    if (idx < 0)
      this->button_event_->trigger("unknown");
    else if (state == 0x01)
      this->button_event_->trigger(std::string(BUTTON_KEYS[idx]) + "_long");
    else
      this->button_event_->trigger(BUTTON_KEYS[idx]);
  }
}

}  // namespace esphome::geberit_aquaclean

#endif
