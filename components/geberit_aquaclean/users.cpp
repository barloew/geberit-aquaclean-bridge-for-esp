#ifdef USE_ESP32

#include <algorithm>
#include <cctype>
#include <cstring>
#include <strings.h>

#include "log_text.h"
#include "hub.h"
#include "remote_link.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif
#include "esphome/core/application.h"
#include "mbedtls/aes.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean.users";

// Users: every profile (1-3 in the toilet, 4-6 only on the ESP) can have a name, a Bluetooth device and a
// welcome light. When the device comes within about a metre of the ESP (signal above the threshold), the
// profile is loaded into the session settings and the welcome light blinks twice.

static constexpr uint32_t USER_MAGIC = 0x55535232;
static constexpr uint32_t FOUND_MAGIC = 0x464E4431;
static constexpr uint32_t IRK_MAGIC = 0x49524B31;
static constexpr uint32_t SEARCH_MS = 20000;
static constexpr uint32_t LOST_MS = 30000;          // not near for this long: no longer recognized
static constexpr int STAY_MARGIN = 15;              // once recognized, near = within this many dB below the threshold
static constexpr uint32_t BOOST_MAX_MS = 60000;     // scan boost at most this long in one go ...
static constexpr uint32_t BOOST_PAUSE_MS = 120000;  // ... then a pause (a phone lying just outside the range)
static constexpr uint32_t WELCOME_AGAIN_MS = 5 * 60000;  // load and greet the same user at most every 5 min
static constexpr size_t MAX_FOUND = 10;
static constexpr uint8_t COMMON_COLOR = 0x02;
static constexpr uint8_t COMMON_BRIGHTNESS = 0x01;
static constexpr uint8_t CMD_LIGHT_TOGGLE = 0x14;
static constexpr uint8_t PROC_SET_COMMAND = 0x09;
static constexpr int SCAN_BOOST_MARGIN = 20;        // dB below the threshold that start faster scanning
static constexpr uint32_t SCAN_BOOST_MS = 20000;    // ... for this long after the last such sighting
static constexpr uint32_t SCAN_WINDOW_NORMAL = 48;  // 30 ms (0.625 ms units), as in packages/aquaclean.yaml
static constexpr uint32_t SCAN_WINDOW_BOOST = 256;  // 160 ms of every 320 ms

struct FoundStore {
  uint32_t magic;
  uint8_t count;
  DeviceId devices[MAX_FOUND];
};

// Toilet colors for the board LED (same order as common setting 02)
static const float LED_COLORS[7][3] = {
    {0.0f, 0.0f, 1.0f}, {0.0f, 0.85f, 0.8f},  {1.0f, 0.0f, 1.0f}, {1.0f, 0.5f, 0.0f},
    {1.0f, 1.0f, 0.0f}, {1.0f, 0.72f, 0.42f}, {1.0f, 1.0f, 1.0f},
};

bool DeviceId::same(const DeviceId &o) const {
  if (this->type != o.type || this->type == 0) return false;
  if (this->type == 1) return this->mac == o.mac;
  return memcmp(this->uuid, o.uuid, 16) == 0 && this->major == o.major && this->minor == o.minor;
}

static std::string mac_text(uint64_t a) {
  return str_sprintf("%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)(a >> 40) & 0xFF, (unsigned)(a >> 32) & 0xFF,
                     (unsigned)(a >> 24) & 0xFF, (unsigned)(a >> 16) & 0xFF, (unsigned)(a >> 8) & 0xFF,
                     (unsigned)a & 0xFF);
}

/// The identity a device can be recognized by again, or type 0
static DeviceId identify(const esp32_ble_tracker::ESPBTDevice &device) {
  DeviceId id{};
  auto ib = device.get_ibeacon();
  if (ib.has_value()) {
    id.type = 2;
    memcpy(id.uuid, ib->get_uuid().uuid128(), 16);
    id.major = ib->get_major();
    id.minor = ib->get_minor();
    snprintf(id.label, sizeof(id.label), "iBeacon %u/%u %02X%02X%02X%02X", id.major, id.minor, id.uuid[15], id.uuid[14],
             id.uuid[13], id.uuid[12]);
    return id;
  }
  const uint64_t a = device.address_uint64();
  // public address, or random static (top bits 11): stays the same. Phones without iBeacon change it.
  const bool fixed = device.get_address_type() == BLE_ADDR_TYPE_PUBLIC ||
                     (device.get_address_type() == BLE_ADDR_TYPE_RANDOM && ((a >> 46) & 0x3) == 0x3);
  if (!fixed) return id;
  id.type = 1;
  id.mac = a;
  const std::string name(device.get_name().c_str(), device.get_name().size());
  if (!name.empty())
    snprintf(id.label, sizeof(id.label), "%.20s %s", name.c_str(), mac_text(a).c_str());
  else
    snprintf(id.label, sizeof(id.label), "%s", mac_text(a).c_str());
  return id;
}

// ---------------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------------

void UserText::control(const std::string &value) {
  if (this->hub_ == nullptr) return;
  if (this->kind_ == 0) {
    this->hub_->set_user_name(this->user_, value);
  } else if (this->kind_ == 2) {
    // IRK input: the key is stored in the profile, the field itself always stays empty
    this->hub_->set_user_irk_text(this->user_, value);
    this->publish_state("");
  } else {
    this->hub_->set_user_entity(this->user_, value);
  }
}

static uint64_t parse_mac(const std::string &s) {
  uint64_t v = 0;
  int digits = 0;
  for (char c : s) {
    int h;
    if (c >= '0' && c <= '9')
      h = c - '0';
    else if (c >= 'a' && c <= 'f')
      h = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      h = c - 'A' + 10;
    else
      continue;
    v = (v << 4) | h;
    digits++;
  }
  return digits == 12 ? v : 0;
}

