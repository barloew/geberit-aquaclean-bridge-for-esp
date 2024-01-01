#pragma once

#ifdef USE_ESP32

#include <array>
#include <cstdint>

#include "esphome/components/button/button.h"
#include "esphome/components/datetime/time_entity.h"
#include "esphome/components/event/event.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"

namespace esphome::geberit_aquaclean {

class AquaCleanHub;

static const uint8_t LANGUAGE_COUNT = 29;  // order of LANGUAGES in __init__.py

/// Kind of setting = which request pair the toilet expects.
enum SettingKind : uint8_t {
  KIND_SESSION = 0,   // read 07 <id>,    write 08 <id> <LE16>
  KIND_DEFAULT = 1,   // read 53 00 <id>, write 54 00 <id> <LE16>
  KIND_COMMON = 2,    // read 51 <id>,    write 52 <id> <LE16> (+ 0b, the layer the light really uses)
  KIND_SCHEDULE = 3,  // read 57 <id>,    write 58 <id> <LE32>
  KIND_LOCAL = 6,     // kept on the ESP: settings of the user profiles that only exist in Home Assistant
};
static const uint8_t KIND_COUNT = 4;
static const uint16_t SCHEDULE_OFF = 0xFFFF;

// Common setting ids
static const uint8_t COMMON_WATER_HARDNESS = 0x00;
static const uint8_t COMMON_LIGHT_BRIGHTNESS = 0x01;
static const uint8_t COMMON_LIGHT_COLOR = 0x02;
static const uint8_t COMMON_LIGHT_ACTIVATION = 0x03;

/// Shared part of all setting entities (number, select, switch, time).
class SettingEntity {
 public:
  void set_max_value(uint16_t max_value) { this->max_value_ = max_value; }
  void configure_setting(AquaCleanHub *hub, uint8_t kind, uint8_t id, uint16_t max_value, uint8_t offset,
                         uint8_t profile = 0) {
    this->hub_ = hub;
    this->kind_ = static_cast<SettingKind>(kind);
    this->id_ = id;
    this->max_value_ = max_value;
    this->offset_ = offset;
    this->profile_ = profile;
  }
  /// User profile of a default setting (53/54 <profile> <id>), 0 = the default settings themselves
  uint8_t profile() const { return this->profile_; }
  /// Hidden by the feature discovery: not read any more
  bool hidden{false};
  SettingKind kind() const { return this->kind_; }
  uint8_t setting_id() const { return this->id_; }
  uint16_t max_value() const { return this->max_value_; }
  uint8_t offset() const { return this->offset_; }
  /// Raw value last read from or written to the toilet, -1 = unknown
  int32_t value() const { return this->value_; }
  virtual const char *setting_name() = 0;

  /// New raw value; published to Home Assistant only when it changed
  void update_value(uint16_t v);
  /// Undo a refused change in Home Assistant
  void republish() {
    if (this->value_ >= 0) this->publish_value_(static_cast<uint16_t>(this->value_));
  }
  void forget() { this->value_ = -1; }
  uint8_t bad_reads{0};

 protected:
  virtual void publish_value_(uint16_t v) = 0;
  void request_write_(uint16_t v);

  AquaCleanHub *hub_{nullptr};
  SettingKind kind_{KIND_COMMON};
  uint8_t id_{0};
  uint16_t max_value_{0};
  uint8_t offset_{0};
  uint8_t profile_{0};
  int32_t value_{-1};
};

class SettingNumber : public number::Number, public SettingEntity {
 public:
  const char *setting_name() override { return this->get_name().c_str(); }

 protected:
  void control(float value) override;
  void publish_value_(uint16_t v) override { this->publish_state(v + this->offset_); }
};

/// Select whose option texts follow the runtime language
class SettingSelect : public select::Select, public SettingEntity {
 public:
  const char *setting_name() override { return this->get_name().c_str(); }
  void set_translation(uint8_t lang, const char *const *options, size_t count) {
    if (lang < LANGUAGE_COUNT) {
      this->tr_[lang] = options;
      this->tr_count_[lang] = count;
    }
  }
  /// options for models with another variant (see OPTION_VARIANTS)
  void set_translation_alt(uint8_t lang, const char *const *options, size_t count) {
    if (lang < LANGUAGE_COUNT) {
      this->tr_alt_[lang] = options;
      this->tr_alt_count_[lang] = count;
    }
  }
  void use_alt(bool alt) { this->alt_ = alt; }
  bool alt() const { return this->alt_; }
  void apply_language(uint8_t lang);

 protected:
  void control(size_t index) override { this->request_write_(static_cast<uint16_t>(index)); }
  void publish_value_(uint16_t v) override {
    if (this->has_index(v)) this->publish_state(static_cast<size_t>(v));
  }
  std::array<const char *const *, LANGUAGE_COUNT> tr_{};
  std::array<size_t, LANGUAGE_COUNT> tr_count_{};
  std::array<const char *const *, LANGUAGE_COUNT> tr_alt_{};
  std::array<size_t, LANGUAGE_COUNT> tr_alt_count_{};
  bool alt_{false};
};

class SettingSwitch : public switch_::Switch, public SettingEntity {
 public:
  const char *setting_name() override { return this->get_name().c_str(); }

