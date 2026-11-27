#pragma once

#ifdef USE_ESP32

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <atomic>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#ifdef USE_DEVICES
#include "esphome/core/device.h"
#endif
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"

#include "entities.h"
#include "log_text.h"

namespace esphome::geberit_aquaclean {

namespace espbt = esphome::esp32_ble_tracker;

class RemoteLink;

/// One request to the toilet: <node 01> <context> <procedure> <len> <args...>
struct Request {
  uint8_t context;
  uint8_t procedure;
  std::vector<uint8_t> args;
  bool is_poll{false};
  bool retried{false};              // asked a second time after a timeout
  bool urgent{false};               // welcome light: before everything else in the queue
  SettingEntity *setting{nullptr};  // set when reading a setting
  int16_t scan_index{-1};           // settings scan: position in the scan
  int16_t probe_index{-1};          // feature discovery: index in gated_
};

/// Feature discovery: toilet model from the article number
struct ModelInfo {
  const char *prefix;  // "146.22"
  const char *name;    // "AquaClean Sela"
  int8_t variant;      // index in the variant texts, -1 = none
  uint16_t features;   // bit per feature (FEATURE_COUNT): the functions shown in "Functions"
  uint64_t caps;       // bit per capability (CAPS in __init__.py): which entities the model gets
};

/// An entity that is shown only when the toilet has its feature / setting
struct Gated {
  EntityBase *entity;
  bool yaml_internal;  // internal in YAML: never shown
  uint64_t req;        // capabilities the model must have (all of them), 0 = none
  int8_t probe_kind;   // setting kind to read, PROBE_COUNTER = counter list, -1 = none
  uint8_t probe_id;
  uint8_t probe_profile;   // user profile of a default setting
  SettingEntity *setting;  // setting entity: not read while hidden
  int8_t probe;            // -1 pending, 0 absent, 1 present, 2 no answer
  bool shown{true};        // shown by the feature discovery at boot
};

static constexpr uint8_t FEATURE_COUNT = 12;
static constexpr int8_t PROBE_COUNTER = 4;
static constexpr int8_t PROBE_SLOT = 5;    // Home Assistant profile <probe_id> has been created
static constexpr int8_t PROBE_MODULE = 6;  // bit <probe_id> of status 64 (connected SSM modules: 0 flush trigger)

// Users: profiles 1-3 live in the toilet, 4-6 only on the ESP (created from Home Assistant)
static constexpr uint8_t TOILET_PROFILES = 3;
static constexpr uint8_t MAX_USERS = 6;
static constexpr uint8_t USER_VALUES = 10;   // profile setting ids 00-09 (in UserData, layout kept)
static constexpr uint8_t USER_VALUES_X = 4;  // profile setting ids 0a-0d (separate store since v1.24.0)
static constexpr uint8_t USER_VALUE_IDS = USER_VALUES + USER_VALUES_X;
// Home Assistant profiles (4-6) only: the shower (anal) oscillation and water temperature are the ordinary
// stored ids 01 and 06; the toilet keeps a single slot for each, shared by both showers, so the Lady shower
// variant cannot live on the toilet. These two ids exist only on the bridge (separate store) and are applied
// to the active session just before the Lady shower starts (see apply_lady_split_).
static constexpr uint8_t USER_VALUES_L = 2;
static constexpr uint8_t LOCAL_LADY_OSCILLATION = USER_VALUE_IDS;            // 0x0e, bridge-only
static constexpr uint8_t LOCAL_LADY_WATER_TEMPERATURE = USER_VALUE_IDS + 1;  // 0x0f, bridge-only
static constexpr uint8_t SESSION_OSCILLATION = 0x01;
static constexpr uint8_t SESSION_WATER_TEMPERATURE = 0x06;

struct UserValuesX {
  uint32_t magic;
  int16_t values[USER_VALUES_X];
};

struct UserValuesL {
  uint32_t magic;
  int16_t values[USER_VALUES_L];
};

/// A Bluetooth device a user carries: fixed address (tracker) or iBeacon (Home Assistant Companion app)
struct DeviceId {
  uint8_t type;  // 0 none, 1 address, 2 iBeacon
  uint8_t uuid[16];
  uint16_t major;
  uint16_t minor;
  uint64_t mac;
  char label[40];
  bool same(const DeviceId &o) const;
};

struct UserData {
  uint32_t magic;
  DeviceId dev;
  uint8_t welcome;  // 0 off, 1-7 = toilet color 0-6
  uint8_t created;  // Home Assistant profiles
  char name[24];
  int16_t values[USER_VALUES];  // Home Assistant profiles: setting values, -1 = unknown
  char entity[64];              // Home Assistant device tracker (Private BLE Device, iBeacon), empty = none
};

/// What Home Assistant tells about the linked device tracker: current address (Private BLE Device) or iBeacon
struct HaDevice {
  uint64_t address{0};
  std::string uuid;  // lower case, with dashes
  int major{-1};
  int minor{-1};
};
static constexpr size_t MAX_GATED = 384;
inline bool bit_get(const std::array<uint32_t, MAX_GATED / 32> &b, size_t i) { return (b[i / 32] >> (i % 32)) & 1; }
inline void bit_set(std::array<uint32_t, MAX_GATED / 32> &b, size_t i) { b[i / 32] |= 1u << (i % 32); }

/**
 * Hub: the app-protocol client on the permanent connection to the toilet.
 *
 * Polls the status, reads and writes settings, sets the clock and relays frames of the Geberit Home app
 * (connected to the ESP, see RemoteLink) to the toilet. Its own requests are slipped in right after an
 * app transaction, so Home Assistant keeps working while the app is open.
 */
class AquaCleanHub : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  // --- configuration ---
  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }
  void set_response_timeout(uint32_t ms) { this->response_timeout_ = ms; }
  /// Toilet presumed silent after this many ms without any decoded frame (0 = off). Drives the liveness probe.
  void set_liveness_timeout(uint32_t ms) { this->liveness_timeout_ = ms; }
  void set_session_refresh(uint32_t ms) { this->session_refresh_ = ms; }
  void set_settings_refresh(uint32_t ms) { this->settings_refresh_ = ms; }
  void set_set_clock(bool v) { this->set_clock_ = v; }
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_default_language(uint8_t lang) { this->language_ = lang; }
  void set_link(RemoteLink *link) { this->link_ = link; }
  void set_light(OrientationLight *light) { this->light_ = light; }
  /// Log texts per language (sorted by the English text; English has none)
  void set_log_table(uint8_t lang, const LogText *texts, uint16_t count) {
    if (lang < LANGUAGE_COUNT) {
      this->log_tables_[lang] = texts;
      this->log_counts_[lang] = count;
    }
  }
  void add_status_flag(uint8_t index, binary_sensor::BinarySensor *s) { this->status_flags_[index] = s; }
  /// Status id shown as a number (e.g. descaling minutes left, last error)
  void add_status_value(uint16_t index, sensor::Sensor *s) { this->status_values_[index] = s; }
  /// Status id shown as text from a table per language (index 0x100 = registration level, proc 55)
  void add_status_enum(uint16_t index, uint16_t key, text_sensor::TextSensor *s) {
    this->status_enums_[key].sensor = s;
    this->status_enums_[key].idx = index;
  }
  void set_status_enum_texts(uint16_t key, uint8_t lang, const char *const *texts, uint8_t count) {
    if (lang < LANGUAGE_COUNT) {
      this->status_enums_[key].texts[lang] = texts;
      this->status_enums_[key].counts[lang] = count;
    }
  }
  /// This status enum carries a raw fault code: render module-aware label + hex instead of indexing.
  void set_status_enum_error_map(uint16_t key) { this->status_enums_[key].error_map = true; }
  /// Counter list (59) id shown as a sensor: kind 0 number, 1 UTC timestamp, 2 signed number
  void add_counter_value(uint8_t id, uint8_t kind, sensor::Sensor *s) { this->counter_values_[id] = {s, kind}; }
  void register_setting(SettingEntity *s) { this->settings_.push_back(s); }
  void register_switch(HubSwitch *s) { this->switches_.push_back(s); }
  void register_select(SettingSelect *s) { this->selects_.push_back(s); }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_app_connected_sensor(binary_sensor::BinarySensor *s) { this->app_connected_sensor_ = s; }
  void set_bridge_problem_sensor(binary_sensor::BinarySensor *s) { this->bridge_problem_sensor_ = s; }
  void set_reconnects_sensor(sensor::Sensor *s) { this->reconnects_sensor_ = s; }
  void set_timeouts_sensor(sensor::Sensor *s) { this->timeouts_sensor_ = s; }
  void set_free_heap_sensor(sensor::Sensor *s) { this->free_heap_sensor_ = s; }
  void set_min_free_heap_sensor(sensor::Sensor *s) { this->min_free_heap_sensor_ = s; }
  void set_largest_free_block_sensor(sensor::Sensor *s) { this->largest_free_block_sensor_ = s; }
  void set_connection_uptime_sensor(text_sensor::TextSensor *s) { this->uptime_sensor_ = s; }
  void set_raw_status_sensor(text_sensor::TextSensor *s) { this->raw_status_sensor_ = s; }
  void set_last_response_sensor(text_sensor::TextSensor *s) { this->last_response_sensor_ = s; }
  void set_toilet_advertisement_sensor(text_sensor::TextSensor *s) { this->advertisement_sensor_ = s; }
  void set_procedures_seen_sensor(text_sensor::TextSensor *s) { this->procedures_sensor_ = s; }
  void set_settings_scan_sensor(text_sensor::TextSensor *s) { this->scan_sensor_ = s; }
  void set_article_number_sensor(text_sensor::TextSensor *s) { this->info_sensors_[0] = s; }
  void set_serial_number_sensor(text_sensor::TextSensor *s) { this->info_sensors_[1] = s; }
  void set_production_date_sensor(text_sensor::TextSensor *s) { this->info_sensors_[2] = s; }
  void set_device_type_sensor(text_sensor::TextSensor *s) { this->info_sensors_[3] = s; }
  void set_commissioning_date_sensor(text_sensor::TextSensor *s) { this->commissioning_sensor_ = s; }
  void set_device_data_sensor(text_sensor::TextSensor *s) { this->device_data_sensor_ = s; }
  void set_counters_sensor(text_sensor::TextSensor *s) { this->counters_sensor_ = s; }
  void set_days_until_descaling_sensor(sensor::Sensor *s) { this->descaling_sensor_ = s; }
  void set_shower_count_sensor(sensor::Sensor *s) { this->shower_count_sensor_ = s; }
  void set_model_sensor(text_sensor::TextSensor *s) { this->model_sensor_ = s; }
  void set_firmware_sensor(text_sensor::TextSensor *s) { this->firmware_sensor_ = s; }
  void set_toilet_signal_sensor(sensor::Sensor *s) { this->signal_sensor_ = s; }
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;
  void set_last_descaling_sensor(sensor::Sensor *s) { this->last_descaling_sensor_ = s; }
  void set_shower_restricted_in_sensor(sensor::Sensor *s) { this->restricted_sensor_ = s; }
  void set_descaling_count_sensor(sensor::Sensor *s) { this->descaling_count_sensor_ = s; }
  void set_days_until_filter_change_sensor(sensor::Sensor *s) { this->filter_sensor_ = s; }
  void set_functions_sensor(text_sensor::TextSensor *s) { this->functions_sensor_ = s; }

  // --- feature discovery ---
  void add_model(const char *prefix, const char *name, int8_t variant, uint16_t features, uint64_t caps) {
    this->models_.push_back({prefix, name, variant, features, caps});
  }
  /// capabilities of a toilet whose model is not known: what every known model has
  void set_unknown_caps(uint64_t caps) { this->unknown_caps_ = caps; }
  /// options / ranges that follow the model (applied when the model is known)
  void add_select_variant(SettingSelect *s, uint64_t cap) { this->select_variants_.push_back({s, cap}); }
  void add_number_variant(SettingNumber *n, uint64_t cap, uint16_t max_raw) {
    this->number_variants_.push_back({n, cap, max_raw});
  }
  void add_status_enum_variant(uint16_t key, uint64_t cap, uint8_t lang, const char *const *texts, uint8_t count) {
    if (lang < LANGUAGE_COUNT) {
      auto &v = this->status_enums_[key].variants[cap];
      v.texts[lang] = texts;
      v.counts[lang] = count;
    }
  }
  void set_texts(uint8_t lang, const char *const *features, const char *const *variants, const char *not_supported,
                 const char *model_unknown) {
    if (lang < LANGUAGE_COUNT) this->texts_[lang] = {features, variants, not_supported, model_unknown};
  }
  void add_gated(EntityBase *e, uint64_t req, int8_t probe_kind, uint8_t probe_id, uint8_t probe_profile = 0) {
    this->gated_.push_back({e, e->is_internal(), req, probe_kind, probe_id, probe_profile, nullptr, -1, true});
  }
  void add_gated_setting(EntityBase *e, SettingEntity *setting, uint64_t req, int8_t probe_kind, uint8_t probe_id,
                         uint8_t probe_profile) {
    this->gated_.push_back({e, e->is_internal(), req, probe_kind, probe_id, probe_profile, setting, -1, true});
  }
  void set_layout(uint32_t layout) { this->layout_ = layout; }
  /// Physical maintenance (maintenance.cpp): the YAML opt-in. The effective consent (maint_accepted_) is this
  /// OR a runtime acceptance stored in flash during web-installer setup; both are combined in setup().
  void set_maintenance_consent(bool accepted) { this->maint_consent_yaml_ = accepted; }
  bool maintenance_consent_accepted() const { return this->maint_accepted_; }
  bool maintenance_consent_yaml() const { return this->maint_consent_yaml_; }
  /// True once a runtime consent value is stored in flash: set once, only a factory reset changes it.
  bool maintenance_consent_locked() const { return this->maint_consent_locked_; }
  /// Store runtime consent (web-installer setup page). Permanent: refuses when consent is forced in the YAML
  /// or already stored. Returns true only when it stored it now.
  bool accept_maintenance_consent();
  /// Long press of the maintenance key on the remote control: show or hide the physical maintenance device
  void on_maintenance_button();
  void add_maintenance_entity(EntityBase *e) { this->maint_entities_.push_back(e); }
  /// Statistics sensor: kind 0 = counter, 1 = counter in seconds shown in hours, 2 = last timer entry (s),
  /// 3 = average of the last 25 timer entries (s); index = counter / timer number
  void add_stat_sensor(uint8_t kind, uint8_t index, sensor::Sensor *s) {
    this->stat_sensors_.push_back({kind, index, s});
  }
