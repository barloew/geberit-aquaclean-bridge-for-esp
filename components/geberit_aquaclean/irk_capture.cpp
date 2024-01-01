// IRK capture: the phone pairs with the ESP once, and hands over its Identity Resolving Key. With it the ESP
// recognizes the phone itself, whatever random address it uses (no Home Assistant needed for that).
//
// For the capture the ESP advertises for at most two minutes with a fresh random
// address as a heart rate sensor (iPhone, Apple Watch) or a keyboard (Android); its characteristics need
// encryption, so the phone pairs (Just Works) and distributes its identity key. Afterwards the bond is
// removed again and the ESP advertises as the toilet as before.
#ifdef USE_ESP32

#include "log_text.h"
#include "remote_link.h"
#include "hub.h"

#include <array>
#include <cstring>
#include <vector>

#include <esp_gap_ble_api.h>
#include <esp_random.h>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean.irk";

static const uint16_t IRK_GATTS_APP_ID = 0x7C;
static const uint32_t IRK_CAPTURE_MS = 120000;
static const uint32_t IRK_DONE_DELAY = 1500;  // after the key: let the pairing finish, then disconnect

// --- GATT services, built step by step (ESPHome hands the events over later, so the handle list of
// esp_ble_gatts_create_attr_tab is gone by then; the classic calls report every handle in the event)
static const uint16_t UUID_HR_SVC = 0x180D;
static const uint16_t UUID_HID_SVC = 0x1812;
static const esp_gatt_perm_t P_RE = ESP_GATT_PERM_READ_ENCRYPTED;
static const esp_gatt_perm_t P_WE = ESP_GATT_PERM_WRITE_ENCRYPTED;
static const esp_gatt_perm_t P_RWE = ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED;
static const uint8_t PROP_R = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t PROP_RN = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t PROP_WNR = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t PROP_RWNR = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

static uint8_t hr_meas[2] = {0x00, 60};
static uint8_t hr_loc[1] = {0x01};
static uint8_t cccd_hr[2] = {0, 0};
static uint8_t hid_info[4] = {0x11, 0x01, 0x00, 0x02};
static uint8_t hid_ctrl[1] = {0};
static uint8_t hid_proto[1] = {1};
static uint8_t hid_report[8] = {0};
static uint8_t cccd_hid[2] = {0, 0};
static uint8_t hid_ref[2] = {0x01, 0x01};  // report id 1, input
static uint8_t hid_map[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0,
                            0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
                            0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
                            0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0};

struct IrkStep {
  uint8_t svc;  // 0 heart rate, 1 keyboard
  bool descr;   // descriptor of the previous characteristic
  uint16_t uuid;
  uint8_t prop;
  esp_gatt_perm_t perm;
  uint8_t *value;
  uint16_t len;
};
static const IrkStep IRK_STEPS[] = {
    {0, false, 0x2A37, PROP_RN, P_RE, hr_meas, sizeof(hr_meas)},
    {0, true, ESP_GATT_UUID_CHAR_CLIENT_CONFIG, 0, P_RWE, cccd_hr, sizeof(cccd_hr)},
    {0, false, 0x2A38, PROP_R, P_RE, hr_loc, sizeof(hr_loc)},
    {1, false, 0x2A4A, PROP_R, P_RE, hid_info, sizeof(hid_info)},
    {1, false, 0x2A4B, PROP_R, P_RE, hid_map, sizeof(hid_map)},
    {1, false, 0x2A4C, PROP_WNR, P_WE, hid_ctrl, sizeof(hid_ctrl)},
    {1, false, 0x2A4E, PROP_RWNR, P_RWE, hid_proto, sizeof(hid_proto)},
    {1, false, 0x2A4D, PROP_RN, P_RE, hid_report, sizeof(hid_report)},
    {1, true, ESP_GATT_UUID_CHAR_CLIENT_CONFIG, 0, P_RWE, cccd_hid, sizeof(cccd_hid)},
    {1, true, 0x2908, 0, P_RE, hid_ref, sizeof(hid_ref)},
};
static const uint8_t IRK_NUM_STEPS = sizeof(IRK_STEPS) / sizeof(IRK_STEPS[0]);

static void irk_create_service(esp_gatt_if_t gatts_if, uint8_t svc) {
  esp_gatt_srvc_id_t id{};
  id.is_primary = true;
  id.id.inst_id = svc;
  id.id.uuid.len = ESP_UUID_LEN_16;
  id.id.uuid.uuid.uuid16 = svc == 0 ? UUID_HR_SVC : UUID_HID_SVC;
  esp_ble_gatts_create_service(gatts_if, &id, svc == 0 ? 8 : 16);
}