static std::string lower(std::string s) {
  for (auto &c : s)
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  return s;
}

void UserSelect::control(size_t index) {
  if (this->hub_ == nullptr) return;
  if (this->kind_ == 0)
    this->hub_->select_user_device(this->user_, index);
  else
    this->hub_->set_user_welcome(this->user_, index);
}

void ThresholdNumber::control(float value) {
  if (this->hub_ != nullptr) this->hub_->set_threshold(value);
}

// ---------------------------------------------------------------------------------------------------
// Setup and storage
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::users_setup_() {
  const uint8_t lang = this->language_ < LANGUAGE_COUNT ? this->language_ : 0;
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    this->user_prefs_[u] =
        global_preferences->make_preference<UserData>(fnv1_hash(str_sprintf("geberit_aquaclean_user_%u", u + 1)));
    UserData d{};
    if (!this->user_prefs_[u].load(&d) || d.magic != USER_MAGIC) {
      d = UserData{};
      d.magic = USER_MAGIC;
      for (auto &v : d.values) v = -1;
      // stored by v1.8.0 (without the Home Assistant device): keep it
      struct UserDataV180 {
        uint32_t magic;
        DeviceId dev;
        uint8_t welcome;
        uint8_t created;
        char name[24];
        int16_t values[USER_VALUES];
      } old{};
      auto pref =
          global_preferences->make_preference<UserDataV180>(fnv1_hash(str_sprintf("geberit_aquaclean_user_%u", u + 1)));
      if (pref.load(&old) && old.magic == 0x55535231) {
        d.dev = old.dev;
        d.welcome = old.welcome;
        d.created = old.created;
        memcpy(d.name, old.name, sizeof(d.name));
        memcpy(d.values, old.values, sizeof(d.values));
      }
    }
    // setting ids 0a-0d (dryer intensity, ...): separate store, so UserData keeps its layout
    this->user_x_prefs_[u] =
        global_preferences->make_preference<UserValuesX>(fnv1_hash(str_sprintf("geberit_aquaclean_userx_%u", u + 1)));
    if (!this->user_x_prefs_[u].load(&this->users_x_[u]) || this->users_x_[u].magic != USER_MAGIC) {
      this->users_x_[u].magic = USER_MAGIC;
      for (auto &v : this->users_x_[u].values) v = -1;
    }
    d.created = 1;  // all six profiles exist (toilet ones hidden by the discovery when the toilet lacks them)
    d.name[sizeof(d.name) - 1] = 0;
    d.entity[sizeof(d.entity) - 1] = 0;
    d.dev.label[sizeof(d.dev.label) - 1] = 0;
    this->users_[u] = d;
  }

  // IRKs captured per profile (separate store, so the profile data keeps its layout)
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    this->irk_prefs_[u] =
        global_preferences->make_preference<IrkStore>(fnv1_hash(str_sprintf("geberit_aquaclean_irk_%u", u + 1)));
    IrkStore k{};
    if (this->irk_prefs_[u].load(&k) && k.magic == IRK_MAGIC) this->irks_[u] = k;
  }

  // Devices found by the last search, plus the devices already assigned: the options of every device select
  this->found_pref_ = global_preferences->make_preference<FoundStore>(fnv1_hash("geberit_aquaclean_found"));
  FoundStore fs{};
  if (this->found_pref_.load(&fs) && fs.magic == FOUND_MAGIC) {
    for (uint8_t i = 0; i < fs.count && i < MAX_FOUND; i++) {
      fs.devices[i].label[sizeof(fs.devices[i].label) - 1] = 0;
      this->found_.push_back(fs.devices[i]);
    }
  }
  this->device_options_.push_back("—");
  auto add_option = [this](const DeviceId &d) {
    if (d.type == 0) return;
    for (size_t i = 1; i < this->device_options_.size(); i++)
      if (this->device_options_[i] == d.label) return;
    this->device_options_.push_back(d.label);
  };
  for (auto &u : this->users_) add_option(u.dev);
  for (auto &d : this->found_) add_option(d);
  for (auto &o : this->device_options_) this->device_option_ptrs_.push_back(o.c_str());
  for (auto *sel : this->device_selects_)
    if (sel != nullptr)
      sel->traits.set_options_static(this->device_option_ptrs_.data(), this->device_option_ptrs_.size());
  for (auto *sel : this->welcome_selects_)
    if (sel != nullptr) sel->apply_language(lang);

  this->threshold_pref_ = global_preferences->make_preference<int8_t>(fnv1_hash("geberit_aquaclean_threshold"));
  int8_t t;
  if (this->threshold_pref_.load(&t) && t >= -100 && t <= -30) this->threshold_ = t;

  // Settings of the Home Assistant profiles
  for (auto *s : this->settings_) {
    if (s->kind() != KIND_LOCAL || s->profile() < 1 || s->profile() > MAX_USERS) continue;
    const int16_t *v = this->user_value_(s->profile() - 1, s->setting_id());
    if (v != nullptr && *v >= 0) s->update_value(static_cast<uint16_t>(*v));
  }
  // Linked Home Assistant device trackers: follow their current address / iBeacon id. Home Assistant sends
  // the subscribed states when it connects, so a new link takes a restart.
