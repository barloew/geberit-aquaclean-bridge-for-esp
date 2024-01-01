// The ESP as remote control towards the toilet (GATT server), plus the app service the Geberit Home app uses.
// On the permanent connection (ESP = central, speaking the app protocol) the ESP offers the three services of
// the remote control. The toilet pairs with it like with a real remote control (power-on window, Just Works)
// and then writes screens and status bytes to the ESP; they are passed on unchanged to the real remote
// control, and its buttons go to the toilet as notifications.
#include "log_text.h"
#include "remote_link.h"
#include "hub.h"

#ifdef USE_ESP32

#include <cstring>

#include <esp_gap_ble_api.h>
#include <esp_gatt_common_api.h>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean.toilet";

static const uint16_t TOILET_GATTS_APP_ID = 0x7B;
static const uint32_t TOILET_PAIRING_WINDOW = 180000;

// --- UUIDs, in byte order as on air (little endian) ---
static const uint16_t UUID_DIS = 0x180A;

static const uint8_t SVC_BUTTONS[16] = {0x26, 0xc5, 0x2c, 0x96, 0x87, 0x9a, 0x43, 0x5a,
                                        0x92, 0x27, 0xd7, 0x97, 0x19, 0x3b, 0xdc, 0x94};
static const uint8_t CHR_BUTTONS[16] = {0x5a, 0x4d, 0x40, 0x6b, 0xb2, 0x10, 0x47, 0xba,
                                        0xb7, 0xe6, 0xdb, 0x6b, 0x9f, 0x2e, 0x99, 0x97};
static const uint8_t SVC_SCREEN[16] = {0xdb, 0xe0, 0x96, 0xdf, 0xae, 0x93, 0x48, 0x57,
                                       0xbf, 0xe1, 0x92, 0x9a, 0x99, 0xa5, 0x28, 0xb2};
static const uint8_t CHR_0021[16] = {0x25, 0xdc, 0xdf, 0xd2, 0x88, 0x67, 0x48, 0xda,
                                     0xb1, 0xd6, 0x1b, 0x59, 0x85, 0xc4, 0xf2, 0x59};
static const uint8_t CHR_0024[16] = {0x86, 0x77, 0x10, 0xfb, 0x5e, 0x31, 0x49, 0xba,
                                     0x84, 0xe0, 0xa1, 0x0d, 0x5d, 0x83, 0x2a, 0xd7};
static const uint8_t CHR_0026[16] = {0x71, 0x52, 0xf4, 0xa9, 0x65, 0x23, 0x45, 0x17,
                                     0x80, 0xa2, 0x96, 0xd8, 0xb9, 0x27, 0x35, 0x38};
static const uint8_t CHR_0028[16] = {0x46, 0x4e, 0xad, 0x99, 0xec, 0x2c, 0x49, 0xd4,
                                     0xa1, 0x86, 0xaf, 0x6f, 0xf8, 0x97, 0x9a, 0x96};
static const uint8_t CHR_002A[16] = {0x0e, 0x06, 0x9b, 0x0a, 0x96, 0x7c, 0x40, 0x02,
                                     0x91, 0xac, 0x1e, 0x51, 0x90, 0x6a, 0x84, 0xb2};
static const uint8_t SVC_FOURTH[16] = {0x30, 0x8a, 0x11, 0x55, 0xb7, 0xb7, 0x43, 0x52,
                                       0xa7, 0x25, 0x4c, 0x4a, 0x01, 0x1d, 0xce, 0xb6};
static const uint8_t CHR_0038[16] = {0x1d, 0xb5, 0x12, 0xc1, 0x2a, 0xa1, 0x45, 0xd7,
                                     0x89, 0x4e, 0x1e, 0x94, 0x41, 0xbc, 0x83, 0x89};
static const uint8_t CHR_003A[16] = {0x02, 0x00, 0x13, 0xac, 0x42, 0x02, 0x21, 0x96,
                                     0xec, 0x11, 0xdd, 0x16, 0x4c, 0x0a, 0xd3, 0xa8};