#ifdef USE_DEVICES
  void set_maintenance_device(Device *d) { this->maint_device_ = d; }
#endif
  /// Commands that only the physical maintenance device may send
  static bool is_maintenance_command(uint8_t cmd);
  /// Another toilet was chosen: run the feature discovery again after connecting
  void new_toilet() { this->profile_valid_ = false; }
  /// Read model and settings again and show / hide entities accordingly (restarts when that changes)
  void rediscover();

  // --- users (users.cpp) ---
  void set_user_labels(uint8_t lang, const char *const *labels, const char *nobody) {
    if (lang < LANGUAGE_COUNT) {
      this->user_labels_[lang] = labels;
      this->nobody_[lang] = nobody;
    }
  }
  void register_user_text(uint8_t user, uint8_t kind, UserText *t) {
    if (user >= 1 && user <= MAX_USERS) (kind == 0 ? this->name_texts_ : this->entity_texts_)[user - 1] = t;
  }
  /// Link the user of a profile to a Home Assistant device tracker (empty = unlink); restarts the ESP
  void set_user_entity(int user, const std::string &entity_id);
  void register_user_select(uint8_t user, uint8_t kind, UserSelect *s) {
    if (user >= 1 && user <= MAX_USERS) (kind == 0 ? this->device_selects_ : this->welcome_selects_)[user - 1] = s;
  }
  void set_user_flag_sensor(uint8_t user, uint8_t kind, binary_sensor::BinarySensor *s) {
    if (user >= 1 && user <= MAX_USERS) (kind == 0 ? this->recognized_sensors_ : this->active_sensors_)[user - 1] = s;
  }
  void set_recognized_user_sensor(text_sensor::TextSensor *s) { this->recognized_user_sensor_ = s; }
  void set_user_irk_sensor(uint8_t user, text_sensor::TextSensor *s) {
    if (user >= 1 && user <= MAX_USERS) this->irk_sensors_[user - 1] = s;
  }
  /// IRK capture: the key of the phone of profile `user` (LSB first, as received), and the end of a capture
  void set_user_irk(uint8_t user, const uint8_t *irk_le);
  /// A known IRK as text: 32 hex digits, most significant first (as Show IRK and Home Assistant's Private BLE
  /// Device show it), or base64 (as in the iOS keychain, least significant first); empty removes it
  bool set_user_irk_text(uint8_t user, const std::string &irk);
  void irk_capture_ended(bool got);
  void set_user_rssi_sensor(uint8_t user, sensor::Sensor *s) {
    if (user >= 1 && user <= MAX_USERS) this->rssi_sensors_[user - 1] = s;
  }
  void set_active_user_sensor(text_sensor::TextSensor *s) { this->active_user_sensor_ = s; }
  void set_threshold_number(ThresholdNumber *n) { this->threshold_number_ = n; }
  void set_statestream_topic(const std::string &t) { this->statestream_topic_ = t; }
  void set_log_obfuscate(bool on) { ::esphome::geberit_aquaclean::set_log_obfuscate(on); }
  /// Every advertisement (via RemoteLink): user devices and the device search
  void on_advert(const esp32_ble_tracker::ESPBTDevice &device);
  void set_user_name(uint8_t user, const std::string &name);
  void select_user_device(uint8_t user, size_t index);
  void set_user_welcome(uint8_t user, size_t index);
  void set_threshold(float value);
  void add_user_profile();
  void delete_user_profile(uint8_t user);
  void search_devices();
  /// Status LED of the board: welcome blinking of the recognized user (true = override, color in r/g/b)
  bool welcome_led(float &r, float &g, float &b);

  // --- control (also usable from YAML lambdas) ---
  /// 09 01 <cmd>: 0x00 shower, 0x01 Lady shower, 0x14 orientation light (toggle)
  void send_command(uint8_t cmd);
  /// Names of shower, Lady shower and dryer in the log (these commands only start while someone is seated)
  void set_seated_name(uint8_t i, const char *name) {
    if (i < 3) this->seated_names_[i] = name;
  }
  /// Free request; args as hex text ("0b0302"). The answer is logged and shown in "Last response".
  void send_raw(uint8_t context, uint8_t procedure, const std::string &args_hex);
  /// Read all settings again
  void refresh_settings();
  /// Write a setting through its entity; session settings may also be written before sitting down
  void write_setting(SettingEntity *s, uint16_t value);
  /// A setting changed its known value (read, written or seen in app traffic)
  void on_setting_changed(SettingEntity *s);
  /// Remote control button without passthrough: translated into an app command
  void remote_button(uint8_t code);
  /// Actions of the HubButton / HubSwitch entities
  void button_action(uint8_t action);
  void switch_action(HubSwitch *sw, uint8_t action, bool state);
  /// Settings scan for other models: reads ids 0x00-0x1f of every kind and reports which ones answer
  void start_settings_scan();
  /// Device information (82: article/serial/date/description; 81, 05, 0e, 13 raw) and the counter list (59)
  void read_device_info();
  /// Copy user profile 1-4 into the session settings (what a profile key of the remote control does)
  void activate_profile(uint8_t profile);
  /// Store the session settings in user profile 1-4
  void store_profile(uint8_t profile);
  /// Just before a shower starts for the recognized user: if a Home Assistant profile (4-6) is active, write
  /// its oscillation + water temperature for this shower (Lady or not) into the session. The toilet keeps only
  /// one slot for each, shared by both showers; this is where the per-shower split kept on the bridge is applied.
  void apply_lady_split_(bool lady);

  // --- app via the ESP (RemoteLink) ---
  void app_frame_in(uint8_t write_index, const uint8_t *data, uint16_t len);
  void set_app_connected(bool v);
  /// The Geberit Home app connected from this address: recognize its user (users.cpp)
  void on_app_device(uint64_t address);
  /// A profile key (1-3) of the remote control was pressed: recognize the user of that profile
  void on_remote_profile(uint8_t profile);
  void set_app_sink(std::function<void(uint8_t, const uint8_t *, uint16_t)> sink) { this->app_sink_ = std::move(sink); }

  // --- language (option texts at runtime) ---
  uint8_t language() const { return this->language_; }
  std::array<const LogText *, LANGUAGE_COUNT> log_tables_{};
  std::array<uint16_t, LANGUAGE_COUNT> log_counts_{};

  // --- discovery ---
  bool discovery() const { return this->discovery_; }
  void publish_advertisement(const std::string &text);

  bool is_ready() const { return this->node_state == espbt::ClientState::ESTABLISHED; }
  bool app_connected() const { return this->app_connected_; }
  /// Status 0: somebody is seated
  bool is_seated() const { return this->status_[0] == 1; }
  /// Status 9: orientation light is lit (-1 = unknown)
  int8_t light_lit() const { return this->status_[9] < 0 ? -1 : (this->status_[9] != 0 ? 1 : 0); }
  SettingEntity *find_setting(SettingKind kind, uint8_t id, uint8_t profile = 0);
  RemoteLink *link() const { return this->link_; }