// --- advertisement ---------------------------------------------------------------------------------
static const char NAME_HR[] = "AquaClean IRK";
static const char NAME_KB[] = "AquaClean Keyboard";

static size_t build_adv(uint8_t *out, bool android, bool scan_rsp) {
  size_t n = 0;
  const char *name = android ? NAME_KB : NAME_HR;
  const size_t nl = strlen(name);
  if (!scan_rsp) {
    out[n++] = 2;
    out[n++] = 0x01;  // flags
    out[n++] = 0x06;
    out[n++] = 3;
    out[n++] = 0x19;  // appearance
    out[n++] = android ? 0xC1 : 0x40;
    out[n++] = 0x03;  // 0x03C1 keyboard, 0x0340 heart rate sensor
    out[n++] = 3;
    out[n++] = 0x03;  // complete list of 16-bit service UUIDs
    out[n++] = android ? 0x12 : 0x0D;
    out[n++] = 0x18;
  }
  if (n + 2 + nl <= 31) {
    out[n++] = static_cast<uint8_t>(nl + 1);
    out[n++] = 0x09;  // complete local name
    memcpy(out + n, name, nl);
    n += nl;
  }
  return n;
}

// ---------------------------------------------------------------------------------------------------

static bool bond_known(const std::vector<std::array<uint8_t, 6>> &list, const uint8_t *bda) {
  for (auto &b : list)
    if (memcmp(b.data(), bda, 6) == 0) return true;
  return false;
}

void RemoteLink::start_irk_capture(uint8_t user, bool android) {
  if (this->gatts_if_ == ESP_GATT_IF_NONE) {
    ESP_LOGW(TAG, LT("Bluetooth not ready yet"));
    return;
  }
  if (this->irk_active_) this->stop_irk_capture_("restarted");
  this->irk_active_ = true;
  this->irk_user_ = user;
  this->irk_android_ = android;
  this->irk_until_ = millis() + IRK_CAPTURE_MS;
  this->irk_done_at_ = 0;
  this->irk_peer_set_ = false;
  this->irk_got_ = false;
  this->irk_look_until_ = 0;
  this->irk_advertising_ = false;
  // bonds that exist now (toilet, remote control): the new one after the pairing is the phone
  this->irk_bonds_before_.clear();
  int n = esp_ble_get_bond_device_num();
  if (n > 0) {
    std::vector<esp_ble_bond_dev_t> list(n);
    if (esp_ble_get_bond_device_list(&n, list.data()) == ESP_OK)
      for (int k = 0; k < n; k++) {
        std::array<uint8_t, 6> a;
        memcpy(a.data(), list[k].bd_addr, 6);
        this->irk_bonds_before_.push_back(a);
      }
  }
  ESP_LOGI(TAG, LT("IRK capture for profile %u: on the %s, pair with \"%s\" within %u s"), user,
           android ? LT("Android phone (Bluetooth settings)") : LT("iPhone (Settings > Bluetooth)"),
           android ? NAME_KB : NAME_HR, (unsigned)(IRK_CAPTURE_MS / 1000));
  if (this->adv_running_) {
    esp_ble_gap_stop_advertising();
    this->adv_running_ = false;
  }
  // A random address can only be set while nothing scans: pause the scanner (irk_loop_ goes on)
  auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
  if (tracker != nullptr) tracker->stop_scan();
  this->irk_addr_set_ = false;
  if (!this->irk_registered_) {
    this->irk_registered_ = true;
    esp_err_t err = esp_ble_gatts_app_register(IRK_GATTS_APP_ID);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, LT("GATT app not registered: %d"), err);
      this->irk_registered_ = false;
      this->stop_irk_capture_("Bluetooth error");
    }
  }
}

void RemoteLink::irk_begin_() {
  // services ready and address set: the chosen service on, the other off, then advertise
  if (this->irk_advertising_ || !this->irk_addr_set_ || this->irk_svc_handle_[1] == 0 || !this->irk_svc_ready_) return;
  this->irk_advertising_ = true;
  esp_ble_gatts_stop_service(this->irk_svc_handle_[this->irk_android_ ? 0 : 1]);
  esp_ble_gatts_start_service(this->irk_svc_handle_[this->irk_android_ ? 1 : 0]);
  uint8_t adv[31];
  const size_t n = build_adv(adv, this->irk_android_, false);
  esp_ble_gap_config_adv_data_raw(adv, n);  // scan response and start follow in irk_gap_event_
}