static const uint8_t CHR_003C[16] = {0x02, 0x00, 0x13, 0xac, 0x42, 0x02, 0x21, 0x96,
                                     0xec, 0x11, 0xdd, 0x16, 0x64, 0x0b, 0xd3, 0xa8};

// App service of the toilet (3334429d-90f3-4c41-a02d-5cb3a03e0000), characteristics a1..a8: a1-a4 write,
// a5-a8 notify. The app writes frames to a1/a2 and gets answers via a5-a8 (recording sela_app).
static uint8_t SVC_APP[16] = {0x00, 0x00, 0x3e, 0xa0, 0xb3, 0x5c, 0x2d, 0xa0,
                              0x41, 0x4c, 0xf3, 0x90, 0x9d, 0x42, 0x34, 0x33};
static uint8_t CHR_APP[8][16];
static void init_app_uuids() {
  for (int i = 0; i < 8; i++) {
    memcpy(CHR_APP[i], SVC_APP, 16);
    CHR_APP[i][3] = 0xa1 + i;
  }
}

static const uint8_t PROP_R = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t PROP_RN = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t PROP_RWW =
    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t PROP_WW = ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t PROP_W = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t PROP_N = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t PROP_RWN =
    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// Device Information of the remote control. The toilet reads the model number (2a24) on every central to tell
// the remote control from the app; we answer it ourselves so the log shows when the toilet recognizes us.
static const char FW_STRING[] = "3.60.101.900/0001";
static uint8_t cccd_buttons[2] = {0, 0};
static uint8_t cccd_0021[2] = {0, 0};
static uint8_t cccd_003c[2] = {0, 0};
static uint8_t cccd_app[4][2];
static uint8_t val_zero[20] = {0};

static const esp_gatt_perm_t PERM_R = ESP_GATT_PERM_READ;
static const esp_gatt_perm_t PERM_RW = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;
static const uint16_t MAX_VAL = 64;

// Services are created step by step (create_service / add_char / add_char_descr): ESPHome queues GATTS events
// and does not copy the handle list of create_attr_tab, so that route gives no usable handles.
struct CharDef {
  uint8_t svc;      // 0 DIS, 1 buttons, 2 screen, 3 fourth, 4 app
  uint16_t uuid16;  // 0 = 128-bit
  const uint8_t *uuid128;
  uint8_t prop;
  bool cccd;
  bool rsp_by_app;
  uint8_t val_idx;  // index in gatts_handles_[svc]
};
static const CharDef CHARS[] = {
    {0, 0x2A29, nullptr, PROP_R, false, false, 2},
    {0, 0x2A24, nullptr, PROP_R, false, true, 4},
    {0, 0x2A25, nullptr, PROP_R, false, false, 6},
    {0, 0x2A27, nullptr, PROP_R, false, false, 8},
    {0, 0x2A26, nullptr, PROP_R, false, false, 10},
    {0, 0x2A28, nullptr, PROP_R, false, false, 12},
    {0, 0x2A23, nullptr, PROP_R, false, false, 14},
    {1, 0, CHR_BUTTONS, PROP_RN, true, false, 2},
    {2, 0, CHR_0021, PROP_RN, true, false, 2},
    {2, 0, CHR_0024, PROP_RWW, false, false, 5},
    {2, 0, CHR_0026, PROP_WW, false, false, 7},
    {2, 0, CHR_0028, PROP_RWW, false, false, 9},
    {2, 0, CHR_002A, PROP_WW, false, false, 11},
    {3, 0, CHR_0038, PROP_RWW, false, false, 2},
    {3, 0, CHR_003A, PROP_W, false, false, 4},
    {3, 0, CHR_003C, PROP_RWN, true, false, 6},
    // app service: a1-a4 write (val 2, 4, 6, 8), a5-a8 notify with CCCD (val 10/13/16/19)
    {4, 0, CHR_APP[0], PROP_WW, false, false, 2},
    {4, 0, CHR_APP[1], PROP_WW, false, false, 4},
    {4, 0, CHR_APP[2], PROP_WW, false, false, 6},
    {4, 0, CHR_APP[3], PROP_WW, false, false, 8},
    {4, 0, CHR_APP[4], PROP_N, true, false, 10},
    {4, 0, CHR_APP[5], PROP_N, true, false, 13},
    {4, 0, CHR_APP[6], PROP_N, true, false, 16},
    {4, 0, CHR_APP[7], PROP_N, true, false, 19},
};
static const uint8_t NUM_CHARS = sizeof(CHARS) / sizeof(CHARS[0]);
static const uint8_t NUM_SERVICES = 5;
static const uint16_t SVC_HANDLES[NUM_SERVICES] = {15, 4, 12, 8, 21};

