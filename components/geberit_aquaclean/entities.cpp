#include "log_text.h"
#include "entities.h"
#include "hub.h"

#ifdef USE_ESP32

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "esphome/core/log.h"

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean.entity";

// ---------------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------------

void SettingEntity::update_value(uint16_t v) {
  if (this->value_ == v) return;
  this->value_ = v;
  this->publish_value_(v);
  if (this->hub_ != nullptr) this->hub_->on_setting_changed(this);
}

void SettingEntity::request_write_(uint16_t v) {
  if (this->hub_ != nullptr) this->hub_->write_setting(this, v);
}

void SettingNumber::control(float value) {
  const int raw = static_cast<int>(std::lround(value)) - this->offset_;
  if (raw < 0) {
    this->republish();
    return;
  }
  this->request_write_(static_cast<uint16_t>(raw));
}

void RawRequestText::control(const std::string &value) {
  if (this->hub_ == nullptr) return;
  // Parse "<context> <procedure> <args>" as hex, e.g. "01 53 0103"; separators space/comma/colon. The first
  // two tokens are context and procedure; any further tokens are the args, concatenated ("01 53 01 03" works).
  std::vector<std::string> tok;
  std::string cur;
  for (const char c : value) {
    if (c == ' ' || c == ',' || c == ':' || c == '\t') {
      if (!cur.empty()) {
        tok.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) tok.push_back(cur);
  this->publish_state("");  // clear the field; the answer appears in "Last response"
  if (tok.size() < 2) return;
  const uint8_t context = static_cast<uint8_t>(strtol(tok[0].c_str(), nullptr, 16));
  const uint8_t procedure = static_cast<uint8_t>(strtol(tok[1].c_str(), nullptr, 16));
  std::string args;
  for (size_t i = 2; i < tok.size(); i++) args += tok[i];
  this->hub_->send_raw(context, procedure, args);
}

void SettingSelect::apply_language(uint8_t lang) {
  if (lang >= LANGUAGE_COUNT || this->tr_[lang] == nullptr) return;
  if (this->alt_ && this->tr_alt_[lang] != nullptr)
    this->traits.set_options_static(this->tr_alt_[lang], this->tr_alt_count_[lang]);
  else
    this->traits.set_options_static(this->tr_[lang], this->tr_count_[lang]);
  this->republish();
}

void SettingTime::control(const datetime::TimeCall &call) {
  const bool known = this->value_ >= 0 && this->value_ != SCHEDULE_OFF && this->value_ < 1440;
  const uint8_t h = call.get_hour().value_or(known ? this->value_ / 60 : 0);
  const uint8_t m = call.get_minute().value_or(known ? this->value_ % 60 : 0);
  this->request_write_(h * 60 + m);
}

void SettingTime::publish_value_(uint16_t v) {
  if (v == SCHEDULE_OFF || v >= 1440) return;  // window off: no time to show
  this->hour_ = v / 60;
  this->minute_ = v % 60;
  this->second_ = 0;
  this->publish_state();
}

// ---------------------------------------------------------------------------------------------------
// Buttons, switches, language
// ---------------------------------------------------------------------------------------------------

void HubButton::press_action() {
  if (this->hub_ == nullptr) return;
  if (this->action_ == BTN_COMMAND)
    this->hub_->send_command(this->command_);
  else
    this->hub_->button_action(this->action_);
}

void HubSwitch::write_state(bool state) {
  if (this->hub_ != nullptr) this->hub_->switch_action(this, this->action_, state);
}

// ---------------------------------------------------------------------------------------------------
// Orientation light
// ---------------------------------------------------------------------------------------------------

struct Rgb {
  float r, g, b;
};
// Toilet colors in the order of common setting 02: blue, turquoise, magenta, orange, yellow, warm white, white
static const Rgb COLORS[7] = {
    {0.0f, 0.0f, 1.0f}, {0.0f, 0.85f, 0.8f},  {1.0f, 0.0f, 1.0f}, {1.0f, 0.5f, 0.0f},
    {1.0f, 1.0f, 0.0f}, {1.0f, 0.72f, 0.42f}, {1.0f, 1.0f, 1.0f},
};
static const uint8_t BRIGHTNESS_LEVELS = 5;  // common setting 01: 0..4

static uint8_t nearest_color(float r, float g, float b) {
  // normalise so that only the hue/saturation counts, not the brightness
  const float m = std::max(r, std::max(g, b));
  if (m > 0.0f) {
    r /= m;
    g /= m;
    b /= m;
  }
  uint8_t best = 6;
  float best_d = 1e9f;
  for (uint8_t i = 0; i < 7; i++) {
    const float d = (r - COLORS[i].r) * (r - COLORS[i].r) + (g - COLORS[i].g) * (g - COLORS[i].g) +
                    (b - COLORS[i].b) * (b - COLORS[i].b);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

light::LightTraits OrientationLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void OrientationLight::sync_from_toilet() {
  if (this->state_ == nullptr || this->hub_ == nullptr) return;
  const int8_t lit = this->hub_->light_lit();
  if (lit < 0) return;
  auto call = this->state_->make_call();
  call.set_state(lit == 1);
  call.set_transition_length(0);
  SettingEntity *color = this->hub_->find_setting(KIND_COMMON, COMMON_LIGHT_COLOR);
  if (color != nullptr && color->value() >= 0 && color->value() < 7) {
    const Rgb &c = COLORS[color->value()];
    call.set_rgb(c.r, c.g, c.b);
  }
  SettingEntity *bright = this->hub_->find_setting(KIND_COMMON, COMMON_LIGHT_BRIGHTNESS);
  if (bright != nullptr && bright->value() >= 0 && bright->value() < BRIGHTNESS_LEVELS)
    call.set_brightness(static_cast<float>(bright->value() + 1) / BRIGHTNESS_LEVELS);
  call.set_save(false);
  call.perform();
}

void OrientationLight::write_state(light::LightState *state) {
  if (this->hub_ == nullptr || !this->hub_->is_ready()) return;
  // Only send what differs from the toilet: values that came from the toilet itself cause no traffic
  const auto &v = state->current_values;
  const bool on = v.is_on();
  const int8_t lit = this->hub_->light_lit();
  if (lit < 0) return;  // toilet state not known yet
  if (on) {
    SettingEntity *color = this->hub_->find_setting(KIND_COMMON, COMMON_LIGHT_COLOR);
    const uint8_t c = nearest_color(v.get_red(), v.get_green(), v.get_blue());
    if (color != nullptr && color->value() >= 0 && color->value() != c) this->hub_->write_setting(color, c);
    SettingEntity *bright = this->hub_->find_setting(KIND_COMMON, COMMON_LIGHT_BRIGHTNESS);
    int level = static_cast<int>(std::lround(v.get_brightness() * BRIGHTNESS_LEVELS)) - 1;
    level = std::max(0, std::min(level, BRIGHTNESS_LEVELS - 1));
    if (bright != nullptr && bright->value() >= 0 && bright->value() != level)
      this->hub_->write_setting(bright, static_cast<uint16_t>(level));
  }
  if (on != (lit == 1)) {
    const uint32_t now = millis();
    if (now - this->last_toggle_ < 1500) return;  // the toilet has not reported the previous toggle yet
    this->last_toggle_ = now;
    ESP_LOGD(TAG, LT("Orientation light %s"), on ? LT("on") : LT("off"));
    this->hub_->send_command(0x14);
  }
}

}  // namespace esphome::geberit_aquaclean

#endif
