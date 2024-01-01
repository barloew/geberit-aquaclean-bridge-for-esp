#pragma once

#ifdef USE_ESP32

#include <array>
#include <string>
#include <vector>

#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_gatts_api.h>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "entities.h"

namespace esphome::geberit_aquaclean {

class AquaCleanHub;

/**
 * Everything that is not the app-protocol client:
 *
 *  - towards the real remote control the ESP behaves like the toilet: it advertises as "Geberit AC PRO"
 *    (0xAA in the pairing window), accepts the pairing (Just Works, bonding) and opens its own GATT client
 *    on the connection the remote control sets up (remote_link.cpp);
 *  - towards the toilet the ESP is paired as the remote control: it offers the remote's GATT services on
 *    the permanent connection, so the toilet writes screens to it and subscribes to its buttons; both are
 *    passed through unchanged (toilet_side.cpp);
 *  - the Geberit Home app connects to the ESP (which advertises as the toilet) and its frames are relayed
 *    to the toilet by the hub. After a passed-through long press of + the ESP advertises the app-adding
 *    state (third manufacturer byte 01) so the app can add the "toilet".
 */
class RemoteLink : public Component, public esp32_ble_tracker::ESPBTDeviceListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  // All GAP, GATTC and GATTS events (registered with esp32_ble, unfiltered)
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
  void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
  /// Toilet advertisements (pairing window 0xAA, discovery)
  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;

  // configuration
  void set_hub(AquaCleanHub *hub) { this->hub_ = hub; }
  void set_remote_address(uint64_t address);
  void set_idle_timeout(uint32_t ms) { this->idle_timeout_ = ms; }
  void set_pairing_duration(uint32_t ms) { this->pairing_duration_ = ms; }
  void set_remote_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_remote_passthrough_sensor(binary_sensor::BinarySensor *s) { this->passthrough_sensor_ = s; }
  void set_remote_last_button_sensor(text_sensor::TextSensor *s) { this->last_button_sensor_ = s; }
  void set_remote_firmware_sensor(text_sensor::TextSensor *s) { this->firmware_sensor_ = s; }
  void set_button_event(RemoteEvent *e) { this->button_event_ = e; }
  void set_button_texts(uint8_t lang, const char *const *names, const char *const *states) {
    if (lang < LANGUAGE_COUNT) {
      this->button_texts_[lang] = names;
      this->state_texts_[lang] = states;
    }
  }

  // control
  void set_forward(bool v) { this->forward_ = v; }
  bool get_forward() const { return this->forward_; }
  void set_advertise(bool v);
  /// Pairing window for the remote control: advertise 0xAA like the toilet after power-on
  void start_pairing();
  void stop_pairing();
  bool is_pairing() const { return this->pairing_; }
  bool remote_connected() const { return this->remote_linked_; }
  // --- peripheral-side health (read by the hub) ---
  /// Advertising as the toilet is actually running (clients can discover us)
  bool is_advertising() const { return this->adv_running_; }
  /// Advertising as the toilet is wanted (configured on)
  bool advertise_wanted() const { return this->advertise_; }
  /// A client (remote control / app) is linked to our peripheral side
  bool client_linked() const { return this->remote_linked_; }
  /// millis() of the last exchange with a linked client
  uint32_t last_client_activity() const { return this->last_activity_; }
  /// Remove the bond with the remote control (and forget a learned remote control)
  void unpair();
  /// Forget the toilet found automatically and look for the nearest one again
  void search_toilet();
  void set_toilet_address_sensor(text_sensor::TextSensor *s) { this->toilet_address_sensor_ = s; }
  void set_remote_address_sensor(text_sensor::TextSensor *s) { this->remote_address_sensor_ = s; }
  /// Pair the ESP with the toilet as remote control (toilet off and on within the window)
  void start_toilet_pairing();
  /// Screens and buttons go straight between toilet and remote control
  bool is_passthrough() const;
  /// IRK capture (irk_capture.cpp): the phone pairs once and hands over its identity key for profile `user`
  void start_irk_capture(uint8_t user, bool android);
  bool irk_capturing() const { return this->irk_active_; }

 protected:
  bool has_remote_() const { return this->address_ != 0; }
  bool is_remote_(const uint8_t *bda) const;
  void start_advertising_();
  void apply_adv_data_();
  void restore_bond_();
  void begin_session_();
  void write_cccd_(uint16_t handle);
  void write_cmd_(uint16_t handle, const uint8_t *data, uint16_t len);
  void on_button_(uint8_t code, uint8_t state);
  void set_remote_connected_(bool v);
  void reset_link_();