static void create_service(esp_gatt_if_t gatts_if, uint8_t svc) {
  esp_gatt_srvc_id_t id{};
  id.is_primary = true;
  id.id.inst_id = svc;
  if (svc == 0) {
    id.id.uuid.len = ESP_UUID_LEN_16;
    id.id.uuid.uuid.uuid16 = UUID_DIS;
  } else {
    id.id.uuid.len = ESP_UUID_LEN_128;
    memcpy(id.id.uuid.uuid.uuid128,
           svc == 1   ? SVC_BUTTONS
           : svc == 2 ? SVC_SCREEN
           : svc == 3 ? SVC_FOURTH
                      : SVC_APP,
           16);
  }
  esp_err_t err = esp_ble_gatts_create_service(gatts_if, &id, SVC_HANDLES[svc]);
  if (err != ESP_OK) ESP_LOGE(TAG, LT("GATT server: creating service %u failed (%d)"), svc, err);
}

// Characteristic values the toilet writes -> handle on the real remote control
struct ForwardDef {
  uint8_t svc;
  uint8_t idx;
  uint16_t remote_handle;
  bool with_response;
};
static const ForwardDef FORWARDS[] = {
    {2, 5, 0x0024, false}, {2, 7, 0x0026, false}, {2, 9, 0x0028, false}, {2, 11, 0x002a, false},
    {3, 2, 0x0038, false}, {3, 4, 0x003a, true},  {3, 6, 0x003c, true},
};

static std::string hex_of(const uint8_t *d, uint16_t len) {
  std::string s;
  char b[4];
  for (uint16_t i = 0; i < len && i < 24; i++) {
    snprintf(b, sizeof(b), "%02x", d[i]);
    s += b;
  }
  if (len > 24) s += "..";
  return s;
}

// ---------------------------------------------------------------------------------------------------
// Pairing with the toilet as remote control
// ---------------------------------------------------------------------------------------------------

const uint8_t *RemoteLink::toilet_bda_() const {
  if (this->hub_ == nullptr || this->hub_->parent() == nullptr) return nullptr;
  return this->hub_->parent()->get_remote_bda();
}

bool RemoteLink::toilet_bonded_() const {
  const uint8_t *bda = this->toilet_bda_();
  if (bda == nullptr) return false;
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return false;
  std::vector<esp_ble_bond_dev_t> list(n);
  if (esp_ble_get_bond_device_list(&n, list.data()) != ESP_OK) return false;
  for (int i = 0; i < n; i++) {
    if (memcmp(list[i].bd_addr, bda, 6) == 0) return true;
  }
  return false;
}

bool RemoteLink::is_passthrough() const {
  return this->forward_ && this->toilet_conn_id_ >= 0 && this->toilet_encrypted_ && this->toilet_sub_buttons_;
}

void RemoteLink::start_toilet_pairing() {
  this->pair_toilet_until_ = millis() + TOILET_PAIRING_WINDOW;
  this->pair_toilet_next_ = 0;
  this->pair_wait_aa_ = true;
  this->toilet_read_seen_ = false;
  if (this->hub_ != nullptr && this->hub_->parent() != nullptr) {
    // Do not connect before the toilet opens its pairing window (manufacturer data starts with aa)
    this->hub_->parent()->set_auto_connect(false);
    this->hub_->parent()->disconnect();
  }
  ESP_LOGI(TAG, LT("Pairing as remote control: switch the toilet off for 30 s and on again now (window 180 s)"));
}