#ifdef USE_AQUACLEAN_HOMEKIT
  // Apple Home (HomeKit) on the ESP: shower, lady shower, dryer, odor extraction as switches
  void set_homekit(const char *setup_code, bool reset_pairings) {
    this->hk_setup_code_ = setup_code;
    this->hk_reset_ = reset_pairings;
  }
  /// Initial run state of HomeKit. Always-on builds (homekit: enabled: true) pass true here; builds with the
  /// runtime toggle pass false, and the SW_HOMEKIT switch sets it from its restored state.
  void set_homekit_runtime_enabled(bool on) { this->hk_runtime_enabled_ = on; }
  void set_homekit_name(uint8_t i, const char *name) { this->hk_names_[i] = name; }
  /// Route-neutral: the pairing code and /homekit page URL as a text sensor, shown by every controller
  void set_homekit_code_sensor(text_sensor::TextSensor *s) { this->hk_code_sensor_ = s; }
  void set_homekit_entities(binary_sensor::BinarySensor *shower, binary_sensor::BinarySensor *lady,
                            binary_sensor::BinarySensor *dryer, switch_::Switch *odor) {
    this->hk_flags_[0] = shower;
    this->hk_flags_[1] = lady;
    this->hk_flags_[2] = dryer;
    this->hk_odor_ = odor;
  }
  /// Called from the HomeKit task: only stores the request, the main loop carries it out
  void homekit_request(uint8_t i, bool on) { this->hk_request_[i].store(on ? 1 : 0); }
  /// Called from the HomeKit task: the Home app wrote a name for switch i
  void homekit_renamed(uint8_t i) { this->hk_rename_[i].store(true); }
  /// Button: show the setup QR code again (Home Assistant notification, or the URL of the page on the ESP)
  void homekit_show_qr();