#ifdef USE_API_HOMEASSISTANT_STATES
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    const char *entity = this->users_[u].entity;  // lives as long as the hub
    if (entity[0] == 0 || api::global_api_server == nullptr) continue;
    auto *ha = &this->ha_devices_[u];
    auto *srv = api::global_api_server;
    srv->subscribe_home_assistant_state(entity, "current_address", [ha, entity](StringRef v) {
      const uint64_t a = parse_mac(std::string(v.c_str(), v.size()));
      if (a != ha->address) ESP_LOGI(TAG, LT("%s: current address %s"), ob(entity), ob(mac_text(a)));
      ha->address = a;
    });
    srv->subscribe_home_assistant_state(entity, "uuid", [ha, entity](StringRef v) {
      const std::string u = lower(std::string(v.c_str(), v.size()));
      if (u != ha->uuid) ESP_LOGI(TAG, LT("%s: iBeacon %s"), ob(entity), ob(u));
      ha->uuid = u;
    });
    srv->subscribe_home_assistant_state(
        entity, "major", [ha](StringRef v) { ha->major = atoi(std::string(v.c_str(), v.size()).c_str()); });
    srv->subscribe_home_assistant_state(
        entity, "minor", [ha](StringRef v) { ha->minor = atoi(std::string(v.c_str(), v.size()).c_str()); });
    ESP_LOGI(TAG, LT("%s follows %s"), ob(this->user_label_(u)), ob(entity));
  }
#elif defined(USE_MQTT)
  // MQTT route: Home Assistant publishes the tracker attributes with mqtt_statestream
  // (<base>/device_tracker/<object>/current_address, JSON-encoded values).
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    const char *entity = this->users_[u].entity;
    if (entity[0] == 0 || mqtt::global_mqtt_client == nullptr || this->statestream_topic_.empty()) continue;
    std::string path = entity;
    const size_t dot = path.find('.');
    if (dot == std::string::npos) continue;
    path = this->statestream_topic_ + "/" + path.substr(0, dot) + "/" + path.substr(dot + 1) + "/";
    auto *ha = &this->ha_devices_[u];
    auto unq = [](const std::string &v) {
      if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
      return v;
    };
    auto *mq = mqtt::global_mqtt_client;
    mq->subscribe(path + "current_address", [ha, entity, unq](const std::string &, const std::string &v) {
      const uint64_t a = parse_mac(unq(v));
      if (a != ha->address) ESP_LOGI(TAG, LT("%s: current address %s"), ob(entity), ob(mac_text(a)));
      ha->address = a;
    });
    mq->subscribe(path + "uuid", [ha, entity, unq](const std::string &, const std::string &v) {
      const std::string id = lower(unq(v));
      if (id != ha->uuid) ESP_LOGI(TAG, LT("%s: iBeacon %s"), ob(entity), ob(id));
      ha->uuid = id;
    });
    mq->subscribe(path + "major",
                  [ha, unq](const std::string &, const std::string &v) { ha->major = atoi(unq(v).c_str()); });
    mq->subscribe(path + "minor",
                  [ha, unq](const std::string &, const std::string &v) { ha->minor = atoi(unq(v).c_str()); });
    ESP_LOGI(TAG, LT("%s follows %s via MQTT (%s...)"), ob(this->user_label_(u)), ob(entity), ob(path));
  }
#endif
  this->publish_users_();
}

void AquaCleanHub::set_user_entity(int user, const std::string &entity_id) {
  if (user < 1 || user > MAX_USERS) return;
  if (entity_id == "-") {
    // "-" from the link script (no device chosen): clear the whole profile
    this->delete_user_profile(user);
    return;
  }
  auto &d = this->users_[user - 1];
  if (entity_id == d.entity) return;
  snprintf(d.entity, sizeof(d.entity), "%s", entity_id.c_str());
  this->save_user_(user - 1);
  ESP_LOGI(TAG, LT("%s linked to %s; restarting so Home Assistant sends its state"), ob(this->user_label_(user - 1)),
           d.entity[0] ? ob(d.entity) : LT("nothing"));
  this->publish_users_();
  this->reboot_at_ = millis() + 1500;
}

bool AquaCleanHub::matches_user_(uint8_t u, const DeviceId &id, const esp32_ble_tracker::ESPBTDevice &device) const {
  if (this->users_[u].dev.same(id)) return true;
  if (this->irks_[u].magic == IRK_MAGIC && const_cast<AquaCleanHub *>(this)->rpa_owner_(device.address_uint64()) == u)
    return true;  // the phone's own key resolves its random address
  const HaDevice &ha = this->ha_devices_[u];
  if (this->users_[u].entity[0] == 0) return false;
  if (ha.address != 0 && device.address_uint64() == ha.address)
    return true;  // Private BLE Device: the address it uses right now
  if (!ha.uuid.empty() && id.type == 2) {
    auto ib = device.get_ibeacon();
    if (ib.has_value()) {
      char buf[48];
      ib->get_uuid().to_str(buf);
      return lower(buf) == ha.uuid && (ha.major < 0 || ha.major == ib->get_major()) &&
             (ha.minor < 0 || ha.minor == ib->get_minor());
    }
  }
  return false;
}

void AquaCleanHub::save_user_(uint8_t u) {
  if (u >= MAX_USERS) return;
  this->users_[u].magic = USER_MAGIC;
  this->user_prefs_[u].save(&this->users_[u]);
  this->users_x_[u].magic = USER_MAGIC;
  this->user_x_prefs_[u].save(&this->users_x_[u]);
  global_preferences->sync();
}

std::string AquaCleanHub::user_label_(int u) const {
  const uint8_t lang = this->language_ < LANGUAGE_COUNT ? this->language_ : 0;
  if (u < 0 || u >= MAX_USERS) return this->nobody_[lang] != nullptr ? this->nobody_[lang] : "";
  if (this->users_[u].name[0] != 0) return this->users_[u].name;
  return this->user_labels_[lang] != nullptr ? this->user_labels_[lang][u] : str_sprintf("%d", u + 1);
}

// ---------------------------------------------------------------------------------------------------
// IRK: resolve random private addresses with the key the phone gave:
// ah(k, r) = e(k, 0^104 || prand) mod 2^24; address = prand (MSB, top bits 01) || hash
// ---------------------------------------------------------------------------------------------------