bool RemoteLink::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (this->hub_ == nullptr || this->hub_->parent() == nullptr) return false;
  this->hub_->on_advert(device);  // user devices, device search
  if (this->auto_toilet_ && this->hub_->parent()->get_address() == 0) {
    this->toilet_candidate_(device);
    return false;
  }
  if (device.address_uint64() != this->hub_->parent()->get_address()) return false;
  int mode = -1;
  for (const auto &m : device.get_manufacturer_datas()) {
    mode = m.uuid.get_uuid().uuid.uuid16 & 0xFF;  // first byte of the manufacturer data
    break;
  }
  if (mode != this->toilet_adv_mode_) {
    this->toilet_adv_mode_ = mode;
    ESP_LOGI(TAG, LT("Toilet advertises mode 0x%02x%s"), mode & 0xFF, mode == 0xAA ? LT(" (pairing window)") : LT(""));
  }
  // Full advertisement for discovery (other models): name, manufacturer data, services
  std::string desc = str_sprintf("name '%s'", device.get_name().c_str());
  for (const auto &m : device.get_manufacturer_datas()) {
    const uint16_t id = m.uuid.get_uuid().uuid.uuid16;
    desc += str_sprintf(", mfr %02x %02x", id & 0xFF, id >> 8);
    for (uint8_t b : m.data) desc += str_sprintf(" %02x", b);
  }
  for (const auto &u : device.get_service_uuids()) {
    char ub[64];
    desc += ", service ";
    desc += u.to_str(ub);
  }
  for (const auto &sd : device.get_service_datas()) {
    char ub[64];
    desc += ", service data ";
    desc += sd.uuid.to_str(ub);
    desc += ":";
    for (uint8_t b : sd.data) desc += str_sprintf(" %02x", b);
  }
  if (desc != this->toilet_adv_desc_) {
    this->toilet_adv_desc_ = desc;
    this->hub_->publish_advertisement(desc);
  }
  if (this->pair_wait_aa_ && mode == 0xAA) {
    this->pair_wait_aa_ = false;
    ESP_LOGI(TAG, LT("Pairing window of the toilet open (aa), ESP connects"));
    this->hub_->parent()->set_auto_connect(true);
  }
  return false;
}