#endif

 protected:
#ifdef USE_AQUACLEAN_HOMEKIT
  void homekit_start_();
  void homekit_loop_(uint32_t now);
  void homekit_dump_config_();
  bool homekit_notify_qr_(bool show);
  bool hk_qr_due_{false};
  bool homekit_state_(uint8_t i) const;
  std::string hk_setup_code_;
  bool hk_reset_{false};
  bool hk_reset_due_{false};
  ESPPreferenceObject hk_reset_pref_;
  bool hk_started_{false};
  bool hk_runtime_enabled_{false};  // gates homekit_start_(); set by codegen or the SW_HOMEKIT switch
  const char *hk_names_[4]{};
  text_sensor::TextSensor *hk_code_sensor_{nullptr};
  binary_sensor::BinarySensor *hk_flags_[3]{};
  switch_::Switch *hk_odor_{nullptr};
  void *hk_chars_[4]{};  // hap_char_t* of the On characteristic, nullptr = switch not offered
  std::atomic<int8_t> hk_request_[4]{{-1}, {-1}, {-1}, {-1}};
  int8_t hk_published_[4]{-1, -1, -1, -1};
  uint32_t hk_resync_at_{0};
  void *hk_cn_[4]{};  // hap_char_t* of ConfiguredName
  std::atomic<bool> hk_rename_[4]{{false}, {false}, {false}, {false}};
  uint32_t hk_rename_at_[4]{};
  bool hk_layout_due_{false};
  uint32_t hk_layout_{0};
  ESPPreferenceObject hk_layout_pref_;