static std::string irk_hex(const uint8_t *key) {
  char buf[33];
  for (int i = 0; i < 16; i++) sprintf(buf + i * 2, "%02x", key[i]);
  return std::string(buf, 32);
}

int8_t AquaCleanHub::rpa_owner_(uint64_t address) {
  if (((address >> 46) & 0x3) != 0x1) return -1;  // not a resolvable private address
  for (auto &c : this->rpa_cache_)
    if (c.address == address) return c.owner;
  int8_t owner = -1;
  uint8_t in[16] = {0}, out[16];
  in[13] = (address >> 40) & 0xFF;
  in[14] = (address >> 32) & 0xFF;
  in[15] = (address >> 24) & 0xFF;
  const uint32_t hash = static_cast<uint32_t>(address & 0xFFFFFF);
  for (uint8_t u = 0; u < MAX_USERS && owner < 0; u++) {
    if (this->irks_[u].magic != IRK_MAGIC) continue;
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, this->irks_[u].key, 128);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&ctx);
    if (((uint32_t(out[13]) << 16) | (uint32_t(out[14]) << 8) | out[15]) == hash) owner = static_cast<int8_t>(u);
  }
  this->rpa_cache_[this->rpa_next_] = {address, owner};
  this->rpa_next_ = (this->rpa_next_ + 1) % this->rpa_cache_.size();
  return owner;
}

void AquaCleanHub::publish_irk_(uint8_t u) {
  if (u >= MAX_USERS || this->irk_sensors_[u] == nullptr) return;
  // only the last four digits: the key itself is shown on request (show_irk_), like ESPHome's encryption key
  std::string v;
  if (this->irks_[u].magic == IRK_MAGIC)
    v = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022 " + irk_hex(this->irks_[u].key).substr(28);
  if (!this->irk_sensors_[u]->has_state() || this->irk_sensors_[u]->state != v) this->irk_sensors_[u]->publish_state(v);
}

void AquaCleanHub::show_irk_(uint8_t u) {
  if (u >= MAX_USERS) return;
  const std::string who = this->user_label_(u);
  if (this->irks_[u].magic != IRK_MAGIC) {
    ESP_LOGW(TAG, LT("%s: no IRK stored"), ob(who));
    return;
  }
  const std::string key = irk_hex(this->irks_[u].key);
#ifdef USE_API_HOMEASSISTANT_SERVICES
  if (api::global_api_server != nullptr && api::global_api_server->is_connected()) {
    // a persistent notification in Home Assistant: the key in its own line, to copy
    char title[96], body[512];
    snprintf(title, sizeof(title), LT("IRK of %s"), who.c_str());
    snprintf(body, sizeof(body),
             LT("This is the Identity Resolving Key of the device of %s (smartphone, smartwatch, wearable or tracker). "
                "Keep it in a safe place: with it the device can be recognized. Home Assistant's Private BLE Device "
                "takes it as it is."),
             who.c_str());
    const std::string message = std::string(body) + "\n\n`" + key + "`";
    const std::string id = str_sprintf("aquaclean_irk_%u", u + 1);
    api::HomeassistantActionRequest req;
    req.service = StringRef("persistent_notification.create");
    req.data.init(3);
    auto &t = req.data.emplace_back();
    t.key = StringRef("title");
    t.value = StringRef(title);
    auto &m = req.data.emplace_back();
    m.key = StringRef("message");
    m.value = StringRef(message);
    auto &n = req.data.emplace_back();
    n.key = StringRef("notification_id");
    n.value = StringRef(id);
    api::global_api_server->send_homeassistant_action(req);
    ESP_LOGI(TAG, LT("%s: IRK sent to Home Assistant as a notification"), ob(who));
    return;
  }
#endif
  ESP_LOGI(TAG, LT("%s: IRK %s"), ob(who), ob(key));
}

void AquaCleanHub::set_user_irk(uint8_t user, const uint8_t *irk_le) {
  if (user < 1 || user > MAX_USERS) return;
  auto &k = this->irks_[user - 1];
  k.magic = IRK_MAGIC;
  for (int i = 0; i < 16; i++)
    k.key[i] = irk_le[15 - i];  // MSB first: the AES key, and the form Home Assistant's Private BLE Device takes
  this->irk_prefs_[user - 1].save(&k);
  global_preferences->sync();
  this->rpa_cache_ = {};
  ESP_LOGI(TAG, LT("%s: IRK stored (… %s); the ESP now recognizes this phone by itself"),
           ob(this->user_label_(user - 1)), irk_hex(k.key).substr(28).c_str());
  this->publish_irk_(user - 1);
}

bool AquaCleanHub::set_user_irk_text(uint8_t user, const std::string &irk) {
  if (user < 1 || user > MAX_USERS) return false;
  std::string s;
  for (char c : irk)
    if (c != ' ' && c != ':' && c != '-' && c != '\t' && c != '\n' && c != '\r') s += c;
  if (s.size() > 4 && strncasecmp(s.c_str(), "irk:", 4) == 0) s.erase(0, 4);
  const std::string who = this->user_label_(user - 1);
  if (s.empty()) {
    if (this->irks_[user - 1].magic == IRK_MAGIC) {
      this->irks_[user - 1] = IrkStore{};
      this->irk_prefs_[user - 1].save(&this->irks_[user - 1]);
      global_preferences->sync();
      this->rpa_cache_ = {};
      ESP_LOGI(TAG, LT("%s: IRK removed"), ob(who));
      this->publish_irk_(user - 1);
    }
    return true;
  }
  uint8_t le[16];
  bool ok = false;
  if (s.size() == 32 &&
      std::all_of(s.begin(), s.end(), [](char c) { return isxdigit(static_cast<unsigned char>(c)); })) {
    // hex, most significant byte first
    for (int i = 0; i < 16; i++) le[15 - i] = static_cast<uint8_t>(strtoul(s.substr(i * 2, 2).c_str(), nullptr, 16));
    ok = true;
  } else if (s.size() == 24 && s.back() == '=') {
    // base64 of the 16 bytes, least significant byte first (iOS keychain; Home Assistant reverses it too)
    uint8_t buf[18];
    ok = base64_decode(s, buf, sizeof(buf)) == 16;
    if (ok) memcpy(le, buf, 16);
  }
  if (!ok) {
    ESP_LOGW(TAG, LT("%s: IRK not recognized: expected 32 hex digits or base64"), ob(who));
    return false;
  }
  this->set_user_irk(user, le);
  return true;
}