void RemoteLink::toilet_loop_(uint32_t now) {
  const bool ble_up = esp32_ble::global_ble != nullptr && esp32_ble::global_ble->is_active();
  // Bond watchdog: if the bond with the toilet disappears while the connection stays up, Bluedroid has no
  // security record for it any more and logs "BTM_GetSecurityFlags failed" on every request, for hours.
  // Connecting again creates the record again.
  if (now - this->bond_check_at_ >= 30000) {
    this->bond_check_at_ = now;
    const bool connected =
        this->hub_ != nullptr && this->hub_->parent() != nullptr && this->hub_->parent()->connected();
    const bool bonded = connected && this->toilet_bonded_();
    if (connected && this->toilet_was_bonded_ && !bonded) {
      ESP_LOGW(TAG, LT("Bond with the toilet lost while connected: connecting again"));
      this->hub_->parent()->disconnect();
    }
    this->toilet_was_bonded_ = bonded;
  }
  if (ble_up && !this->gatts_registered_) {
    this->gatts_registered_ = true;
    esp_err_t err = esp_ble_gatts_app_register(TOILET_GATTS_APP_ID);
    if (err != ESP_OK) ESP_LOGE(TAG, LT("Registering GATT server failed: %d"), err);
  }

  if (this->pair_toilet_until_ != 0 && static_cast<int32_t>(now - this->pair_toilet_until_) >= 0) {
    ESP_LOGW(TAG, LT("Pairing window as remote control expired without pairing%s"),
             this->pair_wait_aa_ ? LT(" (toilet never sent aa: was it switched off and on?)") : LT(""));
    this->pair_toilet_until_ = 0;
    this->pair_wait_aa_ = false;
    if (this->hub_ != nullptr && this->hub_->parent() != nullptr) this->hub_->parent()->set_auto_connect(true);
  }

  const uint8_t *bda = this->toilet_bda_();
  const bool pairing = this->pair_toilet_until_ != 0;
  if (pairing && !this->pair_wait_aa_ && bda != nullptr && this->toilet_conn_id_ >= 0 && !this->toilet_encrypted_ &&
      this->pair_toilet_next_ != 0) {
    // The real remote control asks to pair ~0.5 s after connecting while the toilet discovers its services;
    // ESPHome's own service discovery (3 s) is not awaited
    if (this->toilet_read_seen_ || static_cast<int32_t>(now - this->pair_toilet_next_) >= 0) {
      this->pair_toilet_next_ = 0;  // one attempt per connection
      ESP_LOGI(TAG, LT("Pairing request to the toilet (%s)"),
               this->toilet_read_seen_ ? LT("after it read the device information") : LT("without read"));
      esp_ble_set_encryption(const_cast<uint8_t *>(bda), ESP_BLE_SEC_ENCRYPT_NO_MITM);
    }
  }

  // Already paired: encrypt right after connecting, like the real remote control. The toilet recognizes the
  // ESP by the model number and drops an unencrypted "remote control" within a second.
  if (!pairing && bda != nullptr && this->toilet_conn_id_ >= 0 && !this->toilet_encrypted_ &&
      !this->toilet_enc_requested_ && this->toilet_bonded_()) {
    this->toilet_enc_requested_ = true;
    ESP_LOGI(TAG, LT("Encrypting the connection to the toilet (paired as remote control)"));
    esp_ble_set_encryption(const_cast<uint8_t *>(bda), ESP_BLE_SEC_ENCRYPT_NO_MITM);
  }
}

bool RemoteLink::toilet_gap_event_(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  const uint8_t *bda = this->toilet_bda_();
  if (bda == nullptr) return false;
  if (event == ESP_GAP_BLE_SEC_REQ_EVT) {
    if (memcmp(param->ble_security.ble_req.bd_addr, bda, 6) != 0) return false;
    ESP_LOGI(TAG, LT("Toilet asks for security"));
    esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
    return true;
  }
  if (event != ESP_GAP_BLE_AUTH_CMPL_EVT) return false;
  if (memcmp(param->ble_security.auth_cmpl.bd_addr, bda, 6) != 0) return false;
  if (!param->ble_security.auth_cmpl.success) {
    const uint8_t r = param->ble_security.auth_cmpl.fail_reason;
    ESP_LOGW(TAG, LT("Pairing/encryption with the toilet failed, reason 0x%02x%s"), r,
             r == 0x52 ? LT(" (toilet: pairing not supported; pair the ESP again with the toilet)") : LT(""));
    // "Pairing not supported" while the ESP has a stored key that worked before: after a dropped connection
    // Bluedroid threw away its in-memory copy of the key and tried to pair anew, which the toilet refuses
    // outside pairing mode. The key is still in flash; a restart loads it again (at most once per start).
    if (this->pair_toilet_until_ == 0 && r == 0x52 && this->toilet_enc_ok_ && this->toilet_bonded_()) {
      ESP_LOGW(TAG, LT("Bluetooth stack lost the stored key for the toilet; restarting the ESP to load it again"));
      this->reload_keys_due_ = true;
      return true;
    }
    if (this->pair_toilet_until_ != 0) {
      // Bluedroid drops the device after a failed pairing: reconnect and try again
      this->toilet_read_seen_ = false;
      if (this->hub_ != nullptr && this->hub_->parent() != nullptr) this->hub_->parent()->disconnect();
    }
    return true;
  }
  this->toilet_encrypted_ = true;
  this->toilet_enc_ok_ = true;
  const bool was_pairing = this->pair_toilet_until_ != 0;
  this->pair_toilet_until_ = 0;
  ESP_LOGI(TAG, LT("Toilet encrypted (%s)"), was_pairing ? LT("newly paired as remote control") : LT("stored key"));
  return true;
}