#endif
  void reset_session_();
  void start_session_();
  void sync_clock_();
  void maintenance_setup_();
  void maintenance_loop_(uint32_t now);
  void maintenance_switch_(HubSwitch *sw, bool state);
  void maintenance_notice_();
  void maintenance_show_(bool show);
  bool maint_accepted_{false};        // effective consent = YAML opt-in OR stored runtime acceptance
  bool maint_consent_yaml_{false};    // the YAML opt-in (maintenance_disclaimer_accepted)
  bool maint_consent_locked_{false};  // a runtime consent value is stored in flash (set once; factory-reset to change)
  ESPPreferenceObject maint_consent_pref_;
  bool maint_active_{false};  // shown now
  uint32_t maint_since_{0};
  std::vector<EntityBase *> maint_entities_;
  HubSwitch *maint_switch_{nullptr};
#ifdef USE_DEVICES
  Device *maint_device_{nullptr};
#endif
  const char *restart_reason_{nullptr};  // log text for a planned restart (default: feature set changed)
  void apply_language_();
  void enqueue_(Request req);
  void enqueue_(uint8_t context, uint8_t procedure, std::vector<uint8_t> args, bool is_poll = false);
  void enqueue_urgent_(uint8_t context, uint8_t procedure, std::vector<uint8_t> args);
  void set_scan_boost_(bool on);
  uint32_t scan_boost_until_{0};
  uint32_t boost_started_{0};
  uint32_t boost_pause_until_{0};
  bool scan_boosted_{false};
  void enqueue_read_(SettingEntity *s);
  void enqueue_reads_(bool session_kind);
  void send_request_(const Request &req);
  void write_frame_(uint16_t handle, const uint8_t *data);
  void on_frame_(const uint8_t *data, uint16_t len);
  void on_message_(const std::vector<uint8_t> &msg);
  void handle_status_(const uint8_t *args, uint8_t len);
  void handle_setting_read_(SettingEntity *s, const uint8_t *args, uint8_t len);
  bool snoop_app_response_(uint8_t procedure, const uint8_t *args, uint8_t len);
  void snoop_app_write_(uint8_t procedure, const uint8_t *args, uint8_t len);
  void handle_timeout_();
  void scan_result_(int16_t index, const uint8_t *args, int len);
  void publish_procedures_();
  void step_session_(uint8_t id, int delta);
  void send_ack_();
  static uint16_t crc16_(const uint8_t *data, size_t len);

  // configuration
  uint32_t poll_interval_{500};
  uint32_t response_timeout_{1000};
  uint32_t session_refresh_{2000};
  uint32_t settings_refresh_{300000};
  bool set_clock_{true};
  time::RealTimeClock *time_{nullptr};
  RemoteLink *link_{nullptr};
  OrientationLight *light_{nullptr};

  // entities
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *app_connected_sensor_{nullptr};
  binary_sensor::BinarySensor *bridge_problem_sensor_{nullptr};
  sensor::Sensor *reconnects_sensor_{nullptr};
  sensor::Sensor *timeouts_sensor_{nullptr};
  text_sensor::TextSensor *uptime_sensor_{nullptr};
  text_sensor::TextSensor *raw_status_sensor_{nullptr};
  text_sensor::TextSensor *last_response_sensor_{nullptr};
  text_sensor::TextSensor *advertisement_sensor_{nullptr};
  text_sensor::TextSensor *procedures_sensor_{nullptr};
  text_sensor::TextSensor *scan_sensor_{nullptr};
  std::array<text_sensor::TextSensor *, 4> info_sensors_{};
  text_sensor::TextSensor *commissioning_sensor_{nullptr};
  text_sensor::TextSensor *device_data_sensor_{nullptr};
  text_sensor::TextSensor *counters_sensor_{nullptr};
  sensor::Sensor *descaling_sensor_{nullptr};
  sensor::Sensor *shower_count_sensor_{nullptr};
  std::map<uint8_t, binary_sensor::BinarySensor *> status_flags_;
  struct StatusEnum {
    text_sensor::TextSensor *sensor{nullptr};
    uint16_t idx{0};  // status id (or 0x100 registration)
    struct Texts {
      const char *const *texts[LANGUAGE_COUNT]{};
      uint8_t counts[LANGUAGE_COUNT]{};
    };
    std::map<uint64_t, Texts> variants;  // other texts for models with that capability
    const char *const *texts[LANGUAGE_COUNT]{};
    uint8_t counts[LANGUAGE_COUNT]{};
    int64_t value{-1};
    // last_error: value is a raw hex fault code, not a table index. Render code-first as
    // "<texts[1]> NNNN (<module>)" where texts[0]=no error, [1]="error code" word,
    // [2..17]=modules by high byte 0x00-0x0F ("" -> brackets omitted).
    bool error_map{false};
  };
  struct CounterValue {
    sensor::Sensor *sensor;
    uint8_t kind;
  };
  std::map<uint16_t, sensor::Sensor *> status_values_;
  std::map<uint16_t, StatusEnum> status_enums_;
  std::map<uint8_t, CounterValue> counter_values_;
  std::map<uint8_t, int64_t> status_extra_;  // status ids beyond the poll list (64-6a, read in discovery)
  uint16_t status_seen_{0};                  // ids 0-11 received at least once: publish the first value
  void publish_status_(uint16_t idx, int64_t value);
  void publish_status_enum_(StatusEnum &e);
  std::vector<SettingEntity *> settings_;
  std::vector<HubSwitch *> switches_;
  bool switches_restored_{false};  // true once the boot-time restore of switch states has finished
  std::vector<SettingSelect *> selects_;

  // language
  uint8_t language_{0};
  const char *seated_names_[3]{"Shower", "Lady shower", "Dryer"};
  uint32_t reboot_at_{0};

  // GATT: only handles are kept, ESPHome releases the GATT objects once the connection is established
  uint16_t write0_handle_{0};
  uint16_t write1_handle_{0};
  std::array<uint16_t, 4> notify_handles_{};
  uint8_t pending_notify_regs_{0};

  // session
  bool session_started_{false};
  uint32_t established_at_{0};
  std::deque<Request> queue_;
  bool waiting_{false};
  Request waiting_req_{};
  uint32_t sent_at_{0};
  uint32_t last_poll_{0};
  uint32_t last_session_read_{0};
  uint32_t last_settings_read_{0};
  bool session_read_due_{false};
  bool settings_read_due_{false};
  uint32_t last_clock_sync_{0};
  bool clock_synced_{false};  // false: set the clock as soon as the time is known
  bool clock_check_pending_{false};
  bool clock_check_requested_{false};
  // The time source has synced: only then is the time zone applied. After a software restart the ESP32 keeps
  // its UTC time but reports it as local time until Home Assistant / SNTP has synced.
  bool time_synced_{false};
  uint32_t time_synced_at_{0};
  uint32_t clock_check_since_{0};
  uint32_t last_uptime_publish_{0};
  bool uptime_published_{false};
  bool banner_shown_{false};
  std::map<uint8_t, std::string> device_raw_;  // last content per device-data procedure (log on change)
  uint8_t consecutive_timeouts_{0};
  uint32_t timeouts_{0};
  uint32_t reconnects_{0};
  bool was_connected_{false};
  // Liveness: any correctly decoded frame from the toilet (response or notification, ours or app-relayed)
  // refreshes last_healthy_rx_. If it stays stale past liveness_timeout_ while the app is connected -- when
  // the hub otherwise stays silent and would not notice a wedged toilet -- a single status poll is injected
  // so the normal timeout path can detect a dead toilet. 0 disables the probe.
  uint32_t liveness_timeout_{90000};
  uint32_t last_healthy_rx_{0};
  bool last_healthy_rx_valid_{false};
  uint32_t last_liveness_probe_{0};

  // --- recovery ladder (step 3) ---
  // When the toilet link stays unhealthy (not connected, or connected but silent past liveness_timeout_) or
  // keeps flapping, escalate: at most MAX_RECOVERY_REBOOTS gated reboots per bad episode -- never while the app
  // is connected -- then a sticky "needs power-cycle" state (no reboot loop). A persisted counter bounds the
  // reboots across restarts; a sustained healthy period clears the episode. Runs only after the first
  // successful connection this boot, so a missing toilet never triggers a reboot loop.
  void recovery_loop_(uint32_t now);
  void record_reconnect_(uint32_t now);
  uint8_t recent_reconnects_(uint32_t now, uint32_t window) const;
  bool recovery_enabled_{true};
  bool recovery_reboot_enabled_{true};
  bool ever_connected_{false};
  uint32_t unhealthy_since_{0};
  uint32_t healthy_since_{0};
  bool needs_power_cycle_{false};
  uint8_t recovery_reboots_{0};
  ESPPreferenceObject recovery_pref_;
  std::array<uint32_t, 8> reconnect_times_{};
  uint8_t reconnect_idx_{0};

  // --- resource watchdog + unified health (step 4) ---
  // Samples free heap, lowest-ever free heap and the largest free block (fragmentation) every ~30 s, and folds
  // them together with the link/recovery state into one coarse health verdict, logged on change. Step 4a is
  // logic + logging only; the bridge_health entity and heap diagnostics are wired as entities in step 4b.
  enum class Health : uint8_t { OK, DEGRADED, RECOVERING, NEEDS_POWER_CYCLE };
  void resource_loop_(uint32_t now);
  Health compute_health_(uint32_t now) const;
  void publish_health_(uint32_t now);
  static const char *health_name_(Health h);
  uint32_t last_resource_check_{0};
  uint32_t free_heap_{0};
  uint32_t min_free_heap_{0};
  uint32_t largest_block_{0};
  uint32_t heap_floor_{20000};  // free heap below this -> degraded
  uint32_t block_floor_{8000};  // largest free block below this -> degraded (fragmentation)
  Health health_{Health::OK};
  Health health_logged_{Health::NEEDS_POWER_CYCLE};     // sentinel so the first verdict is always published
  static constexpr uint16_t BRIDGE_HEALTH_IDX = 0x101;  // synthetic status-enum index (like registration 0x100)
  sensor::Sensor *free_heap_sensor_{nullptr};
  sensor::Sensor *min_free_heap_sensor_{nullptr};
  sensor::Sensor *largest_free_block_sensor_{nullptr};
  std::array<uint8_t, KIND_COUNT> kind_timeouts_{};
  std::array<bool, KIND_COUNT> kind_disabled_{};

  // app via the ESP
  static constexpr uint32_t APP_HOLD_MS = 400;
  bool app_owned_() const { return static_cast<int32_t>(millis() - this->app_owner_until_) < 0; }
  std::deque<std::pair<uint8_t, std::array<uint8_t, 20>>> app_tx_;
  uint32_t app_owner_until_{0};
  bool app_connected_{false};
  std::function<void(uint8_t, const uint8_t *, uint16_t)> app_sink_;
  // last single-frame request of the app (to recognize its answers): procedure + args
  uint8_t app_req_proc_{0};
  std::vector<uint8_t> app_req_args_;

  // reception: up to 4 frames per message
  uint8_t rx_expected_{0};
  uint8_t rx_mask_{0};
  std::array<std::array<uint8_t, 19>, 4> rx_frames_{};
  // large transfers (type 1 first frame with the frame count, type 2 frames with their sequence number)
  static constexpr uint8_t LARGE_MAX = 16;
  std::array<std::array<uint8_t, 18>, LARGE_MAX> lrx_frames_{};
  uint8_t lrx_total_{0};
  uint16_t lrx_mask_{0};
  void on_large_frame_(const uint8_t *data, uint8_t type);
  void send_large_ack_();
  void handle_device_message_(uint8_t procedure, const uint8_t *args, uint8_t len);

  // last known status per index (-1 = unknown)
  std::array<int64_t, 12> status_{};

  // feature discovery
  struct Texts {
    const char *const *features{nullptr};
    const char *const *variants{nullptr};
    const char *not_supported{""};
    const char *model_unknown{""};
  };
  static constexpr uint32_t PROFILE_MAGIC = 0x41514303;  // "AQC" v3 (256 entities)
  struct Profile {
    uint32_t magic;
    uint32_t layout;
    std::array<uint32_t, MAX_GATED / 32> visible;  // bit per gated entity
    int8_t model;                                  // index in models_, -1 = unknown
  };
  void apply_profile_();
  bool slot_ok_(const Gated &g) const {
    return g.probe_kind != PROBE_SLOT ||
           (g.probe_id >= 1 && g.probe_id <= MAX_USERS && this->users_[g.probe_id - 1].created);
  }
  // users
  void users_setup_();
  void users_loop_(uint32_t now);
  void users_on_seated_(bool seated);
  void save_user_(uint8_t u);
  void recognized_(uint8_t u, uint32_t now, uint8_t by = 0);  // by: 0 signal, 1 app, 2 profile key
  int8_t app_user_{-1};
  void welcome_(uint8_t u);
  bool welcome_ready_(uint8_t u) const;
  int8_t pending_welcome_{-1};
  uint32_t pending_since_{0};
  std::string user_label_(int u) const;
  void publish_users_();
  void invalidate_profile_and_restart_();
  struct UserState {
    int rssi{-128};
    uint32_t last_near{0};
    bool recognized{false};
    uint32_t last_welcome{0};
    bool welcomed{false};
    uint32_t last_seen{0};
    uint32_t hold_until{0};
    uint32_t last_rssi_pub{0};
    int published_rssi{0};
  };
  struct WelcomeStep {
    uint32_t at;
    uint8_t op;  // 1 = active common setting, 2 = light toggle
    uint8_t id;
    uint16_t value;
  };
  std::array<UserData, MAX_USERS> users_{};
  std::array<UserState, MAX_USERS> ustate_{};
  std::array<ESPPreferenceObject, MAX_USERS> user_prefs_{};
  std::array<ESPPreferenceObject, MAX_USERS> user_x_prefs_{};
  std::array<UserValuesX, MAX_USERS> users_x_{};
  std::array<ESPPreferenceObject, MAX_USERS> user_l_prefs_{};
  std::array<UserValuesL, MAX_USERS> users_l_{};
  /// stored value of setting <id> in Home Assistant profile u (0-based), nullptr when the id is not stored
  int16_t *user_value_(uint8_t u, uint8_t id) {
    if (u >= MAX_USERS) return nullptr;
    if (id < USER_VALUES) return &this->users_[u].values[id];
    if (id < USER_VALUE_IDS) return &this->users_x_[u].values[id - USER_VALUES];
    if (id < USER_VALUE_IDS + USER_VALUES_L) return &this->users_l_[u].values[id - USER_VALUE_IDS];
    return nullptr;
  }
  std::array<UserText *, MAX_USERS> name_texts_{};
  std::array<UserText *, MAX_USERS> entity_texts_{};
  std::array<sensor::Sensor *, MAX_USERS> rssi_sensors_{};
  // IRK per profile (key for AES, MSB first) and a small cache of resolved random addresses
  struct IrkStore {
    uint32_t magic;
    uint8_t key[16];
  };
  std::array<IrkStore, MAX_USERS> irks_{};
  std::array<ESPPreferenceObject, MAX_USERS> irk_prefs_;
  std::array<text_sensor::TextSensor *, MAX_USERS> irk_sensors_{};
  struct RpaCache {
    uint64_t address;
    int8_t owner;  // user index, -1 = nobody
  };
  std::array<RpaCache, 32> rpa_cache_{};
  uint8_t rpa_next_{0};
  bool irk_android_{false};
  int8_t rpa_owner_(uint64_t address);
  void publish_irk_(uint8_t u);
  void show_irk_(uint8_t u);
  std::array<HaDevice, MAX_USERS> ha_devices_{};
  std::string statestream_topic_;  // MQTT route: base topic of Home Assistant's mqtt_statestream
  bool matches_user_(uint8_t u, const DeviceId &id, const esp32_ble_tracker::ESPBTDevice &device) const;
  std::array<UserSelect *, MAX_USERS> device_selects_{};
  std::array<UserSelect *, MAX_USERS> welcome_selects_{};
  std::array<binary_sensor::BinarySensor *, MAX_USERS> recognized_sensors_{};
  std::array<binary_sensor::BinarySensor *, MAX_USERS> active_sensors_{};
  std::array<const char *const *, LANGUAGE_COUNT> user_labels_{};
  std::array<const char *, LANGUAGE_COUNT> nobody_{};
  text_sensor::TextSensor *recognized_user_sensor_{nullptr};
  text_sensor::TextSensor *active_user_sensor_{nullptr};
  ThresholdNumber *threshold_number_{nullptr};
  ESPPreferenceObject threshold_pref_;
  ESPPreferenceObject found_pref_;
  int8_t threshold_{-60};
  int8_t recognized_user_{-1};
  int8_t active_user_{-1};
  std::vector<DeviceId> found_;
  std::vector<std::string> device_options_;
  std::vector<const char *> device_option_ptrs_;
  uint32_t search_until_{0};
  std::vector<std::pair<DeviceId, int>> search_found_;
  std::deque<WelcomeStep> welcome_steps_;
  uint32_t led_welcome_start_{0};
  float led_r_{0}, led_g_{0}, led_b_{0};
  void enqueue_counters_();
  void start_feature_discovery_();
  void on_article_(const std::string &article);
  void probe_result_(int16_t index, int len);
  void check_feature_discovery_();
  void publish_model_();
  std::vector<ModelInfo> models_;
  uint64_t unknown_caps_{0};
  uint64_t model_caps_{0};  // capabilities of the model in use (0 = not known yet)
  struct SelectVariant {
    SettingSelect *select;
    uint64_t cap;
  };
  struct NumberVariant {
    SettingNumber *number;
    uint64_t cap;
    uint16_t max_raw;
  };
  std::vector<SelectVariant> select_variants_;
  std::vector<NumberVariant> number_variants_;
  void apply_variants_(uint64_t caps);
  void read_statistics_();
  void read_stats_();
  void start_timer_reads_();
  void handle_statistic_(uint8_t procedure, uint8_t arg0, uint8_t arg1, const uint8_t *args, uint8_t len);
  void log_statistic_(uint8_t procedure, uint8_t index, const uint8_t *args, uint8_t len);
  // Sela light mode: the app's "time-controlled" is automatic (2) with time windows set
  SettingSelect *sela_light_select_();
  bool sela_light_write_(SettingEntity *s, uint16_t value);
  void sela_light_publish_();
  std::array<Texts, LANGUAGE_COUNT> texts_{};
  std::vector<Gated> gated_;
  // Statistics
  struct StatSensor {
    uint8_t kind;
    uint8_t index;
    sensor::Sensor *s;
  };
  struct TimerRead {
    uint8_t timer;
    uint8_t page;    // page with the newest entry
    uint8_t idx;     // newest entry in that page
    bool need_prev;  // average: the page before it too
    bool want_avg{false};
    bool scan{false};  // the toilet keeps fewer entries than its counter: newest = last non-zero of the last page
    bool got_prev{false};
    bool got_last{false};
    uint16_t prev[25]{};
    uint16_t last[25]{};
  };
  std::vector<StatSensor> stat_sensors_;
  std::vector<TimerRead> timer_reads_;
  uint8_t raw_proc_{0};  // procedure of the last free request (its statistics answer is always logged)
  uint32_t raw_at_{0};
  uint32_t stat_counter_[26]{};
  uint32_t stat_valid_{0};          // bit per counter
  int16_t stats_last_counter_{-1};  // timers are read after this counter answered
  bool stats_read_due_{false};
  uint32_t stats_due_at_{0};  // after the user left: read again (0 = not planned)
  uint32_t last_stats_read_{0};
  uint32_t layout_{0};
  ESPPreferenceObject profile_pref_;
  Profile profile_{};
  bool profile_valid_{false};
  bool fd_running_{false};
  bool fd_article_done_{false};
  int8_t fd_model_{-1};
  bool counter_descaling_seen_{false};
  std::set<uint8_t> counter_ids_seen_;  // ids the counter list (59) answered, for the discovery
  bool restart_pending_{false};
  text_sensor::TextSensor *model_sensor_{nullptr};
  text_sensor::TextSensor *firmware_sensor_{nullptr};
  sensor::Sensor *signal_sensor_{nullptr};
  uint32_t last_rssi_read_{0};
  uint8_t info_retries_{0};
  sensor::Sensor *last_descaling_sensor_{nullptr};
  sensor::Sensor *restricted_sensor_{nullptr};
  sensor::Sensor *descaling_count_sensor_{nullptr};
  sensor::Sensor *filter_sensor_{nullptr};
  text_sensor::TextSensor *functions_sensor_{nullptr};

  // discovery
  bool discovery_{false};
  std::map<uint8_t, uint32_t> app_procedures_;
  bool procedures_dirty_{false};
  uint32_t last_procedures_publish_{0};
  static constexpr uint8_t SCAN_KINDS = 5;  // the 4 setting kinds + active values (0a)
  std::array<std::string, SCAN_KINDS> scan_found_;
  std::map<uint8_t, std::string> device_data_;
  bool device_info_read_{false};
  int16_t scan_total_{0};
  int16_t scan_done_{0};
};

// Set in the hub constructor. The web-installer maintenance setup page (maint_setup.h) reaches the hub through
// the free functions in maintenance.cpp, which use this.
extern AquaCleanHub *global_aquaclean_hub;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome::geberit_aquaclean

#endif