void AquaCleanHub::irk_capture_ended(bool got) {
  // board LED: two blinks green (key received) or red (nothing)
  this->led_r_ = got ? 0.0f : 1.0f;
  this->led_g_ = got ? 1.0f : 0.0f;
  this->led_b_ = 0.0f;
  this->led_welcome_start_ = millis();
}

void AquaCleanHub::publish_users_() {
  // Publish only what changed: this runs on every recognition and would otherwise resend all six profiles
  auto text = [](text::Text *t, const char *v) {
    if (t != nullptr && (!t->has_state() || t->state != v)) t->publish_state(v);
  };
  auto sel = [](select::Select *s, size_t idx) {
    if (s != nullptr && (!s->has_state() || s->active_index() != idx)) s->publish_state(idx);
  };
  auto bin = [](binary_sensor::BinarySensor *b, bool v) {
    if (b != nullptr && (!b->has_state() || b->state != v)) b->publish_state(v);
  };
  auto tsens = [](text_sensor::TextSensor *t, const std::string &v) {
    if (t != nullptr && (!t->has_state() || t->state != v)) t->publish_state(v);
  };
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    const auto &d = this->users_[u];
    this->publish_irk_(u);
    text(this->name_texts_[u], d.name);
    text(this->entity_texts_[u], d.entity);
    if (this->device_selects_[u] != nullptr) {
      size_t idx = 0;
      for (size_t i = 1; i < this->device_options_.size(); i++)
        if (d.dev.type != 0 && this->device_options_[i] == d.dev.label) idx = i;
      sel(this->device_selects_[u], idx);
    }
    if (d.welcome < 8) sel(this->welcome_selects_[u], static_cast<size_t>(d.welcome));
    bin(this->recognized_sensors_[u], this->ustate_[u].recognized);
    bin(this->active_sensors_[u], this->active_user_ == u);
  }
  tsens(this->recognized_user_sensor_, this->user_label_(this->recognized_user_));
  tsens(this->active_user_sensor_, this->user_label_(this->active_user_));
  if (this->threshold_number_ != nullptr &&
      (!this->threshold_number_->has_state() || this->threshold_number_->state != this->threshold_))
    this->threshold_number_->publish_state(this->threshold_);
}

// ---------------------------------------------------------------------------------------------------
// Changes from Home Assistant
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::set_user_name(uint8_t user, const std::string &name) {
  if (user < 1 || user > MAX_USERS) return;
  if (name.rfind("device_tracker.", 0) == 0) {
    // a device tracker written to the name field: it is meant as the Home Assistant device
    this->publish_users_();
    this->set_user_entity(user, name);
    return;
  }
  auto &d = this->users_[user - 1];
  snprintf(d.name, sizeof(d.name), "%s", name.c_str());
  this->save_user_(user - 1);
  this->publish_users_();
}

void AquaCleanHub::select_user_device(uint8_t user, size_t index) {
  if (user < 1 || user > MAX_USERS || index >= this->device_options_.size()) return;
  auto &d = this->users_[user - 1];
  if (index == 0) {
    d.dev = DeviceId{};
  } else {
    const std::string &label = this->device_options_[index];
    bool found = false;
    for (auto &f : this->found_)
      if (!found && label == f.label) {
        d.dev = f;
        found = true;
      }
    for (auto &o : this->users_)
      if (!found && o.dev.type != 0 && label == o.dev.label) {
        d.dev = o.dev;
        found = true;
      }
    if (!found) return;
  }
  ESP_LOGI(TAG, LT("%s: device %s"), ob(this->user_label_(user - 1)), d.dev.type == 0 ? LT("none") : ob(d.dev.label));
  this->ustate_[user - 1] = UserState{};
  if (this->irks_[user - 1].magic == IRK_MAGIC) {
    this->irks_[user - 1] = IrkStore{};
    this->irk_prefs_[user - 1].save(&this->irks_[user - 1]);
    this->rpa_cache_ = {};
  }
  this->publish_irk_(user - 1);
  this->save_user_(user - 1);
  this->publish_users_();
}

void AquaCleanHub::set_user_welcome(uint8_t user, size_t index) {
  if (user < 1 || user > MAX_USERS || index > 7) return;
  this->users_[user - 1].welcome = static_cast<uint8_t>(index);
  this->save_user_(user - 1);
  this->publish_users_();
}

void AquaCleanHub::set_threshold(float value) {
  this->threshold_ = static_cast<int8_t>(std::max(-100.0f, std::min(-30.0f, value)));
  this->threshold_pref_.save(&this->threshold_);
  global_preferences->sync();
  this->publish_users_();
}

void AquaCleanHub::invalidate_profile_and_restart_() {
  // The list of entities shown changes: forget the stored result (the model is kept) and restart, so
  // Home Assistant gets the new list
  Profile p = this->profile_;
  p.magic = PROFILE_MAGIC;
  p.layout = 0;
  p.model = this->fd_model_;
  this->profile_pref_.save(&p);
  global_preferences->sync();
  ESP_LOGI(TAG, LT("Restarting so Home Assistant gets the changed entities"));
  this->reboot_at_ = millis() + 1500;
}