// ---------------------------------------------------------------------------------------------------
// GATT server
// ---------------------------------------------------------------------------------------------------

void RemoteLink::add_next_char_() {
  // next characteristic of the current service; then start the service and create the next one
  const uint8_t svc = this->gatts_next_service_;
  if (this->gatts_next_char_ < NUM_CHARS && CHARS[this->gatts_next_char_].svc == svc) {
    const CharDef &c = CHARS[this->gatts_next_char_];
    esp_bt_uuid_t u{};
    if (c.uuid128 == nullptr) {
      u.len = ESP_UUID_LEN_16;
      u.uuid.uuid16 = c.uuid16;
    } else {
      u.len = ESP_UUID_LEN_128;
      memcpy(u.uuid.uuid128, c.uuid128, 16);
    }
    esp_attr_value_t v{};
    v.attr_max_len = MAX_VAL;
    if (svc == 0) {
      v.attr_len = sizeof(FW_STRING) - 1;
      v.attr_value = (uint8_t *)FW_STRING;
    } else {
      v.attr_len = 2;
      v.attr_value = val_zero;
    }
    esp_attr_control_t ctl{};
    ctl.auto_rsp = c.rsp_by_app ? ESP_GATT_RSP_BY_APP : ESP_GATT_AUTO_RSP;
    const esp_gatt_perm_t perm =
        (c.prop & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) ? PERM_RW : PERM_R;
    esp_err_t err = esp_ble_gatts_add_char(this->gatts_handles_[svc][0], &u, perm, c.prop, &v, &ctl);
    if (err != ESP_OK)
      ESP_LOGE(TAG, LT("GATT server: adding characteristic %u failed (%d)"), this->gatts_next_char_, err);
    return;
  }
  esp_ble_gatts_start_service(this->gatts_handles_[svc][0]);
  ESP_LOGD(TAG, LT("GATT server: service %u from handle 0x%04x"), svc, this->gatts_handles_[svc][0]);
  this->gatts_next_service_ = svc + 1;
  if (this->gatts_next_service_ < NUM_SERVICES) {
    create_service(this->gatts_if_, this->gatts_next_service_);
  } else {
    ESP_LOGI(TAG, LT("Remote control and app services ready for the toilet and the app"));
  }
}