  // IRK capture (irk_capture.cpp)
  void irk_begin_();
  void stop_irk_capture_(const char *why);
  void irk_loop_(uint32_t now);
  bool irk_is_peer_(const uint8_t *bda) const;
  bool irk_on_connect_(const uint8_t *bda);
  bool irk_read_from_bonds_();
  void irk_add_step_();
  bool irk_gap_event_(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void irk_gatts_event_(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
  bool irk_active_{false};
  bool irk_android_{false};
  bool irk_registered_{false};
  bool irk_peer_set_{false};
  bool irk_got_{false};
  bool irk_addr_set_{false};
  bool irk_advertising_{false};
  bool irk_svc_ready_{false};
  uint8_t irk_user_{0};
  uint8_t irk_building_{0};
  uint8_t irk_step_{0};
  uint32_t irk_until_{0};
  uint32_t irk_done_at_{0};
  uint32_t irk_look_until_{0};
  esp_bd_addr_t irk_peer_{};
  esp_gatt_if_t irk_gatts_if_{ESP_GATT_IF_NONE};
  uint16_t irk_svc_handle_[2]{};
  std::vector<std::array<uint8_t, 6>> irk_bonds_before_;

  // ESP as remote control towards the toilet (toilet_side.cpp)
  static constexpr uint8_t MAX_ATTRS = 24;
  const uint8_t *toilet_bda_() const;
  bool toilet_bonded_() const;
  uint32_t bond_check_at_{0};
  bool toilet_was_bonded_{false};
  void toilet_loop_(uint32_t now);
  void add_next_char_();
  bool toilet_gap_event_(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void forward_to_toilet_(uint16_t remote_handle, const uint8_t *data, uint16_t len);
  void send_to_app_(uint8_t notify_index, const uint8_t *data, uint16_t len);

  // Automatic discovery: toilet = strongest advertiser with Geberit manufacturer data ("146..."),
  // remote control = the device that pairs during the pairing window. Both are stored.
  void toilet_candidate_(const esp32_ble_tracker::ESPBTDevice &device);
  void publish_addresses_();
  bool auto_toilet_{false};
  bool auto_remote_{false};
  ESPPreferenceObject toilet_pref_;
  ESPPreferenceObject remote_pref_;
  uint64_t cand_addr_{0};
  int cand_rssi_{-128};
  uint32_t search_end_{0};
  uint16_t app_gattc_conn_id_{0};
  text_sensor::TextSensor *toilet_address_sensor_{nullptr};
  text_sensor::TextSensor *remote_address_sensor_{nullptr};

  AquaCleanHub *hub_{nullptr};
  esp_bd_addr_t remote_bda_{};
  uint64_t address_{0};
  int8_t auto_disc_{-1};

  bool forward_{true};
  bool advertise_{true};
  bool adv_configured_{false};
  bool adv_running_{false};
  uint32_t adv_restart_at_{0};
  uint32_t adv_down_since_{0};  // advertising guardian: first loop advertising was seen unexpectedly down
  uint32_t idle_timeout_{15000};
  bool pairing_{false};
  uint32_t pairing_started_{0};
  uint32_t pairing_duration_{120000};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *passthrough_sensor_{nullptr};
  int8_t passthrough_published_{-1};
  text_sensor::TextSensor *last_button_sensor_{nullptr};
  text_sensor::TextSensor *firmware_sensor_{nullptr};
  RemoteEvent *button_event_{nullptr};
  std::array<const char *const *, LANGUAGE_COUNT> button_texts_{};
  std::array<const char *const *, LANGUAGE_COUNT> state_texts_{};

  // own GATT client app (towards the remote control)
  bool app_registered_{false};
  esp_gatt_if_t gattc_if_{ESP_GATT_IF_NONE};
  uint16_t conn_id_{0xFFFF};

  // connection with the remote control
  bool remote_linked_{false};
  bool want_open_{false};
  bool gatt_open_{false};
  bool encrypted_{false};
  bool pairing_seen_{false};
  bool session_started_{false};
  bool buttons_cccd_written_{false};
  uint32_t linked_at_{0};
  uint32_t last_activity_{0};
  uint32_t cccd2_due_{0};

  // ESP as remote control towards the toilet
  bool gatts_registered_{false};
  esp_gatt_if_t gatts_if_{ESP_GATT_IF_NONE};
  uint8_t gatts_next_service_{0};
  uint8_t gatts_next_char_{0};
  uint16_t gatts_handles_[5][MAX_ATTRS]{};
  int toilet_conn_id_{-1};
  bool toilet_encrypted_{false};
  bool toilet_enc_requested_{false};
  bool toilet_enc_ok_{false};    // encrypted with the stored key at least once since the start
  bool reload_keys_due_{false};  // Bluetooth stack lost the stored key: restart to load it again
  bool toilet_sub_buttons_{false};
  bool toilet_sub_2_{false};
  uint32_t pair_toilet_until_{0};
  uint32_t pair_toilet_next_{0};
  bool pair_wait_aa_{false};
  bool listener_registered_{false};
  bool toilet_read_seen_{false};
  int toilet_adv_mode_{-1};
  std::string toilet_adv_desc_;

  // app via the ESP
  bool app_linked_{false};
  bool app_pair_joined_{false};  // an app connected while the adding window was open
  uint32_t app_pair_until_{0};
  uint8_t app_bda_[6]{};
  int app_conn_id_{-1};
};

}  // namespace esphome::geberit_aquaclean

#endif