void RemoteLink::stop_irk_capture_(const char *why) {
  if (!this->irk_active_) return;
  this->irk_active_ = false;
  ESP_LOGI(TAG, LT("IRK capture ended: %s"), LT(why));
  if (this->irk_peer_set_) esp_ble_gap_disconnect(this->irk_peer_);
  this->irk_peer_set_ = false;
  // the ESP does not stay paired with the phone
  int n = esp_ble_get_bond_device_num();
  if (n > 0) {
    std::vector<esp_ble_bond_dev_t> list(n);
    if (esp_ble_get_bond_device_list(&n, list.data()) == ESP_OK)
      for (int k = 0; k < n; k++)
        if (!bond_known(this->irk_bonds_before_, list[k].bd_addr)) esp_ble_remove_bond_device(list[k].bd_addr);
  }
  if (this->irk_svc_handle_[0] != 0) esp_ble_gatts_stop_service(this->irk_svc_handle_[this->irk_android_ ? 1 : 0]);
  if (this->adv_running_) {
    esp_ble_gap_stop_advertising();
    this->adv_running_ = false;
  }
  auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
  if (tracker != nullptr && !this->irk_addr_set_) {
    tracker->set_scan_continuous(true);  // stopped before the address was set: scan again
    tracker->start_scan();
  }
  this->apply_adv_data_();  // back to the toilet advertisement (public address)
  if (this->hub_ != nullptr) this->hub_->irk_capture_ended(this->irk_got_);
}

bool RemoteLink::irk_read_from_bonds_() {
  // The new bond is the phone; Bluedroid keeps its identity key with it
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) return false;
  std::vector<esp_ble_bond_dev_t> list(n);
  if (esp_ble_get_bond_device_list(&n, list.data()) != ESP_OK) return false;
  for (int k = 0; k < n; k++) {
    if (bond_known(this->irk_bonds_before_, list[k].bd_addr)) continue;
    if (!(list[k].bond_key.key_mask & ESP_BLE_ID_KEY_MASK)) continue;
    const uint8_t *irk = list[k].bond_key.pid_key.irk;
    bool valid = false;
    for (int b = 0; b < 16; b++) valid |= irk[b] != 0x00 && irk[b] != 0xFF;
    if (!valid) continue;
    this->irk_got_ = true;
    if (this->hub_ != nullptr) this->hub_->set_user_irk(this->irk_user_, irk);  // LSB first, as received
    return true;
  }
  return false;
}

void RemoteLink::irk_loop_(uint32_t now) {
  if (!this->irk_active_) return;
  if (!this->irk_addr_set_) {
    auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
    const auto st = tracker != nullptr ? tracker->get_scanner_state() : ble_device_base::ScannerState::IDLE;
    if (st == ble_device_base::ScannerState::IDLE || st == ble_device_base::ScannerState::STOPPED) {
      esp_bd_addr_t a;
      esp_fill_random(a, sizeof(a));
      a[0] |= 0xC0;  // random static: the phone sees a new device
      esp_err_t err = esp_ble_gap_set_rand_addr(a);
      this->irk_addr_set_ = true;
      if (tracker != nullptr) {
        tracker->set_scan_continuous(true);
        tracker->start_scan();
      }
      if (err != ESP_OK) {
        this->stop_irk_capture_("random address not set");
        return;
      }
      this->irk_begin_();
    }
  }
  if (this->irk_look_until_ != 0) {
    if (this->irk_read_from_bonds_()) {
      this->irk_look_until_ = 0;
      this->irk_done_at_ = now + IRK_DONE_DELAY;
    } else if (static_cast<int32_t>(now - this->irk_look_until_) >= 0) {
      this->irk_look_until_ = 0;
      ESP_LOGW(TAG, LT("Paired, but no IRK from the phone"));
      this->irk_done_at_ = now;
    }
  }
  if (this->irk_done_at_ != 0 && static_cast<int32_t>(now - this->irk_done_at_) >= 0) {
    this->stop_irk_capture_(this->irk_got_ ? "IRK received" : "no IRK received");
    return;
  }
  if (static_cast<int32_t>(now - this->irk_until_) >= 0) this->stop_irk_capture_("time is up, the phone did not pair");
}

bool RemoteLink::irk_is_peer_(const uint8_t *bda) const {
  return this->irk_active_ && this->irk_peer_set_ && memcmp(bda, this->irk_peer_, 6) == 0;
}

bool RemoteLink::irk_on_connect_(const uint8_t *bda) {
  // An incoming connection during the capture that is not the remote control: the phone
  if (!this->irk_active_ || this->is_remote_(bda)) return false;
  memcpy(this->irk_peer_, bda, 6);
  this->irk_peer_set_ = true;
  ESP_LOGI(TAG, LT("Phone connected (%s), waiting for pairing"), ob_mac(bda));
  return true;
}