void RemoteLink::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                     esp_ble_gatts_cb_param_t *param) {
  this->irk_gatts_event_(event, gatts_if, param);
  if (event == ESP_GATTS_REG_EVT) {
    if (param->reg.app_id != TOILET_GATTS_APP_ID) return;
    if (param->reg.status != ESP_GATT_OK) {
      ESP_LOGE(TAG, LT("GATT server: registration failed (%d)"), param->reg.status);
      return;
    }
    this->gatts_if_ = gatts_if;
    init_app_uuids();
    this->gatts_next_service_ = 0;
    this->gatts_next_char_ = 0;
    create_service(gatts_if, 0);
    return;
  }
  if (gatts_if != this->gatts_if_ || this->gatts_if_ == ESP_GATT_IF_NONE) return;

  switch (event) {
    case ESP_GATTS_CREATE_EVT: {
      if (param->create.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, LT("GATT server: service %u not created (%d)"), this->gatts_next_service_, param->create.status);
        return;
      }
      this->gatts_handles_[this->gatts_next_service_][0] = param->create.service_handle;
      this->add_next_char_();
      return;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
      if (param->add_char.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, LT("GATT server: characteristic %u not added (%d)"), this->gatts_next_char_,
                 param->add_char.status);
        return;
      }
      const CharDef &c = CHARS[this->gatts_next_char_];
      this->gatts_handles_[c.svc][c.val_idx] = param->add_char.attr_handle;
      if (c.cccd) {
        esp_bt_uuid_t u{};
        u.len = ESP_UUID_LEN_16;
        u.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_attr_value_t v{};
        v.attr_max_len = 2;
        v.attr_len = 2;
        v.attr_value = c.svc == 1   ? cccd_buttons
                       : c.svc == 2 ? cccd_0021
                       : c.svc == 3 ? cccd_003c
                                    : cccd_app[(c.val_idx - 10) / 3];
        esp_attr_control_t ctl{};
        ctl.auto_rsp = ESP_GATT_AUTO_RSP;
        esp_ble_gatts_add_char_descr(this->gatts_handles_[c.svc][0], &u, PERM_RW, &v, &ctl);
        return;
      }
      this->gatts_next_char_++;
      this->add_next_char_();
      return;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
      const CharDef &c = CHARS[this->gatts_next_char_];
      this->gatts_handles_[c.svc][c.val_idx + 1] = param->add_char_descr.attr_handle;
      this->gatts_next_char_++;
      this->add_next_char_();
      return;
    }
    case ESP_GATTS_START_EVT:
      if (param->start.status != ESP_GATT_OK)
        ESP_LOGE(TAG, LT("GATT server: starting service failed (%d)"), param->start.status);
      return;
    case ESP_GATTS_CONNECT_EVT: {
      const uint8_t *bda = this->toilet_bda_();
      const bool is_toilet = bda != nullptr && memcmp(param->connect.remote_bda, bda, 6) == 0;
      if (!is_toilet && !this->is_remote_(param->connect.remote_bda) && this->irk_active_)
        return;  // IRK capture: the phone, not the app
      if (!is_toilet && !this->is_remote_(param->connect.remote_bda)) {
        // Any other device is the app. This GATTS event can come before the GATTC one (which sets app_linked_).
        this->app_conn_id_ = param->connect.conn_id;
        ESP_LOGD(TAG, LT("GATT server: app connected (conn %u)"), param->connect.conn_id);
        return;
      }
      if (is_toilet) {
        this->toilet_conn_id_ = param->connect.conn_id;
        this->toilet_read_seen_ = false;
        if (this->pair_toilet_until_ != 0 && !this->pair_wait_aa_) this->pair_toilet_next_ = millis() + 600;
        ESP_LOGD(TAG, LT("GATT server: toilet connected (conn %u)"), param->connect.conn_id);
      }
      return;
    }
    case ESP_GATTS_DISCONNECT_EVT: {
      if (static_cast<int>(param->disconnect.conn_id) == this->app_conn_id_) {
        this->app_conn_id_ = -1;
        // Backup for a missed GATTC disconnect: otherwise the app stays "connected" for ever
        if (this->app_linked_) {
          ESP_LOGI(TAG, LT("App disconnected (GATT server)"));
          this->app_linked_ = false;
          if (this->hub_ != nullptr) this->hub_->set_app_connected(false);
        }
      }
      if (static_cast<int>(param->disconnect.conn_id) == this->toilet_conn_id_) {
        this->toilet_conn_id_ = -1;
        this->toilet_sub_buttons_ = false;
        this->toilet_sub_2_ = false;
        this->toilet_encrypted_ = false;
        this->toilet_enc_requested_ = false;
        ESP_LOGD(TAG, LT("GATT server: toilet gone"));
      }
      return;
    }
    case ESP_GATTS_READ_EVT:
      if (param->read.handle == this->gatts_handles_[0][4])
        ESP_LOGI(TAG, LT("Toilet reads the model number: it recognizes the ESP as remote control"));
      if (param->read.need_rsp) {
        esp_gatt_rsp_t rsp{};
        rsp.attr_value.handle = param->read.handle;
        const uint16_t off = param->read.offset;
        const uint16_t total = sizeof(FW_STRING) - 1;
        rsp.attr_value.offset = off;
        rsp.attr_value.len = off < total ? total - off : 0;
        if (rsp.attr_value.len > 0) memcpy(rsp.attr_value.value, FW_STRING + off, rsp.attr_value.len);
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
      }
      for (uint8_t k = 1; k < 15; k++) {
        if (param->read.handle == this->gatts_handles_[0][k])
          this->toilet_read_seen_ = true;  // Device Information read
      }
      return;
    case ESP_GATTS_WRITE_EVT: {
      const uint16_t h = param->write.handle;
      const uint8_t *v = param->write.value;
      const uint16_t len = param->write.len;
      if (static_cast<int>(param->write.conn_id) == this->app_conn_id_) {
        for (uint8_t i = 0; i < 4; i++) {
          if (h == this->gatts_handles_[4][2 + 2 * i]) {
            ESP_LOGV(TAG, LT("App -> a%u: %s"), i + 1, hex_of(v, len).c_str());
            if (this->hub_ != nullptr) this->hub_->app_frame_in(i, v, len);
            return;
          }
        }
        return;  // CCCDs of the app: the stack keeps them
      }
      if (h == this->gatts_handles_[1][3]) {  // CCCD buttons (0x001e on the real remote control)
        this->toilet_sub_buttons_ = len >= 1 && (v[0] & 0x01);
        ESP_LOGI(TAG, LT("Toilet %s the buttons (0x001e)"),
                 this->toilet_sub_buttons_ ? LT("subscribes to") : LT("unsubscribes"));
        return;
      }
      if (h == this->gatts_handles_[2][3]) {  // CCCD 0x0022
        this->toilet_sub_2_ = len >= 1 && (v[0] & 0x01);
        ESP_LOGD(TAG, LT("Toilet %s 0x0022"), this->toilet_sub_2_ ? LT("subscribes to") : LT("unsubscribes"));
        return;
      }
      for (const auto &f : FORWARDS) {
        if (h != this->gatts_handles_[f.svc][f.idx]) continue;
        ESP_LOGV(TAG, LT("Toilet -> 0x%04x: %s"), f.remote_handle, hex_of(v, len).c_str());
        if (this->gatt_open_ && this->encrypted_) {
          if (f.with_response) {
            esp_ble_gattc_write_char(this->gattc_if_, this->conn_id_, f.remote_handle, len, const_cast<uint8_t *>(v),
                                     ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
          } else {
            this->write_cmd_(f.remote_handle, v, len);
          }
          this->last_activity_ = millis();
        }
        return;
      }
      ESP_LOGD(TAG, LT("Toilet writes unknown handle 0x%04x: %s"), h, hex_of(v, len).c_str());
      return;
    }
    default:
      return;
  }
}