void AquaCleanHub::add_user_profile() {}  // all six profiles always exist

void AquaCleanHub::delete_user_profile(uint8_t user) {
  // Clear: name, devices and welcome light; Home Assistant profiles start again from the default settings
  if (user < 1 || user > MAX_USERS) return;
  auto &d = this->users_[user - 1];
  d.dev = DeviceId{};
  d.entity[0] = 0;
  d.name[0] = 0;
  d.welcome = 0;
  if (user > TOILET_PROFILES) {
    for (auto &v : d.values) v = -1;
    for (auto &v : this->users_x_[user - 1].values) v = -1;
    for (auto *s : this->settings_) {
      int16_t *slot = this->user_value_(user - 1, s->setting_id());
      if (s->kind() == KIND_DEFAULT && s->profile() == 0 && slot != nullptr && s->value() >= 0)
        *slot = static_cast<int16_t>(s->value());
    }
    for (auto *s : this->settings_) {
      const int16_t *slot = this->user_value_(user - 1, s->setting_id());
      if (s->kind() == KIND_LOCAL && s->profile() == user && slot != nullptr && *slot >= 0)
        s->update_value(static_cast<uint16_t>(*slot));
    }
  }
  this->ustate_[user - 1] = UserState{};
  if (this->irks_[user - 1].magic == IRK_MAGIC) {
    this->irks_[user - 1] = IrkStore{};
    this->irk_prefs_[user - 1].save(&this->irks_[user - 1]);
    this->rpa_cache_ = {};
  }
  this->publish_irk_(user - 1);
  this->save_user_(user - 1);
  ESP_LOGI(TAG, LT("User profile %u cleared"), user);
  this->publish_users_();
}

// ---------------------------------------------------------------------------------------------------
// Device search
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::search_devices() {
  this->search_found_.clear();
  this->search_until_ = millis() + SEARCH_MS;
  ESP_LOGI(TAG, LT("Searching for Bluetooth devices for %u s: hold the device next to the ESP"),
           (unsigned)(SEARCH_MS / 1000));
}

void AquaCleanHub::on_advert(const esp32_ble_tracker::ESPBTDevice &device) {
  const uint32_t now = millis();
  bool any_user = false;
  for (uint8_t k = 0; k < MAX_USERS; k++)
    any_user |= this->users_[k].dev.type != 0 || this->users_[k].entity[0] != 0 || this->irks_[k].magic == IRK_MAGIC;
  if (!any_user && this->search_until_ == 0) return;
  const DeviceId id = identify(device);  // type 0 for changing addresses (still matched via Home Assistant)
  const int rssi = device.get_rssi();

  if (this->search_until_ != 0 && id.type != 0) {
    // not the toilet, not the remote control, not another toilet
    bool skip = this->parent() != nullptr && id.type == 1 && id.mac == this->parent()->get_address();
    for (const auto &m : device.get_manufacturer_datas())
      if (m.data.size() >= 4 && m.data[1] == '1' && m.data[2] == '4' && m.data[3] == '6') skip = true;
    if (!skip && rssi > -90) {
      bool merged = false;
      for (auto &f : this->search_found_)
        if (f.first.same(id)) {
          f.second = std::max(f.second, rssi);
          if (f.first.label[0] != 0 && strchr(id.label, ' ') != nullptr && strchr(f.first.label, ' ') == nullptr)
            memcpy(f.first.label, id.label, sizeof(id.label));  // a name came along later
          merged = true;
        }
      if (!merged && this->search_found_.size() < 60) this->search_found_.push_back({id, rssi});
    }
  }

  for (uint8_t u = 0; u < MAX_USERS; u++) {
    if (!this->users_[u].created || !this->matches_user_(u, id, device)) continue;
    auto &st = this->ustate_[u];
    st.rssi = rssi;
    if (st.last_seen == 0) ESP_LOGI(TAG, LT("%s seen (%d dBm)"), ob(this->user_label_(u)), rssi);
    st.last_seen = now;
    if (this->rssi_sensors_[u] != nullptr &&
        (now - st.last_rssi_pub >= 30000 || (now - st.last_rssi_pub >= 5000 && abs(rssi - st.published_rssi) >= 10))) {
      this->rssi_sensors_[u]->publish_state(rssi);
      st.last_rssi_pub = now;
      st.published_rssi = rssi;
    }
    if (rssi >= this->threshold_) {
      st.last_near = now;
      if (!st.recognized) this->recognized_(u, now);
    } else if (st.recognized && rssi >= this->threshold_ - STAY_MARGIN) {
      st.last_near = now;  // hysteresis: the signal of a phone in a pocket jumps 20 dB from one packet to the next
    } else if (!st.recognized && rssi >= this->threshold_ - SCAN_BOOST_MARGIN &&
               (this->boost_pause_until_ == 0 || static_cast<int32_t>(now - this->boost_pause_until_) >= 0)) {
      // Close to the threshold: scan faster for a while, so the step over it is seen within a second
      if (this->scan_boost_until_ == 0) this->boost_started_ = now;
      this->scan_boost_until_ = now + SCAN_BOOST_MS;
    }
  }
}

