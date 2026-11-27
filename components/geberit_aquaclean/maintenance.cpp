// Physical maintenance: descaling, arm cleaning and calibration, lid and arm adjustment, draining and resetting
// maintenance counters. These need someone at the toilet, so their device is hidden:
//  - the YAML must accept the disclaimer (maintenance_disclaimer_accepted: true);
//  - a long press of the maintenance key on the physical remote control shows it, with a warning notification in Home
//  Assistant;
//    another long press, the switch "Physical maintenance", 60 minutes or a restart of the ESP hide it again.
// Home Assistant only learns about shown or hidden entities when it connects, so the ESP closes the API
// connections and Home Assistant connects again at once. Without the API (MQTT route) the ESP restarts instead;
// the flag for "show in the next boot" lives in RTC memory and survives that restart only.
#ifdef USE_ESP32

#include <esp_attr.h>

#include "log_text.h"
#include "hub.h"
#ifdef USE_API
#include "esphome/components/api/api_connection.h"
#include "esphome/components/api/api_server.h"
#endif
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean";

static const uint32_t MAINT_DURATION = 60UL * 60 * 1000;  // then hidden again
#ifndef USE_API
static const uint32_t MAINT_MAGIC = 0x4D4E5432;  // "MNT2": show the maintenance device in the next boot
RTC_NOINIT_ATTR static uint32_t maint_next_boot;
#endif

// entities of the maintenance device that the feature discovery shows (the others stay hidden)
static std::vector<bool> maint_visible;
static uint32_t maint_reconnect_at = 0;

bool AquaCleanHub::is_maintenance_command(uint8_t cmd) {
  switch (cmd) {
    case 0x04:  // arm cleaning
    case 0x05:
    case 0x06:  // descaling start / continue / cancel
    case 0x07:
    case 0x08:
    case 0x09:  // unlock the shower
    case 0x0B:  // lid calibration
    case 0x21:  // lid opening angle
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x2F:  // filter replaced
    case 0x34:  // drain pump and pipe
    case 0xA3:  // shower arm position
    case 0xA4:
    case 0xA5:
    case 0xAC:
    case 0xAD:  // dryer arm position
    case 0xAE:
    case 0xAF:
    case 0xB0:
      return true;
    default:
      return false;
  }
}

void AquaCleanHub::maintenance_setup_() {
  maint_visible.clear();
  for (auto *e : this->maint_entities_) maint_visible.push_back(!e->is_internal());
  bool show = false;
#ifndef USE_API
  show = this->maint_accepted_ && maint_next_boot == MAINT_MAGIC;
  maint_next_boot = 0;  // one boot only
#endif
  this->maint_active_ = !show;  // so that maintenance_show_() applies everything
  this->maintenance_show_(show);
  maint_reconnect_at = 0;  // nobody is connected yet
}

void AquaCleanHub::maintenance_show_(bool show) {
  if (show == this->maint_active_) return;
  this->maint_active_ = show;
  this->maint_since_ = millis();
  for (size_t i = 0; i < this->maint_entities_.size(); i++)
    this->maint_entities_[i]->set_internal(!(show && maint_visible[i]));
  if (this->maint_switch_ != nullptr) {
    this->maint_switch_->set_internal(!show);
    this->maint_switch_->publish_state(show);
  }
#ifdef USE_DEVICES
  // A device without entities would stay empty in Home Assistant, so its slot in the device list points to
  // another device while it is hidden. The list must keep its length: the API sends all ESPHOME_DEVICE_COUNT
  // entries, and a missing one goes out as an empty device that Home Assistant names like the ESP itself.
  if (this->maint_device_ != nullptr) {
    auto &devices = const_cast<StaticVector<Device *, ESPHOME_DEVICE_COUNT> &>(App.get_devices());
    static int slot = -1;
    Device *other = nullptr;
    for (size_t i = 0; i < devices.size(); i++) {
      if (devices[i] == this->maint_device_)
        slot = static_cast<int>(i);
      else if (other == nullptr)
        other = devices[i];
    }
    if (slot >= 0 && other != nullptr) devices[slot] = show ? this->maint_device_ : other;
  }
#endif
#ifdef USE_API
  maint_reconnect_at = millis() + 1500;  // after the notification has gone out
#endif
}

void AquaCleanHub::on_maintenance_button() {
  if (!this->maint_accepted_) {
    ESP_LOGW(TAG, LT("Physical maintenance is off. Enable it in an ESPHome YAML build "
                     "(maintenance_disclaimer_accepted: true), or on the web-installer firmware during setup via the "
                     "AquaClean Setup captive portal (open /maintenance). Read the warning in the README first."));
    return;
  }
  const bool show = !this->maint_active_;
#ifdef USE_API
  if (show) {
    this->maintenance_notice_();
    ESP_LOGW(TAG, LT("Physical maintenance shown (at most 60 minutes): Home Assistant connects again to list it"));
  } else {
    ESP_LOGI(TAG, LT("Physical maintenance hidden: Home Assistant connects again"));
  }
  this->maintenance_show_(show);
#else
  maint_next_boot = show ? MAINT_MAGIC : 0;
  this->restart_reason_ = show ? LT("Physical maintenance switched on, restarting to show it")
                               : LT("Physical maintenance switched off, restarting to hide it");
  this->restart_pending_ = true;
  if (show) this->maintenance_notice_();
#endif
}