void RemoteLink::send_to_app_(uint8_t notify_index, const uint8_t *data, uint16_t len) {
  if (this->app_conn_id_ < 0 || notify_index > 3) return;
  const uint16_t h = this->gatts_handles_[4][10 + 3 * notify_index];
  ESP_LOGV(TAG, LT("Toilet -> app a%u: %s"), notify_index + 5, hex_of(data, len).c_str());
  esp_ble_gatts_send_indicate(this->gatts_if_, static_cast<uint16_t>(this->app_conn_id_), h, len,
                              const_cast<uint8_t *>(data), false);
}

void RemoteLink::forward_to_toilet_(uint16_t remote_handle, const uint8_t *data, uint16_t len) {
  uint16_t h = 0;
  if (remote_handle == 0x001d && this->toilet_sub_buttons_)
    h = this->gatts_handles_[1][2];
  else if (remote_handle == 0x0021 && this->toilet_sub_2_)
    h = this->gatts_handles_[2][2];
  if (h == 0 || this->toilet_conn_id_ < 0) return;
  esp_ble_gatts_set_attr_value(h, len, data);
  esp_ble_gatts_send_indicate(this->gatts_if_, static_cast<uint16_t>(this->toilet_conn_id_), h, len,
                              const_cast<uint8_t *>(data), false);
}

}  // namespace esphome::geberit_aquaclean

#endif