 protected:
  void write_state(bool state) override { this->request_write_(state ? 1 : 0); }
  void publish_value_(uint16_t v) override { this->publish_state(v != 0); }
};

class SettingTime : public datetime::TimeEntity, public SettingEntity {
 public:
  const char *setting_name() override { return this->get_name().c_str(); }

 protected:
  void control(const datetime::TimeCall &call) override;
  void publish_value_(uint16_t v) override;
};

// HubButton / HubSwitch actions (same numbers as in __init__.py)
enum ButtonAction : uint8_t {
  BTN_SHOWER = 0,
  BTN_LADY_SHOWER = 1,
  BTN_PAIR_AS_REMOTE = 2,
  BTN_REFRESH = 3,
  BTN_UNPAIR_REMOTE = 4,
  BTN_SCAN = 5,
  BTN_READ_INFO = 6,
  BTN_REDISCOVER = 7,
  BTN_SEARCH_TOILET = 8,
  BTN_ADD_PROFILE = 9,
  BTN_SEARCH_DEVICES = 10,
  BTN_FILTER_RESET = 11,
  BTN_COMMAND = 12,  // command id in command_
  BTN_READ_STATS = 13,
  BTN_HOMEKIT_QR = 14,
  BTN_ACTIVATE_PROFILE = 16,  // + profile 1-6
  BTN_STORE_PROFILE = 32,     // + profile 1-6
  BTN_DELETE_PROFILE = 48,    // + profile 4-6
  BTN_IRK_CAPTURE = 64,       // + profile 1-6
  BTN_IRK_SHOW = 80,          // + profile 1-6
};
enum SwitchAction : uint8_t {
  SW_FORWARD = 0,
  SW_PAIRING = 1,
  SW_ADVERTISE = 2,
  SW_DISCOVERY = 3,
  SW_IRK_ANDROID = 4,
  SW_MAINTENANCE = 5,
  SW_HOMEKIT = 6,
};

class HubButton : public button::Button {
 public:
  void set_action(AquaCleanHub *hub, uint8_t action) {
    this->hub_ = hub;
    this->action_ = action;
  }
  /// Button that sends a command (proc 09) with this id
  void set_command(AquaCleanHub *hub, uint8_t command) {
    this->hub_ = hub;
    this->action_ = BTN_COMMAND;
    this->command_ = command;
  }

 protected:
  void press_action() override;
  AquaCleanHub *hub_{nullptr};
  uint8_t action_{0};
  uint8_t command_{0};
};

class HubSwitch : public switch_::Switch {
 public:
  void set_action(AquaCleanHub *hub, uint8_t action) {
    this->hub_ = hub;
    this->action_ = action;
  }
  uint8_t action() const { return this->action_; }

 protected:
  void write_state(bool state) override;
  AquaCleanHub *hub_{nullptr};
  uint8_t action_{0};
};

/// Name (kind 0) or linked Home Assistant device tracker (kind 1) of the user of a profile
class UserText : public text::Text {
 public:
  void set_user(AquaCleanHub *hub, uint8_t user, uint8_t kind) {
    this->hub_ = hub;
    this->user_ = user;
    this->kind_ = kind;
  }

 protected:
  void control(const std::string &value) override;
  AquaCleanHub *hub_{nullptr};
  uint8_t user_{0};
  uint8_t kind_{0};
};

/// Bluetooth device (kind 0) or welcome light color (kind 1) of the user of a profile
class UserSelect : public select::Select {
 public:
  void set_user(AquaCleanHub *hub, uint8_t user, uint8_t kind) {
    this->hub_ = hub;
    this->user_ = user;
    this->kind_ = kind;
  }
  void set_translation(uint8_t lang, const char *const *options) {
    if (lang < LANGUAGE_COUNT) this->tr_[lang] = options;
  }
  void apply_language(uint8_t lang) {
    if (lang < LANGUAGE_COUNT && this->tr_[lang] != nullptr) this->traits.set_options_static(this->tr_[lang], 8);
  }

 protected:
  void control(size_t index) override;
  AquaCleanHub *hub_{nullptr};
  uint8_t user_{0};
  uint8_t kind_{0};
  std::array<const char *const *, LANGUAGE_COUNT> tr_{};
};

/// Signal strength from which a user's device counts as "at the toilet" (about 1 m)
class ThresholdNumber : public number::Number {
 public:
  void set_hub(AquaCleanHub *hub) { this->hub_ = hub; }

 protected:
  void control(float value) override;
  AquaCleanHub *hub_{nullptr};
};

/// Button presses of the remote control
class RemoteEvent : public event::Event {};

/**
 * Orientation light as a Home Assistant light: on/off (status 9, toggled with command 0x14), color (7
 * fixed colors, common setting 02) and brightness (5 levels, common setting 01). Changes made on the
 * toilet are shown; a requested RGB color is snapped to the nearest toilet color.
 */
class OrientationLight : public light::LightOutput {
 public:
  void set_hub(AquaCleanHub *hub) { this->hub_ = hub; }
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { this->state_ = state; }
  void write_state(light::LightState *state) override;
  /// Toilet state changed (lit, color, brightness): show it without sending anything back
  void sync_from_toilet();

 protected:
  AquaCleanHub *hub_{nullptr};
  light::LightState *state_{nullptr};
  uint32_t last_toggle_{0};
};

}  // namespace esphome::geberit_aquaclean

#endif