bool AquaCleanHub::accept_maintenance_consent() {
  if (this->maint_consent_yaml_ || this->maint_consent_locked_)
    return false;  // already enabled in the YAML, or already decided: a factory reset changes it
  bool v = true;
  this->maint_consent_pref_.save(&v);
  global_preferences->sync();
  this->maint_consent_locked_ = true;
  this->maint_accepted_ = true;
  ESP_LOGW(TAG, LT("Physical maintenance enabled during setup and stored (factory-reset to change). Long-press the "
                   "maintenance key at the toilet to show it."));
  return true;
}

// Free functions for the web-installer setup page (maint_setup.h); they reach the hub through global_aquaclean_hub.
bool maintenance_consent_accepted() {
  return global_aquaclean_hub != nullptr && global_aquaclean_hub->maintenance_consent_accepted();
}
bool maintenance_consent_yaml() {
  return global_aquaclean_hub != nullptr && global_aquaclean_hub->maintenance_consent_yaml();
}
bool maintenance_consent_locked() {
  return global_aquaclean_hub != nullptr && global_aquaclean_hub->maintenance_consent_locked();
}
bool accept_maintenance_consent() {
  return global_aquaclean_hub != nullptr && global_aquaclean_hub->accept_maintenance_consent();
}

void AquaCleanHub::maintenance_switch_(HubSwitch *sw, bool state) {
  // the switch only hides: showing needs someone at the remote control
  if (state && !this->maint_active_) {
    ESP_LOGW(TAG, LT("Physical maintenance can only be switched on with a long press of the maintenance key on the "
                     "remote control"));
    sw->publish_state(false);
    return;
  }
  if (!state && this->maint_active_) {
#ifdef USE_API
    ESP_LOGI(TAG, LT("Physical maintenance hidden: Home Assistant connects again"));
    this->maintenance_show_(false);
#else
    maint_next_boot = 0;
    this->restart_reason_ = LT("Physical maintenance switched off, restarting to hide it");
    this->restart_pending_ = true;
#endif
  }
  sw->publish_state(state);
}

void AquaCleanHub::maintenance_loop_(uint32_t now) {
  if (this->maint_active_ && now - this->maint_since_ >= MAINT_DURATION) {
#ifdef USE_API
    ESP_LOGI(TAG, LT("Physical maintenance: 60 minutes over, hiding it"));
    this->maintenance_show_(false);
#else
    if (!this->restart_pending_) {
      this->restart_reason_ = LT("Physical maintenance: 60 minutes over, hiding it");
      this->restart_pending_ = true;
    }
#endif
  }
#ifdef USE_API
  // close the API connections: Home Assistant connects again and gets the new list of entities and devices
  if (maint_reconnect_at != 0 && static_cast<int32_t>(now - maint_reconnect_at) >= 0) {
    maint_reconnect_at = 0;
    if (api::global_api_server != nullptr)
      for (auto &c : api::global_api_server->active_clients()) c->on_fatal_error();
  }
#endif
}

void AquaCleanHub::maintenance_notice_() {
  // the warning: as a Home Assistant notification, otherwise in the log (MQTT route)
  const std::string body =
      LT("Physical maintenance was switched on with the remote control. The \"Physical maintenance\" device lets Home "
         "Assistant start maintenance procedures on the toilet: descaling, cleaning and calibrating the spray arm, "
         "adjusting the lid and the arms, draining the pump and pipe, and resetting the shower lock and the filter "
         "counter.\n\n"
         "Only use these functions while you are at the toilet yourself, and follow the Geberit operating "
         "instructions:\n"
         "- Make sure nobody sits on the toilet or can use it during a procedure.\n"
         "- Descaling uses descaling agent: follow its instructions and keep it away from children, skin and eyes.\n"
         "- The spray arm, dryer arm and lid can move unexpectedly: keep hands, children and pets clear.\n"
         "- Water, possibly hot, and hot air from the dryer can come out of the toilet onto people nearby or on the "
         "seat: risk of scalding, slipping and water damage.\n"
         "- The toilet is connected to water and mains power: never open it; draining can release water.\n"
         "- A procedure started from a distance cannot be supervised. An interrupted or wrongly confirmed step can "
         "damage the toilet or lock the shower until the descaling is completed.\n"
         "- Unlocking the shower or resetting counters without doing the maintenance hides a real need for it.\n\n"
         "You use these functions at your own risk. This project is not made by Geberit; claims under warranty or "
         "liability towards Geberit may be affected. The service terms of your installer and/or service provider may "
         "be affected too. The authors accept no liability for any injury or damage, including scalding, slipping "
         "or water damage.\n\n"
         "The device is hidden again after 60 minutes, with another long press of the maintenance key on the remote "
         "control, with the "
         "switch \"Physical maintenance\" or when the ESP restarts.");
#ifdef USE_API_HOMEASSISTANT_SERVICES
  if (api::global_api_server != nullptr && api::global_api_server->is_connected()) {
    const std::string title = LT("AquaClean: physical maintenance switched on");
    api::HomeassistantActionRequest req;
    req.service = StringRef("persistent_notification.create");
    req.data.init(3);
    auto &t = req.data.emplace_back();
    t.key = StringRef("title");
    t.value = StringRef(title);
    auto &m = req.data.emplace_back();
    m.key = StringRef("message");
    m.value = StringRef(body);
    auto &n = req.data.emplace_back();
    n.key = StringRef("notification_id");
    n.value = StringRef("aquaclean_maintenance");
    api::global_api_server->send_homeassistant_action(req);
    return;
  }
#endif
  ESP_LOGW(TAG, "%s", body.c_str());
}

}  // namespace esphome::geberit_aquaclean

#endif