// ---------------------------------------------------------------------------------------------------
// Recognition, welcome, seated
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::recognized_(uint8_t u, uint32_t now, uint8_t by) {
  const bool by_app = by != 0;  // app or remote control: the profile is loaded already
  auto &st = this->ustate_[u];
  st.recognized = true;
  st.last_near = now;
  this->recognized_user_ = u;
  // Recognized: no need to scan faster any more (another user near the threshold switches it on again)
  this->scan_boost_until_ = 0;
  this->boost_pause_until_ = 0;
  if (by == 1)
    ESP_LOGI(TAG, LT("%s recognized: the Geberit Home app connected from this phone"), ob(this->user_label_(u)));
  else if (by == 2)
    ESP_LOGI(TAG, LT("%s recognized: profile key %u of the remote control"), ob(this->user_label_(u)), u + 1);
  else
    ESP_LOGI(TAG, LT("%s recognized (%d dBm)"), ob(this->user_label_(u)), st.rssi);
  const bool busy = this->is_seated() || this->status_[1] == 1 || this->status_[2] == 1;
  if (!busy && (!st.welcomed || now - st.last_welcome >= WELCOME_AGAIN_MS)) {
    st.welcomed = true;
    st.last_welcome = now;
    if (by_app) {
      // the app / the profile key loads the profile into the session itself: only the welcome light
      this->welcome_(u);
    } else {
      // Load the profile; right after a (re)start the settings are not read yet: wait (at most a minute)
      this->pending_welcome_ = u;
      this->pending_since_ = now;
    }
  }
  this->publish_users_();
}

void AquaCleanHub::on_remote_profile(uint8_t profile) {
  // A profile key (1-3) of the remote control: the user of that profile is at the toilet. The toilet loads
  // the profile itself. Recognized for two minutes, so sitting down afterwards makes him the active user.
  if (profile < 1 || profile > TOILET_PROFILES || !this->users_[profile - 1].created) return;
  const uint8_t u = profile - 1;
  const uint32_t now = millis();
  this->ustate_[u].hold_until = now + 120000;
  this->recognized_(u, now, 2);
  if (this->is_seated() && this->active_user_ != u) {
    this->active_user_ = u;
    ESP_LOGI(TAG, LT("%s is seated"), ob(this->user_label_(u)));
    this->publish_users_();
  }
}

void AquaCleanHub::on_app_device(uint64_t address) {
  this->app_user_ = -1;
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    const bool fixed = this->users_[u].dev.type == 1 && this->users_[u].dev.mac == address;
    const bool ha = this->users_[u].entity[0] != 0 && this->ha_devices_[u].address == address;
    const bool irk = this->irks_[u].magic == IRK_MAGIC && this->rpa_owner_(address) == u;
    if (this->users_[u].created && (fixed || ha || irk)) {
      this->app_user_ = u;
      this->recognized_(u, millis(), 1);
      return;
    }
  }
  ESP_LOGD(TAG, LT("App connected from %s: no linked user"), ob(mac_text(address)));
}

bool AquaCleanHub::welcome_ready_(uint8_t u) const {
  if (!this->is_ready() || this->fd_running_) return false;
  const SettingKind kind = u + 1 <= TOILET_PROFILES ? KIND_DEFAULT : KIND_LOCAL;
  for (auto *s : this->settings_) {
    if (s->hidden) continue;
    if (s->kind() == kind && s->profile() == u + 1 && s->value() < 0) return false;  // profile not read yet
    if (s->kind() == KIND_COMMON && (s->setting_id() == COMMON_COLOR || s->setting_id() == COMMON_BRIGHTNESS) &&
        s->value() < 0)
      return false;  // light color / brightness not read yet
  }
  return true;
}

void AquaCleanHub::users_on_seated_(bool seated) {
  int8_t active = -1;
  if (seated && this->recognized_user_ >= 0 && this->ustate_[this->recognized_user_].recognized)
    active = this->recognized_user_;
  if (active != this->active_user_) {
    this->active_user_ = active;
    if (active >= 0) ESP_LOGI(TAG, LT("%s is seated"), ob(this->user_label_(active)));
    this->publish_users_();
  }
}

void AquaCleanHub::set_scan_boost_(bool on) {
  auto *tracker = esp32_ble_tracker::global_esp32_ble_tracker;
  if (tracker == nullptr) return;
  this->scan_boosted_ = on;
  // Window in 0.625 ms units at an interval of 320 ms (512): 30 ms normally (as in packages/aquaclean.yaml),
  // 160 ms (50 %) while boosting. The tracker restarts the scan itself when the window changes.
  tracker->set_scan_window(on ? SCAN_WINDOW_BOOST : SCAN_WINDOW_NORMAL);
  ESP_LOGD(TAG, LT("Scan boost %s"), on ? LT("on (linked device close to the threshold)") : LT("off"));
}

void AquaCleanHub::welcome_(uint8_t u) {
  const uint8_t w = this->users_[u].welcome;
  if (w == 0 || w > 7) return;
  const uint8_t color = w - 1;
  const uint32_t now = millis();
  // Board LED: exact color, two blinks
  this->led_r_ = LED_COLORS[color][0];
  this->led_g_ = LED_COLORS[color][1];
  this->led_b_ = LED_COLORS[color][2];
  this->led_welcome_start_ = now;
  // Orientation light of the toilet: color, full brightness, off-on twice, then back as it was
  if (!this->is_ready() || this->light_ == nullptr) return;
  SettingEntity *col = this->find_setting(KIND_COMMON, COMMON_COLOR);
  SettingEntity *bri = this->find_setting(KIND_COMMON, COMMON_BRIGHTNESS);
  if (col == nullptr || bri == nullptr || col->hidden || col->value() < 0 || bri->value() < 0) return;
  const bool was_on = this->status_[9] == 1;
  this->welcome_steps_.clear();
  this->welcome_steps_.push_back({now, 1, COMMON_COLOR, color});
  this->welcome_steps_.push_back({now, 1, COMMON_BRIGHTNESS, 4});
  uint32_t t = now;
  if (!was_on) this->welcome_steps_.push_back({t, 2, 0, 0});  // on
  for (int i = 0; i < 4; i++) {
    t += 900;
    this->welcome_steps_.push_back({t, 2, 0, 0});  // off, on, off, on
  }
  t += 1200;
  this->welcome_steps_.push_back({t, 1, COMMON_COLOR, static_cast<uint16_t>(col->value())});
  this->welcome_steps_.push_back({t, 1, COMMON_BRIGHTNESS, static_cast<uint16_t>(bri->value())});
  if (!was_on) this->welcome_steps_.push_back({t, 2, 0, 0});  // off again
}