bool RemoteLink::irk_gap_event_(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (!this->irk_active_) return false;
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
      if (!this->irk_advertising_) return false;
      uint8_t rsp[31];
      const size_t n = build_adv(rsp, this->irk_android_, true);
      esp_ble_gap_config_scan_rsp_data_raw(rsp, n);
      return true;
    }
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
      if (!this->irk_advertising_) return false;
      this->start_advertising_();
      return true;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      if (!this->irk_advertising_) return false;
      if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        ESP_LOGI(TAG, LT("Advertising as %s"), this->irk_android_ ? NAME_KB : NAME_HR);
      } else {
        ESP_LOGW(TAG, LT("Advertising not started: %d"), param->adv_start_cmpl.status);
        this->adv_running_ = false;
      }
      return true;
    case ESP_GAP_BLE_SEC_REQ_EVT:
      if (!this->irk_is_peer_(param->ble_security.ble_req.bd_addr)) return false;
      ESP_LOGI(TAG, LT("Phone starts pairing"));
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      return true;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      if (!this->irk_is_peer_(param->ble_security.auth_cmpl.bd_addr)) return false;
      if (param->ble_security.auth_cmpl.success) {
        ESP_LOGI(TAG, LT("Phone paired, reading its IRK"));
        this->irk_look_until_ = millis() + 3000;  // the bond is stored right after this event
      } else {
        ESP_LOGW(TAG, LT("Pairing with the phone failed, reason 0x%02x"), param->ble_security.auth_cmpl.fail_reason);
      }
      return true;
    default:
      return false;
  }
}

void RemoteLink::irk_add_step_() {
  // next characteristic / descriptor of the service being built; then the next service
  while (this->irk_step_ < IRK_NUM_STEPS) {
    const IrkStep &st = IRK_STEPS[this->irk_step_];
    if (st.svc != this->irk_building_) {
      break;
    }
    esp_bt_uuid_t u{};
    u.len = ESP_UUID_LEN_16;
    u.uuid.uuid16 = st.uuid;
    esp_attr_value_t v{};
    v.attr_max_len = st.len;
    v.attr_len = st.len;
    v.attr_value = st.value;
    esp_attr_control_t ctl{};
    ctl.auto_rsp = ESP_GATT_AUTO_RSP;
    const uint16_t svc = this->irk_svc_handle_[st.svc];
    esp_err_t err = st.descr ? esp_ble_gatts_add_char_descr(svc, &u, st.perm, &v, &ctl)
                             : esp_ble_gatts_add_char(svc, &u, st.perm, st.prop, &v, &ctl);
    if (err != ESP_OK) ESP_LOGE(TAG, LT("Adding attribute %u failed: %d"), this->irk_step_, err);
    return;  // continues at the ADD event
  }
  if (this->irk_building_ == 0) {
    this->irk_building_ = 1;
    irk_create_service(this->irk_gatts_if_, 1);
    return;
  }
  this->irk_svc_ready_ = true;
  ESP_LOGD(TAG, LT("Heart rate and keyboard services ready (handles 0x%04x, 0x%04x)"), this->irk_svc_handle_[0],
           this->irk_svc_handle_[1]);
  if (this->irk_active_) this->irk_begin_();
}

void RemoteLink::irk_gatts_event_(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  if (event == ESP_GATTS_REG_EVT) {
    if (param->reg.app_id != IRK_GATTS_APP_ID) return;
    if (param->reg.status != ESP_GATT_OK) {
      ESP_LOGE(TAG, LT("GATT app registration failed (%d)"), param->reg.status);
      this->stop_irk_capture_("Bluetooth error");
      return;
    }
    this->irk_gatts_if_ = gatts_if;
    this->irk_building_ = 0;
    this->irk_step_ = 0;
    irk_create_service(gatts_if, 0);
    return;
  }
  if (gatts_if != this->irk_gatts_if_ || this->irk_gatts_if_ == ESP_GATT_IF_NONE) return;
  switch (event) {
    case ESP_GATTS_CREATE_EVT:
      if (param->create.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, LT("GATT service %u not created (%d)"), this->irk_building_, param->create.status);
        this->stop_irk_capture_("Bluetooth error: service not created");
        return;
      }
      this->irk_svc_handle_[this->irk_building_] = param->create.service_handle;
      this->irk_add_step_();
      return;
    case ESP_GATTS_ADD_CHAR_EVT:
    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
      const esp_gatt_status_t status =
          event == ESP_GATTS_ADD_CHAR_EVT ? param->add_char.status : param->add_char_descr.status;
      if (status != ESP_GATT_OK) {
        ESP_LOGE(TAG, LT("GATT attribute %u not added (%d)"), this->irk_step_, status);
        this->stop_irk_capture_("Bluetooth error: attribute not added");
        return;
      }
      this->irk_step_++;
      this->irk_add_step_();
      return;
    }
    default:
      return;
  }
}

}  // namespace geberit_aquaclean
}  // namespace esphome

#endif  // USE_ESP32