bool AquaCleanHub::welcome_led(float &r, float &g, float &b) {
  if (this->led_welcome_start_ == 0) return false;
  const uint32_t t = millis() - this->led_welcome_start_;
  if (t >= 1600) {
    this->led_welcome_start_ = 0;
    return false;
  }
  const bool on = (t / 400) % 2 == 0;  // on, off, on, off
  r = on ? this->led_r_ : 0.0f;
  g = on ? this->led_g_ : 0.0f;
  b = on ? this->led_b_ : 0.0f;
  return true;
}

void AquaCleanHub::users_loop_(uint32_t now) {
  if (this->pending_welcome_ >= 0) {
    const uint8_t u = this->pending_welcome_;
    if (this->welcome_ready_(u)) {
      this->pending_welcome_ = -1;
      this->welcome_(u);  // first the light (urgent), then the profile
      this->activate_profile(u + 1);
    } else if (now - this->pending_since_ >= 60000) {
      this->pending_welcome_ = -1;
      ESP_LOGW(TAG, LT("%s: settings not known within a minute, profile not loaded"), ob(this->user_label_(u)));
    }
  }
  // welcome light steps
  while (!this->welcome_steps_.empty() && static_cast<int32_t>(now - this->welcome_steps_.front().at) >= 0) {
    const WelcomeStep st = this->welcome_steps_.front();
    this->welcome_steps_.pop_front();
    if (!this->is_ready()) continue;
    if (st.op == 1)
      this->enqueue_urgent_(0x01, 0x0B, {st.id, static_cast<uint8_t>(st.value), static_cast<uint8_t>(st.value >> 8)});
    else
      this->enqueue_urgent_(0x01, PROC_SET_COMMAND, {CMD_LIGHT_TOGGLE});
  }
  // Faster scanning while a linked device is close to the threshold
  if (this->scan_boost_until_ != 0 && now - this->boost_started_ >= BOOST_MAX_MS) {
    // a phone lying just outside the range would keep the boost on: pause it
    this->scan_boost_until_ = 0;
    this->boost_pause_until_ = now + BOOST_PAUSE_MS;
  }
  // not during an IRK capture: the radio time goes to advertising and the pairing of the phone
  const bool capturing = this->link_ != nullptr && this->link_->irk_capturing();
  const bool boost =
      !capturing && this->scan_boost_until_ != 0 && static_cast<int32_t>(now - this->scan_boost_until_) < 0;
  if (boost != this->scan_boosted_) this->set_scan_boost_(boost);
  if (!boost) this->scan_boost_until_ = 0;

  // the user whose phone has the app connected stays recognized
  if (this->app_user_ >= 0) {
    if (this->app_connected_)
      this->ustate_[this->app_user_].last_near = now;
    else
      this->app_user_ = -1;
  }

  // recognized through a profile key: held for a while
  for (auto &st : this->ustate_)
    if (st.hold_until != 0) {
      if (static_cast<int32_t>(now - st.hold_until) < 0)
        st.last_near = now;
      else
        st.hold_until = 0;
    }

  // users out of reach
  bool changed = false;
  for (uint8_t u = 0; u < MAX_USERS; u++) {
    auto &st = this->ustate_[u];
    if (st.last_seen != 0 && now - st.last_seen >= 60000) {
      st.last_seen = 0;  // not seen for a minute: signal unknown
      if (this->rssi_sensors_[u] != nullptr) this->rssi_sensors_[u]->publish_state(NAN);
    }
    if (st.recognized && this->active_user_ == u) st.last_near = now;  // seated: stays recognized until he stands up
    if (st.recognized && now - st.last_near >= LOST_MS) {
      st.recognized = false;
      changed = true;
      ESP_LOGD(TAG, LT("%s out of reach"), ob(this->user_label_(u)));
      if (this->recognized_user_ == u) this->recognized_user_ = -1;
    }
  }
  if (changed) {
    // another user that is still near takes over
    for (uint8_t u = 0; u < MAX_USERS && this->recognized_user_ < 0; u++)
      if (this->ustate_[u].recognized) this->recognized_user_ = u;
    this->publish_users_();
  }

  // end of the device search: keep the strongest, restart so Home Assistant gets the new options
  if (this->search_until_ != 0 && static_cast<int32_t>(now - this->search_until_) >= 0) {
    this->search_until_ = 0;
    std::sort(this->search_found_.begin(), this->search_found_.end(),
              [](const std::pair<DeviceId, int> &a, const std::pair<DeviceId, int> &b) { return a.second > b.second; });
    FoundStore fs{};
    fs.magic = FOUND_MAGIC;
    for (auto &f : this->search_found_) {
      if (fs.count >= MAX_FOUND) break;
      fs.devices[fs.count++] = f.first;
      ESP_LOGI(TAG, LT("Found %s (%d dBm)"), ob(f.first.label), f.second);
    }
    this->found_pref_.save(&fs);
    global_preferences->sync();
    ESP_LOGI(TAG, LT("%u devices found, strongest first; restarting so Home Assistant shows them"), fs.count);
    this->search_found_.clear();
    this->reboot_at_ = now + 1500;
  }
}

}  // namespace esphome::geberit_aquaclean

#endif
