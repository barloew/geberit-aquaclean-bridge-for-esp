#include "log_text.h"
#include "hub.h"
#include "remote_link.h"

#ifdef USE_ESP32

#include <cinttypes>
#include <cmath>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_system.h>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::geberit_aquaclean {

static const char *const TAG = "geberit_aquaclean";
static constexpr uint32_t STATS_INTERVAL = 3600000;  // statistics: once an hour, and after each visit

// GATT of the toilet
static const char *const SERVICE_UUID = "3334429d-90f3-4c41-a02d-5cb3a03e0000";
static const char *const WRITE0_UUID = "3334429d-90f3-4c41-a02d-5cb3a13e0000";
static const char *const WRITE1_UUID = "3334429d-90f3-4c41-a02d-5cb3a23e0000";
static const char *const NOTIFY_UUIDS[4] = {
    "3334429d-90f3-4c41-a02d-5cb3a53e0000",
    "3334429d-90f3-4c41-a02d-5cb3a63e0000",
    "3334429d-90f3-4c41-a02d-5cb3a73e0000",
    "3334429d-90f3-4c41-a02d-5cb3a83e0000",
};

static const uint8_t FRAME_LEN = 20;
static const uint8_t FRAME_PAYLOAD = 19;
static const uint8_t PROC_STATUS = 0x0D;
static const uint8_t PROC_COMMAND = 0x09;
static const uint8_t MSG_ID_REQUEST = 0x04;
static const uint8_t MSG_ID_RESPONSE = 0x05;
static const uint8_t MSG_ID_NOTIFY = 0x06;

static const uint32_t CLOCK_SYNC_INTERVAL = 24UL * 60 * 60 * 1000;  // set the toilet clock once a day
static const uint32_t SESSION_IDLE_REFRESH = 60000;                 // session settings while nobody is seated
static const uint8_t MAX_KIND_TIMEOUTS = 3;
// Recovery ladder (step 3)
static const uint8_t MAX_RECOVERY_REBOOTS = 2;         // gated reboots per bad episode before giving up
static const uint32_t RECOVERY_REBOOT_MS = 300000;     // 5 min continuously unhealthy before a recovery reboot
static const uint32_t RECOVERY_FLAP_WINDOW_MS = 600000;  // 10 min reconnect-flap window
static const uint8_t RECOVERY_FLAP_THRESHOLD = 5;      // reconnects within the window that count as flapping
static const uint32_t RECOVERY_HEALTHY_CLEAR_MS = 600000;  // 10 min healthy clears the episode
static const uint32_t RECOVERY_NO_CONNECT_MS = 300000;     // no connection this boot after a recovery reboot
static const uint8_t SCAN_IDS = 0x20;  // settings scan: ids 0x00-0x1f per kind
// settings scan: the four setting kinds plus the active values of common settings (0a <id>, written with 0b)
static const uint8_t SCAN_PROC[5] = {0x07, 0x53, 0x51, 0x57, 0x0A};
static const char *const SCAN_NAMES[5] = {"session", "default", "common", "schedule", "active"};
// list request as the app sends it for 0d (status) and 59 (counters): ids 00-0b
// Counter ids: 00-07 maintenance counters, 0b the user's local time (set by the app or the ESP), 11 end of the
// trial period, 56 / 5a remaining trial period.
static const std::vector<uint8_t> LIST_00_0B = {15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x11, 0x56, 0x5A};
// status ids beyond the poll list (64 connected modules, 68-6a lid / shower arm / dryer arm offset)
static const std::vector<uint8_t> STATUS_EXTRA = {4, 0x64, 0x68, 0x69, 0x6A};

static const uint8_t READ_PROC[KIND_COUNT] = {0x07, 0x53, 0x51, 0x57};
static const uint8_t WRITE_PROC[KIND_COUNT] = {0x08, 0x54, 0x52, 0x58};
static const char *const KIND_NAMES[KIND_COUNT] = {"session", "default", "common", "schedule"};

// Node information the app reads on every connect, four nodes per request: 11 = node info, 13 = firmware SAP
// number (answer per node: <id> + 12 characters, e.g. "000.000.00.0"). Without them the toilet sometimes
// ignores the status request.
static const uint8_t NODE_INFO_11[4][5] = {
    {0x04, 0x01, 0x03, 0x04, 0x05},
    {0x04, 0x06, 0x07, 0x08, 0x09},
    {0x04, 0x0a, 0x0b, 0x0c, 0x0e},
    {0x01, 0x0f, 0x00, 0x00, 0x00},
};
static const uint8_t FW_SAP_13[4][5] = {
    {0x04, 0x01, 0x03, 0x04, 0x05},
    {0x04, 0x06, 0x07, 0x08, 0x09},
    {0x04, 0x0a, 0x0b, 0x0c, 0x0e},
    {0x02, 0x0f, 0x0d, 0x00, 0x00},
};

static std::string to_hex(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(len * 2);
  char b[3];
  for (size_t i = 0; i < len; i++) {
    snprintf(b, sizeof(b), "%02x", data[i]);
    out += b;
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::setup() {
  if (this->time_ != nullptr)
    this->time_->add_on_time_sync_callback([this]() {
      if (!this->time_synced_) this->time_synced_at_ = millis();
      this->time_synced_ = true;
    });
  this->status_.fill(-1);
  if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(false);
  if (this->app_connected_sensor_ != nullptr) this->app_connected_sensor_->publish_state(false);
  if (this->timeouts_sensor_ != nullptr) this->timeouts_sensor_->publish_state(0);
  if (this->reconnects_sensor_ != nullptr) this->reconnects_sensor_->publish_state(0);

  // Language: strictly the one from the YAML (set at compile time)
  this->apply_language_();

  // Feature discovery: show only the entities of the features the toilet has (decided at boot, before the
  // API lists the entities; set_internal() cannot change later)
  this->profile_pref_ = global_preferences->make_preference<Profile>(fnv1_hash("geberit_aquaclean_profile"));
  // Recovery breadcrumb: how many recovery reboots have happened this bad episode (persisted across reboots)
  this->recovery_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("geberit_aquaclean_recovery"));
  if (!this->recovery_pref_.load(&this->recovery_reboots_))
    this->recovery_reboots_ = 0;
  if (this->recovery_reboots_ != 0)
    ESP_LOGW(TAG, LT("Recovery: resumed after a recovery reboot (%u this episode)"), this->recovery_reboots_);
  this->users_setup_();
  this->apply_profile_();
  this->maintenance_setup_();  // after the feature discovery: hides the maintenance entities unless switched on

  // Switches: restore their state and apply it
  for (auto *sw : this->switches_) {
    if (sw->action() == SW_MAINTENANCE) {
      this->maint_switch_ = sw;
      sw->set_internal(!this->maint_active_);
      sw->publish_state(this->maint_active_);
      continue;
    }
    bool on = sw->get_initial_state_with_restore_mode().value_or(false);
    if (sw->action() == SW_PAIRING) on = false;
    this->switch_action(sw, sw->action(), on);
  }
  this->switches_restored_ = true;  // from here a SW_HOMEKIT toggle is a user action, not a restore
}

void AquaCleanHub::apply_language_() {
  if (this->language_ < LANGUAGE_COUNT)
    set_log_texts(this->log_tables_[this->language_], this->log_counts_[this->language_]);
  for (auto *sel : this->selects_) sel->apply_language(this->language_);
  for (auto &e : this->status_enums_) this->publish_status_enum_(e.second);
  this->sela_light_publish_();
}

void AquaCleanHub::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Geberit AquaClean:\n"
                "  Poll interval: %" PRIu32
                " ms\n"
                "  Response timeout: %" PRIu32
                " ms\n"
                "  Session refresh: %" PRIu32
                " ms\n"
                "  Settings refresh: %" PRIu32
                " ms\n"
                "  Set clock: %s\n"
                "  Settings: %u\n"
                "  Language index: %u",
                this->poll_interval_, this->response_timeout_, this->session_refresh_, this->settings_refresh_,
                YESNO(this->set_clock_ && this->time_ != nullptr), static_cast<unsigned>(this->settings_.size()),
                this->language_);
#ifdef USE_AQUACLEAN_HOMEKIT
  this->homekit_dump_config_();
#endif
}

// ---------------------------------------------------------------------------------------------------
// GATT events
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT: {
      if (this->was_connected_) {
        ESP_LOGW(TAG, LT("Connection to the toilet lost"));
        this->was_connected_ = false;
      }
      this->reset_session_();
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto service = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
      auto *w0 = this->parent()->get_characteristic(service, espbt::ESPBTUUID::from_raw(WRITE0_UUID));
      auto *w1 = this->parent()->get_characteristic(service, espbt::ESPBTUUID::from_raw(WRITE1_UUID));
      this->write0_handle_ = w0 != nullptr ? w0->handle : 0;
      this->write1_handle_ = w1 != nullptr ? w1->handle : 0;
      if (this->write0_handle_ == 0 || this->write1_handle_ == 0) {
        ESP_LOGE(TAG, LT("Write characteristics of the toilet not found (other model?)"));
        this->status_set_warning();
        break;
      }
      this->pending_notify_regs_ = 0;
      for (int i = 0; i < 4; i++) {
        auto *chr = this->parent()->get_characteristic(service, espbt::ESPBTUUID::from_raw(NOTIFY_UUIDS[i]));
        if (chr == nullptr) {
          ESP_LOGE(TAG, LT("Notify characteristic %d not found"), i);
          this->notify_handles_[i] = 0;
          continue;
        }
        this->notify_handles_[i] = chr->handle;
        if (this->parent()->register_for_notify(chr->handle) == ESP_OK) this->pending_notify_regs_++;
      }
      ESP_LOGD(TAG, LT("Service found, %u notifications requested"), this->pending_notify_regs_);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (this->pending_notify_regs_ > 0) this->pending_notify_regs_--;
      if (this->pending_notify_regs_ == 0 && this->node_state != espbt::ClientState::ESTABLISHED) {
        this->node_state = espbt::ClientState::ESTABLISHED;
        this->established_at_ = millis();
        this->was_connected_ = true;
        this->status_clear_warning();
        ESP_LOGI(TAG, LT("Connected to the toilet"));
        if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(true);
      }
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      for (uint8_t i = 0; i < 4; i++) {
        const uint16_t h = this->notify_handles_[i];
        if (h != 0 && param->notify.handle == h) {
          if (this->app_owned_()) {
            // Answer to a request of the app: forward unchanged (the app acknowledges itself)
            this->app_owner_until_ = millis() + APP_HOLD_MS;
            if (this->app_sink_) this->app_sink_(i, param->notify.value, param->notify.value_len);
          }
          this->on_frame_(param->notify.value, param->notify.value_len);
          break;
        }
      }
      break;
    }

    default:
      break;
  }
}

void AquaCleanHub::reset_session_() {
  bool had_session = this->session_started_;
  this->node_state = espbt::ClientState::IDLE;
  this->write0_handle_ = 0;
  this->write1_handle_ = 0;
  this->notify_handles_.fill(0);
  this->pending_notify_regs_ = 0;
  this->session_started_ = false;
  this->queue_.clear();
  this->waiting_ = false;
  this->waiting_req_ = Request{};
  this->rx_expected_ = 0;
  this->rx_mask_ = 0;
  this->consecutive_timeouts_ = 0;
  this->kind_timeouts_.fill(0);
  this->kind_disabled_.fill(false);
  this->last_healthy_rx_valid_ = false;  // no connection: liveness unknown
  this->app_tx_.clear();
  this->app_owner_until_ = 0;
  if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(false);
  if (this->uptime_sensor_ != nullptr) this->uptime_sensor_->publish_state("");  // not connected: unknown
  this->uptime_published_ = false;
  if (had_session) {
    this->reconnects_++;
    if (this->reconnects_sensor_ != nullptr) this->reconnects_sensor_->publish_state(this->reconnects_);
    this->record_reconnect_(millis());  // recovery ladder: track flapping
  }
}

// ---------------------------------------------------------------------------------------------------
// Main loop: one request at a time; status and settings are requested periodically
// ---------------------------------------------------------------------------------------------------

static const char *const BANNER[] = {
    "  ▌▌▌▌▌▌▌▌▌▌      ██████╗ ███████╗██████╗ ███████╗██████╗ ██╗████████╗",
    "▬ ██████████ ▬   ██╔════╝ ██╔════╝██╔══██╗██╔════╝██╔══██╗██║╚══██╔══╝",
    "▬ ██ESP32███ ▬   ██║  ███╗█████╗  ██████╔╝█████╗  ██████╔╝██║   ██║",
    "▬ ██████████ ▬   ██║   ██║██╔══╝  ██╔══██╗██╔══╝  ██╔══██╗██║   ██║",
    "▬ ██████████ ▬   ╚██████╔╝███████╗██████╔╝███████╗██║  ██║██║   ██║",
    "  ▘▘▘▘▘▘▘▘▘▘      ╚═════╝ ╚══════╝╚═════╝ ╚══════╝╚═╝  ╚═╝╚═╝   ╚═╝",
    "──────────────────────────────────────────────────────────────────────",
    "▞▀▖         ▞▀▖▜             ▛▀▖   ▗   ▌         ▗▀▖         ▛▀▘▞▀▖▛▀▖",
    "▙▄▌▞▀▌▌ ▌▝▀▖▌  ▐ ▞▀▖▝▀▖▛▀▖   ▙▄▘▙▀▖▄ ▞▀▌▞▀▌▞▀▖   ▐  ▞▀▖▙▀▖   ▙▄ ▚▄ ▙▄▘",
    "▌ ▌▚▄▌▌ ▌▞▀▌▌ ▖▐ ▛▀ ▞▀▌▌ ▌   ▌ ▌▌  ▐ ▌ ▌▚▄▌▛▀    ▜▀ ▌ ▌▌     ▌  ▖ ▌▌",
    "▘ ▘  ▌▝▀▘▝▀▘▝▀  ▘▝▀▘▝▀▘▘ ▘   ▀▀ ▘  ▀▘▝▀▘▗▄▘▝▀▘   ▐  ▝▀ ▘     ▀▀▘▝▀ ▘",
};

void AquaCleanHub::loop() {
  const uint32_t now = millis();
  // Banner, just before safe mode reports "Boot seems successful" (60 s after its start, a little after boot)
  if (!this->banner_shown_ && now >= 59500) {
    this->banner_shown_ = true;
    for (const char *line : BANNER) ESP_LOGI(TAG, LT("%s"), line);
  }
  this->users_loop_(now);
  this->maintenance_loop_(now);
#ifdef USE_AQUACLEAN_HOMEKIT
  this->homekit_loop_(now);
#endif
  // Signal strength of the toilet link, every minute
  if (this->signal_sensor_ != nullptr && this->is_ready() && now - this->last_rssi_read_ >= 60000) {
    this->last_rssi_read_ = now;
    esp_ble_gap_read_rssi(this->parent()->get_remote_bda());
  }

  // New feature set: restart once nobody uses the toilet or the app, so Home Assistant lists the entities again
  if (this->restart_pending_ && !this->is_seated() && !this->app_connected_ && this->status_[1] != 1 &&
      this->status_[2] != 1) {
    this->restart_pending_ = false;
    if (this->restart_reason_ != nullptr)
      ESP_LOGI(TAG, "%s", this->restart_reason_);
    else
      ESP_LOGI(TAG, LT("Feature set changed, restarting in 2 s"));
    this->reboot_at_ = now + 2000;
  }
  if (this->reboot_at_ != 0 && static_cast<int32_t>(now - this->reboot_at_) >= 0) {
    this->reboot_at_ = 0;
    // Final line before the reboot: the ~2 s delay above let it reach the API/serial log (a guaranteed
    // persisted log is the on-device portal's job on the 250 branch).
    ESP_LOGW(TAG, LT("Rebooting now (bridge health: %s)"), health_name_(this->health_));
    App.safe_reboot();
  }
  // Keep the pairing switch in step with the pairing window of the link
  if (this->link_ != nullptr) {
    for (auto *sw : this->switches_) {
      if (sw->action() == SW_PAIRING && sw->state != this->link_->is_pairing())
        sw->publish_state(this->link_->is_pairing());
    }
  }
  if (this->discovery_ && this->procedures_dirty_ && now - this->last_procedures_publish_ >= 10000)
    this->publish_procedures_();

  this->recovery_loop_(now);   // health evaluation + escalation ladder (also runs while the link is down)
  this->resource_loop_(now);   // memory sampling + unified health verdict (also runs while the link is down)

  if (!this->is_ready()) return;

  // Wait a moment after connecting, like the app does
  if (!this->session_started_) {
    if (now - this->established_at_ < 300) return;
    this->start_session_();
  }

  // "Connected since": the moment of connecting, once, as soon as the clock is known
  if (this->uptime_sensor_ != nullptr && !this->uptime_published_ && this->time_ != nullptr) {
    auto t = this->time_->utcnow();
    if (t.is_valid()) {
      this->uptime_published_ = true;
      const time_t since = t.timestamp - static_cast<time_t>((now - this->established_at_) / 1000);
      this->uptime_sensor_->publish_state(ESPTime::from_epoch_utc(since).strftime("%Y-%m-%dT%H:%M:%SZ"));
    }
  }

  if (this->waiting_) {
    if (now - this->sent_at_ < this->response_timeout_)
      return;  // frames of the app wait in app_tx_ until our answer is in
    this->handle_timeout_();
    if (!this->is_ready()) return;
  }

  // App via the ESP: its frames go to the toilet unchanged. While an app transaction runs we send nothing
  // ourselves (the toilet only knows one conversation partner).
  if (!this->app_tx_.empty()) {
    bool last_is_ack = false;
    while (!this->app_tx_.empty()) {
      const auto &f = this->app_tx_.front();
      const uint16_t h = f.first == 0 ? this->write0_handle_ : f.first == 1 ? this->write1_handle_ : 0;
      last_is_ack = ((f.second[0] >> 5) & 0x07) == 3;  // control frame: the app acknowledges an answer
      if (h != 0) this->write_frame_(h, f.second.data());
      this->app_tx_.pop_front();
    }
    this->app_owner_until_ = now + APP_HOLD_MS;
    // The app polls the status several times per second and is never quiet for long. Our own requests
    // (commands from Home Assistant, 0b writes for the light) therefore go right after the app has
    // acknowledged an answer: its transaction is complete then. Its next frames wait ~0.1 s for our answer.
    if (last_is_ack && !this->queue_.empty()) {
      this->app_owner_until_ = 0;  // the answers that follow are ours
      Request req = this->queue_.front();
      this->queue_.pop_front();
      this->send_request_(req);
    }
    return;
  }
  if (this->app_owned_()) return;

  // Liveness probe. While the app is connected the hub stays silent (it only reads along), so a toilet that
  // wedges mid app-session would go unnoticed: our own polling -- the only thing that drives the
  // consecutive-timeout reconnect -- is suppressed. If no frame at all has arrived for liveness_timeout_, inject
  // one status poll in a quiet gap. A healthy app session keeps last_healthy_rx_ fresh, so this never fires then;
  // only a genuinely silent toilet reaches it, and the injected poll's timeout then drives normal recovery.
  if (this->liveness_timeout_ != 0 && this->app_connected_ && this->last_healthy_rx_valid_ &&
      static_cast<int32_t>(now - this->last_healthy_rx_) > static_cast<int32_t>(this->liveness_timeout_) &&
      this->queue_.empty() && this->app_tx_.empty() &&
      static_cast<int32_t>(now - this->last_liveness_probe_) > static_cast<int32_t>(2 * this->response_timeout_)) {
    this->last_liveness_probe_ = now;
    ESP_LOGW(TAG, LT("Toilet silent for %" PRIu32 " s during app session, probing"),
             (now - this->last_healthy_rx_) / 1000);
    std::vector<uint8_t> args = {12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    this->enqueue_(0x01, PROC_STATUS, args, true);
  }

  // Only plan new work when the queue is empty, so nothing piles up. With the app connected we only read
  // along: no periodic reads of our own, they would slow the app down.
  if (this->queue_.empty() && !this->app_connected_) {
    if (this->clock_check_pending_ && this->time_synced_ && !this->clock_check_requested_) {
      // time zone known now: read the toilet clock as it was before the ESP touches it
      this->clock_check_requested_ = true;
      this->clock_check_since_ = now;
      this->enqueue_(0x01, 0x59, {1, 0x0B});
    }
    if (this->clock_check_pending_ && this->clock_check_requested_ && now - this->clock_check_since_ > 30000)
      this->clock_check_pending_ = false;  // no answer with the clock: set it anyway
    if (this->set_clock_ && this->time_ != nullptr && this->time_synced_ && !this->clock_check_pending_ &&
        (!this->clock_synced_ || now - this->last_clock_sync_ >= CLOCK_SYNC_INTERVAL))
      this->sync_clock_();

    const uint32_t session_interval = this->is_seated() ? this->session_refresh_ : SESSION_IDLE_REFRESH;
    if (this->session_read_due_ || now - this->last_session_read_ >= session_interval) {
      this->session_read_due_ = false;
      this->last_session_read_ = now;
      this->enqueue_reads_(true);
    }
    if (this->stats_due_at_ != 0 && static_cast<int32_t>(now - this->stats_due_at_) >= 0) {
      this->stats_due_at_ = 0;
      this->stats_read_due_ = true;
    }
    if (this->stats_read_due_ || now - this->last_stats_read_ >= STATS_INTERVAL) {
      this->stats_read_due_ = false;
      this->last_stats_read_ = now;
      this->read_stats_();
    }
    if (this->settings_read_due_ || now - this->last_settings_read_ >= this->settings_refresh_) {
      this->settings_read_due_ = false;
      this->last_settings_read_ = now;
      this->enqueue_reads_(false);
      this->enqueue_counters_();  // counters
      // device identification still missing (a time-out right after connecting): ask again
      if (this->info_sensors_[0] != nullptr && !this->info_sensors_[0]->has_state()) {
        this->enqueue_(0x00, 0x82, {});
        this->enqueue_(0x00, 0x86, {});
        this->enqueue_(0x01, 0x0E, {0x02, 0x01, 0x0F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
      }
    }
  }

  if (now - this->last_poll_ >= this->poll_interval_ && !this->app_connected_) {
    bool poll_queued = false;
    for (auto &r : this->queue_) poll_queued |= r.is_poll;
    if (!poll_queued) {
      std::vector<uint8_t> args = {12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
      this->enqueue_(0x01, PROC_STATUS, args, true);
      this->last_poll_ = now;
    }
  }

  if (!this->queue_.empty()) {
    Request req = this->queue_.front();
    this->queue_.pop_front();
    this->send_request_(req);
  }
}

void AquaCleanHub::handle_timeout_() {
  const Request req = this->waiting_req_;
  this->waiting_ = false;
  // A single missed answer is common (radio shared with Wi-Fi and the other links): ask again once, at the
  // front of the queue, before calling it a timeout. A missed status poll is simply repeated by the next poll.
  if (req.is_poll && this->consecutive_timeouts_ == 0) {
    this->consecutive_timeouts_ = 1;
    ESP_LOGV(TAG, LT("Status poll unanswered, next poll follows"));
    return;
  }
  if (!req.retried && !req.is_poll && req.scan_index < 0 && req.probe_index < 0) {
    Request again = req;
    again.retried = true;
    this->queue_.push_front(again);
    ESP_LOGD(TAG, LT("No answer to proc 0x%02X, asking again"), req.procedure);
    return;
  }
  this->timeouts_++;
  if (this->timeouts_sensor_ != nullptr) this->timeouts_sensor_->publish_state(this->timeouts_);

  if (req.scan_index >= 0) {
    this->scan_result_(req.scan_index, nullptr, -1);
    return;
  }
  if (req.probe_index >= 0) {
    this->probe_result_(req.probe_index, -1);
    return;
  }
  // Device identification right after connecting sometimes gets no answer: try again (3 times)
  if ((req.procedure == 0x82 || req.procedure == 0x86 || req.procedure == 0x0E) && this->info_retries_ < 3) {
    this->info_retries_++;
    ESP_LOGD(TAG, LT("Asking proc %02x again"), req.procedure);
    this->enqueue_(req.context, req.procedure, req.args);
    return;
  }
  if (req.procedure == 0x82) this->on_article_("");
  if (req.setting != nullptr) {
    // A setting that does not answer must not drop the connection
    const SettingKind kind = req.setting->kind();
    ESP_LOGW(TAG, LT("No answer reading %s (proc 0x%02X)"), req.setting->setting_name(), req.procedure);
    if (++this->kind_timeouts_[kind] >= MAX_KIND_TIMEOUTS && !this->kind_disabled_[kind]) {
      this->kind_disabled_[kind] = true;
      ESP_LOGE(TAG, LT("Toilet does not answer %s settings (proc 0x%02X); reading stopped until the next connection"),
               KIND_NAMES[kind], req.procedure);
    }
    return;
  }

  this->consecutive_timeouts_++;
  ESP_LOGW(TAG, LT("No answer to procedure 0x%02X (%u in a row)"), req.procedure, this->consecutive_timeouts_);
  if (this->consecutive_timeouts_ >= 5) {
    ESP_LOGE(TAG, LT("Toilet stopped answering, reconnecting"));
    this->parent()->disconnect();
  }
}

void AquaCleanHub::record_reconnect_(uint32_t now) {
  this->reconnect_times_[this->reconnect_idx_] = now != 0 ? now : 1;  // 0 means "empty slot"
  this->reconnect_idx_ = (this->reconnect_idx_ + 1) % this->reconnect_times_.size();
}

uint8_t AquaCleanHub::recent_reconnects_(uint32_t now, uint32_t window) const {
  uint8_t c = 0;
  for (uint32_t t : this->reconnect_times_)
    if (t != 0 && static_cast<int32_t>(now - t) <= static_cast<int32_t>(window)) c++;
  return c;
}

void AquaCleanHub::recovery_loop_(uint32_t now) {
  if (!this->recovery_enabled_)
    return;

  // Health: connected and the toilet has produced a decoded frame within the liveness window.
  const uint32_t stale = this->liveness_timeout_ != 0 ? this->liveness_timeout_ : 90000;
  const bool healthy = this->is_ready() && this->last_healthy_rx_valid_ &&
                       static_cast<int32_t>(now - this->last_healthy_rx_) < static_cast<int32_t>(stale);

  // Never connected yet this boot: do not evaluate -- a missing or unconfigured toilet must never cause a
  // reboot loop. The one exception is after a recovery reboot that still cannot reach the toilet at all.
  if (!this->ever_connected_) {
    if (this->recovery_reboots_ > 0 && now > RECOVERY_NO_CONNECT_MS && !this->needs_power_cycle_) {
      this->needs_power_cycle_ = true;
      ESP_LOGE(TAG, LT("Recovery: no connection to the toilet since a recovery reboot -- power-cycle the toilet"));
    }
    return;
  }

  if (healthy) {
    this->unhealthy_since_ = 0;
    if (this->healthy_since_ == 0)
      this->healthy_since_ = now;
    // A sustained healthy period ends the bad episode: clear the persisted reboot count and any alert.
    if (static_cast<int32_t>(now - this->healthy_since_) >= static_cast<int32_t>(RECOVERY_HEALTHY_CLEAR_MS)) {
      if (this->recovery_reboots_ != 0) {
        this->recovery_reboots_ = 0;
        this->recovery_pref_.save(&this->recovery_reboots_);
        global_preferences->sync();
        ESP_LOGI(TAG, LT("Recovery: healthy again, episode cleared"));
      }
      if (this->needs_power_cycle_) {
        this->needs_power_cycle_ = false;
        ESP_LOGI(TAG, LT("Recovery: toilet responding again"));
      }
    }
    return;
  }

  // Unhealthy.
  this->healthy_since_ = 0;
  if (this->unhealthy_since_ == 0)
    this->unhealthy_since_ = now;
  const uint32_t bad_for = now - this->unhealthy_since_;
  const bool flapping = this->recent_reconnects_(now, RECOVERY_FLAP_WINDOW_MS) >= RECOVERY_FLAP_THRESHOLD;
  const bool stuck = bad_for >= RECOVERY_REBOOT_MS;
  const bool can_reboot = this->recovery_reboot_enabled_ && this->recovery_reboots_ < MAX_RECOVERY_REBOOTS;

  // Escalate to a gated recovery reboot -- but never while the app holds a session, and not if another reboot
  // is already scheduled. The persisted counter bounds this to MAX_RECOVERY_REBOOTS per episode.
  if ((flapping || stuck) && can_reboot && this->reboot_at_ == 0 && !this->app_connected_) {
    this->recovery_reboots_++;
    this->recovery_pref_.save(&this->recovery_reboots_);
    global_preferences->sync();
    ESP_LOGE(TAG, LT("Recovery reboot %u/%u (%s); rebooting to clear a wedged link"), this->recovery_reboots_,
             MAX_RECOVERY_REBOOTS, flapping ? LT("reconnect flapping") : LT("toilet unreachable"));
    this->restart_reason_ = nullptr;  // the line above is the reason
    this->reboot_at_ = now + 2000;
    return;
  }

  // Cannot or will not reboot and still unhealthy for a long time: raise the sticky power-cycle alert.
  if (bad_for >= RECOVERY_REBOOT_MS && !this->needs_power_cycle_ &&
      (!this->recovery_reboot_enabled_ || this->recovery_reboots_ >= MAX_RECOVERY_REBOOTS)) {
    this->needs_power_cycle_ = true;
    ESP_LOGE(TAG, LT("Recovery exhausted; the toilet seems wedged -- power-cycle the toilet"));
  }
}

void AquaCleanHub::resource_loop_(uint32_t now) {
  if (this->last_resource_check_ == 0 || now - this->last_resource_check_ >= 30000) {
    this->last_resource_check_ = now;
    this->free_heap_ = esp_get_free_heap_size();
    this->min_free_heap_ = esp_get_minimum_free_heap_size();
    this->largest_block_ = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    ESP_LOGD(TAG, LT("Resources: free heap %u, lowest %u, largest block %u, uptime %u s"),
             static_cast<unsigned>(this->free_heap_), static_cast<unsigned>(this->min_free_heap_),
             static_cast<unsigned>(this->largest_block_), static_cast<unsigned>(now / 1000));
    if (this->free_heap_ < this->heap_floor_ || this->largest_block_ < this->block_floor_)
      ESP_LOGW(TAG, LT("Low memory: free heap %u, largest block %u"), static_cast<unsigned>(this->free_heap_),
               static_cast<unsigned>(this->largest_block_));
    if (this->free_heap_sensor_ != nullptr) this->free_heap_sensor_->publish_state(this->free_heap_);
    if (this->min_free_heap_sensor_ != nullptr) this->min_free_heap_sensor_->publish_state(this->min_free_heap_);
    if (this->largest_free_block_sensor_ != nullptr) this->largest_free_block_sensor_->publish_state(this->largest_block_);
  }
  this->publish_health_(now);
}

AquaCleanHub::Health AquaCleanHub::compute_health_(uint32_t now) const {
  if (this->needs_power_cycle_)
    return Health::NEEDS_POWER_CYCLE;
  const uint32_t stale = this->liveness_timeout_ != 0 ? this->liveness_timeout_ : 90000;
  const bool link_unhealthy =
      this->ever_connected_ && (!this->is_ready() || !this->last_healthy_rx_valid_ ||
                                static_cast<int32_t>(now - this->last_healthy_rx_) >= static_cast<int32_t>(stale));
  if (link_unhealthy || this->recovery_reboots_ > 0 ||
      this->recent_reconnects_(now, RECOVERY_FLAP_WINDOW_MS) >= RECOVERY_FLAP_THRESHOLD)
    return Health::RECOVERING;
  const bool low_mem = (this->free_heap_ != 0 && this->free_heap_ < this->heap_floor_) ||
                       (this->largest_block_ != 0 && this->largest_block_ < this->block_floor_);
  return low_mem ? Health::DEGRADED : Health::OK;
}

void AquaCleanHub::publish_health_(uint32_t now) {
  const Health h = this->compute_health_(now);
  this->health_ = h;
  if (h != this->health_logged_) {
    this->health_logged_ = h;
    ESP_LOGI(TAG, LT("Bridge health: %s"), health_name_(h));
    // The visible entity shows a localized state via the status-enum texts (options.bridge_health).
    this->publish_status_(BRIDGE_HEALTH_IDX, static_cast<int64_t>(h));
  }
}

const char *AquaCleanHub::health_name_(Health h) {
  switch (h) {
    case Health::OK:
      return "ok";
    case Health::DEGRADED:
      return "degraded";
    case Health::RECOVERING:
      return "recovering";
    case Health::NEEDS_POWER_CYCLE:
      return "needs power-cycle";
  }
  return "ok";
}

void AquaCleanHub::start_session_() {
  this->session_started_ = true;
  // Fresh connection: treat the toilet as alive so the liveness probe does not fire before any traffic.
  this->last_healthy_rx_ = millis();
  this->last_healthy_rx_valid_ = true;
  this->last_liveness_probe_ = 0;
  this->ever_connected_ = true;  // recovery evaluation only runs after the first successful connection
  this->status_.fill(-1);
  for (auto *s : this->settings_) {
    s->forget();
    s->bad_reads = 0;
  }
  for (auto &p : NODE_INFO_11) this->enqueue_(0x01, 0x11, std::vector<uint8_t>(p, p + 5));
  for (auto &p : FW_SAP_13) this->enqueue_(0x01, 0x13, std::vector<uint8_t>(p, p + 5));
  // Read the toilet clock before setting it, so the log shows what it was after a restart or reconnect
  // (it was back in UTC every time so far); read_device_info() reads the counter list with it.
  this->clock_synced_ = false;
  this->clock_check_pending_ = true;
  this->clock_check_requested_ = false;
  this->clock_check_since_ = millis();
  this->session_read_due_ = true;
  this->settings_read_due_ = true;
  this->stats_read_due_ = true;
  this->info_retries_ = 0;
  this->read_device_info();
  if (!this->profile_valid_) this->start_feature_discovery_();
}

void AquaCleanHub::sync_clock_() {
  if (!this->time_synced_) return;  // time zone not known yet: the local time would be UTC
  auto t = this->time_->utcnow();
  if (!t.is_valid()) return;  // no time yet; try again next round
  this->last_clock_sync_ = millis();
  this->clock_synced_ = true;
  // The toilet keeps local time: the app sends local seconds since 1970
  uint32_t ts = static_cast<uint32_t>(t.timestamp + ESPTime::timezone_offset());
  std::vector<uint8_t> args = {0x0b, static_cast<uint8_t>(ts), static_cast<uint8_t>(ts >> 8),
                               static_cast<uint8_t>(ts >> 16), static_cast<uint8_t>(ts >> 24)};
  this->enqueue_(0x01, 0x58, args);
  ESP_LOGI(TAG, LT("Setting the toilet clock (local time)"));
}

// ---------------------------------------------------------------------------------------------------
// Reading and writing settings
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::enqueue_read_(SettingEntity *s) {
  if (s->kind() >= KIND_COUNT || this->kind_disabled_[s->kind()] || s->hidden) return;
  Request req{};
  req.context = 0x01;
  req.procedure = READ_PROC[s->kind()];
  if (s->kind() == KIND_DEFAULT)
    req.args = {s->profile(), s->setting_id()};
  else
    req.args = {s->setting_id()};
  req.setting = s;
  this->enqueue_(std::move(req));
}

void AquaCleanHub::enqueue_reads_(bool session_kind) {
  for (auto *s : this->settings_) {
    if ((s->kind() == KIND_SESSION) == session_kind) this->enqueue_read_(s);
  }
}

void AquaCleanHub::refresh_settings() {
  for (auto *s : this->settings_) {
    s->forget();
    s->bad_reads = 0;
  }
  this->kind_timeouts_.fill(0);
  this->kind_disabled_.fill(false);
  this->session_read_due_ = true;
  this->settings_read_due_ = true;
  ESP_LOGI(TAG, LT("Reading all settings again"));
}

SettingEntity *AquaCleanHub::find_setting(SettingKind kind, uint8_t id, uint8_t profile) {
  for (auto *s : this->settings_) {
    if (s->kind() == kind && s->setting_id() == id && s->profile() == profile) return s;
  }
  return nullptr;
}

void AquaCleanHub::write_setting(SettingEntity *s, uint16_t value) {
  if (s->kind() == KIND_LOCAL) {
    // setting of a Home Assistant profile: kept on the ESP
    const uint8_t u = s->profile();
    int16_t *slot = u >= 1 && u <= MAX_USERS ? this->user_value_(u - 1, s->setting_id()) : nullptr;
    if (slot == nullptr || value > s->max_value()) {
      s->republish();
      return;
    }
    *slot = static_cast<int16_t>(value);
    this->save_user_(u - 1);
    s->update_value(value);
    return;
  }
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected, %s not written"), s->setting_name());
    s->republish();
    return;
  }
  if (value > s->max_value()) {
    ESP_LOGW(TAG, LT("%s: value %u out of range (max %u)"), s->setting_name(), value, s->max_value());
    s->republish();
    return;
  }
  if (this->sela_light_write_(s, value)) return;
  const SettingKind kind = s->kind();
  const uint8_t id = s->setting_id();
  // Session settings may also be written before sitting down: the app does so with the values of its user
  // profile, the toilet keeps them for the next session.

  const uint8_t lo = static_cast<uint8_t>(value);
  const uint8_t hi = static_cast<uint8_t>(value >> 8);
  switch (kind) {
    case KIND_SESSION:
      this->enqueue_(0x01, WRITE_PROC[kind], {id, lo, hi});
      break;
    case KIND_DEFAULT:
      this->enqueue_(0x01, WRITE_PROC[kind], {s->profile(), id, lo, hi});
      break;
    case KIND_COMMON:
      this->enqueue_(0x01, WRITE_PROC[kind], {id, lo, hi});
      // Also as 0b <id> <value>: the layer the light really uses. 52 alone does not change color and
      // brightness of the light until it switches on again.
      this->enqueue_(0x01, 0x0B, {id, lo, hi});
      break;
    case KIND_SCHEDULE:
      this->enqueue_(0x01, WRITE_PROC[kind], {id, lo, hi, 0x00, 0x00});
      break;
    case KIND_LOCAL:
      break;  // handled above
  }
  ESP_LOGI(TAG, LT("%s -> %u"), s->setting_name(), value);
  s->update_value(value);  // show at once, then confirm by reading back
  this->enqueue_read_(s);
}

void AquaCleanHub::on_setting_changed(SettingEntity *s) {
  if (this->light_ != nullptr && s->kind() == KIND_COMMON &&
      (s->setting_id() == COMMON_LIGHT_BRIGHTNESS || s->setting_id() == COMMON_LIGHT_COLOR))
    this->light_->sync_from_toilet();
  if ((s->kind() == KIND_COMMON && s->setting_id() == COMMON_LIGHT_ACTIVATION) ||
      (s->kind() == KIND_SCHEDULE && s->setting_id() >= 0x0C && s->setting_id() <= 0x0F))
    this->sela_light_publish_();
}

// The Sela app has four light modes; the toilet knows three values. Seen in the app:
//   off            = 0, time windows off       on        = 1, time windows off
//   automatic      = 2, time windows off       time-controlled = 2 with time windows (app default 06:00-08:00
//                                                                and 21:00-23:00)
// The select shows "time-controlled" for 2 with a time window set, and writes like the app does.
SettingSelect *AquaCleanHub::sela_light_select_() {
  auto *s = this->find_setting(KIND_COMMON, COMMON_LIGHT_ACTIVATION);
  for (auto &v : this->select_variants_)
    if (v.select == s)
      return v.select->alt() ? nullptr : v.select;  // the other variant (Mera Comfort) has its own values
  return nullptr;
}

void AquaCleanHub::sela_light_publish_() {
  auto *sel = this->sela_light_select_();
  if (sel == nullptr || sel->value() < 0) return;
  bool windows = false;
  for (uint8_t id = 0x0C; id <= 0x0F; id++) {
    auto *w = this->find_setting(KIND_SCHEDULE, id);
    if (w != nullptr && w->value() >= 0 && w->value() != SCHEDULE_OFF) windows = true;
  }
  const size_t index = sel->value() == 2 && windows ? 3 : static_cast<size_t>(sel->value());
  if (sel->has_index(index) && sel->active_index().value_or(SIZE_MAX) != index) sel->publish_state(index);
}

bool AquaCleanHub::sela_light_write_(SettingEntity *s, uint16_t value) {
  auto *sel = this->sela_light_select_();
  if (sel == nullptr || static_cast<SettingEntity *>(sel) != s) return false;
  const uint16_t mode = value == 3 ? 2 : value;
  this->enqueue_(0x01, 0x52, {COMMON_LIGHT_ACTIVATION, static_cast<uint8_t>(mode), 0x00});
  this->enqueue_(0x01, 0x0B, {COMMON_LIGHT_ACTIVATION, static_cast<uint8_t>(mode), 0x00});
  // time windows: kept (or the app's defaults) for time-controlled, off for the other modes
  static const uint16_t DEFAULTS[4] = {360, 480, 1260, 1380};
  bool keep = false;
  if (value == 3)
    for (uint8_t id = 0x0C; id <= 0x0F; id++) {
      auto *w = this->find_setting(KIND_SCHEDULE, id);
      if (w != nullptr && w->value() >= 0 && w->value() != SCHEDULE_OFF) keep = true;
    }
  for (uint8_t id = 0x0C; id <= 0x0F; id++) {
    auto *w = this->find_setting(KIND_SCHEDULE, id);
    if (value == 3 && keep) continue;
    const uint16_t v = value == 3 ? DEFAULTS[id - 0x0C] : SCHEDULE_OFF;
    this->enqueue_(0x01, WRITE_PROC[KIND_SCHEDULE],
                   {id, static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), 0x00, 0x00});
    if (w != nullptr) w->update_value(v);
  }
  ESP_LOGI(TAG, LT("%s -> %u"), s->setting_name(), value);
  s->update_value(mode);
  this->sela_light_publish_();
  this->enqueue_read_(s);
  return true;
}

void AquaCleanHub::handle_setting_read_(SettingEntity *s, const uint8_t *args, uint8_t len) {
  this->kind_timeouts_[s->kind()] = 0;
  if (len < 1) {
    ESP_LOGW(TAG, LT("%s: empty answer"), s->setting_name());
    return;
  }
  // Value little endian, 2 bytes. Schedules (57) answer <id> <value LE32>, e.g. 0c ffff0000 = window 1 start off.
  uint16_t v;
  if (s->kind() == KIND_SCHEDULE && len >= 3 && args[0] == s->setting_id()) {
    v = args[1] | (static_cast<uint16_t>(args[2]) << 8);
  } else {
    v = args[0];
    if (len >= 2) v |= static_cast<uint16_t>(args[1]) << 8;
  }
  const bool schedule_off = s->kind() == KIND_SCHEDULE && v == SCHEDULE_OFF;
  if (v > s->max_value() && !schedule_off) {
    // Unexpected layout: do not publish, report a few times so it can be looked into
    if (s->bad_reads < 3) {
      s->bad_reads++;
      ESP_LOGW(TAG, LT("%s: unexpected answer %s (%u bytes, proc 0x%02X)"), s->setting_name(),
               to_hex(args, len).c_str(), len, READ_PROC[s->kind()]);
    }
    return;
  }
  if (s->value() != v) ESP_LOGD(TAG, LT("%s = %u (raw %s)"), s->setting_name(), v, to_hex(args, len).c_str());
  s->update_value(v);
}

// ---------------------------------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::enqueue_(Request req) {
  // Commands and settings go before the status poll
  if (!req.is_poll && !this->queue_.empty() && this->queue_.back().is_poll)
    this->queue_.insert(this->queue_.end() - 1, std::move(req));
  else
    this->queue_.push_back(std::move(req));
}

void AquaCleanHub::enqueue_urgent_(uint8_t context, uint8_t procedure, std::vector<uint8_t> args) {
  // Before every queued read (a settings refresh can hold ~100 requests), after earlier urgent ones
  Request req{};
  req.context = context;
  req.procedure = procedure;
  req.args = std::move(args);
  req.urgent = true;
  auto it = this->queue_.begin();
  while (it != this->queue_.end() && it->urgent) ++it;
  this->queue_.insert(it, std::move(req));
}

void AquaCleanHub::enqueue_(uint8_t context, uint8_t procedure, std::vector<uint8_t> args, bool is_poll) {
  Request req{};
  req.context = context;
  req.procedure = procedure;
  req.args = std::move(args);
  req.is_poll = is_poll;
  this->enqueue_(std::move(req));
}

void AquaCleanHub::send_request_(const Request &req) {
  if (this->write0_handle_ == 0 || this->write1_handle_ == 0) return;

  // body: <node 01> <context> <procedure> <len> <args>
  std::vector<uint8_t> body = {0x01, req.context, req.procedure, static_cast<uint8_t>(req.args.size())};
  body.insert(body.end(), req.args.begin(), req.args.end());
  if (body.size() > 32) {
    ESP_LOGE(TAG, LT("Request too long (%zu bytes)"), body.size());
    return;
  }

  // message: <04> <ff> <len16 BE> <crc16 BE> <body>
  uint16_t crc = crc16_(body.data(), body.size());
  std::vector<uint8_t> msg = {MSG_ID_REQUEST,
                              0xFF,
                              0x00,
                              static_cast<uint8_t>(body.size()),
                              static_cast<uint8_t>(crc >> 8),
                              static_cast<uint8_t>(crc & 0xFF)};
  msg.insert(msg.end(), body.begin(), body.end());

  uint8_t frame[FRAME_LEN];
  if (msg.size() <= FRAME_PAYLOAD) {
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x11;  // single frame
    memcpy(frame + 1, msg.data(), msg.size());
    this->write_frame_(this->write0_handle_, frame);
  } else {
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x13;  // first of two
    memcpy(frame + 1, msg.data(), FRAME_PAYLOAD);
    this->write_frame_(this->write0_handle_, frame);
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x12;  // second, to the second write characteristic
    memcpy(frame + 1, msg.data() + FRAME_PAYLOAD, msg.size() - FRAME_PAYLOAD);
    this->write_frame_(this->write1_handle_, frame);
  }

  this->waiting_ = true;
  this->waiting_req_ = req;
  this->sent_at_ = millis();
  if (!req.is_poll) {
    ESP_LOGV(TAG, LT("Sent: proc 0x%02X args %s"), req.procedure, to_hex(req.args.data(), req.args.size()).c_str());
  }
}

void AquaCleanHub::write_frame_(uint16_t handle, const uint8_t *data) {
  uint8_t buf[FRAME_LEN];
  memcpy(buf, data, FRAME_LEN);
  auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), handle, FRAME_LEN,
                                      buf, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) ESP_LOGW(TAG, LT("Write failed: %d"), err);
}

void AquaCleanHub::send_ack_() {
  // Control frame: 0x70, error 0, max. unacknowledged frames 8, latency 0, bitmap
  uint8_t frame[FRAME_LEN] = {0};
  frame[0] = 0x70;
  frame[2] = 0x08;
  frame[4] = this->rx_mask_;
  if (this->write0_handle_ != 0) this->write_frame_(this->write0_handle_, frame);
}

// ---------------------------------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::on_frame_(const uint8_t *data, uint16_t len) {
  if (len != FRAME_LEN) {
    ESP_LOGV(TAG, LT("Frame with length %u ignored"), len);
    return;
  }
  const uint8_t header = data[0];
  const uint8_t type = (header >> 5) & 0x07;

  if (type == 3) {  // control frame of the toilet: acknowledges a request
    ESP_LOGV(TAG, LT("Control frame: %s"), to_hex(data, 12).c_str());
    return;
  }
  if (type == 4) {  // info frame
    ESP_LOGV(TAG, LT("Info frame: %s"), to_hex(data, FRAME_LEN).c_str());
    return;
  }
  if (type == 1 || type == 2) {
    this->on_large_frame_(data, type);
    return;
  }
  if (type != 0) {
    ESP_LOGW(TAG, LT("Unexpected frame type %u: %s"), type, to_hex(data, FRAME_LEN).c_str());
    return;
  }

  const uint8_t count_or_index = (header >> 1) & 0x03;
  const bool is_count = header & 0x01;
  uint8_t index;
  if (is_count) {
    this->rx_expected_ = count_or_index + 1;
    this->rx_mask_ = 0;
    index = 0;
  } else {
    index = count_or_index;
    if (this->rx_expected_ == 0) {
      ESP_LOGV(TAG, LT("Continuation frame without start ignored"));
      return;
    }
  }
  memcpy(this->rx_frames_[index].data(), data + 1, FRAME_PAYLOAD);
  this->rx_mask_ |= (1 << index);

  const uint8_t full = (1 << this->rx_expected_) - 1;
  if ((this->rx_mask_ & full) != full) return;

  if (!this->app_owned_()) this->send_ack_();  // during an app transaction the app acknowledges itself
  std::vector<uint8_t> msg;
  msg.reserve(this->rx_expected_ * FRAME_PAYLOAD);
  for (uint8_t i = 0; i < this->rx_expected_; i++)
    msg.insert(msg.end(), this->rx_frames_[i].begin(), this->rx_frames_[i].end());
  this->rx_expected_ = 0;
  this->rx_mask_ = 0;
  this->on_message_(msg);
}

void AquaCleanHub::on_large_frame_(const uint8_t *data, uint8_t type) {
  // Large answer (e.g. device info 82): first frame 0x30 <frame count> <18 bytes>, then 0x5x/0x4x <sequence>
  // <18 bytes>. The receiver acknowledges with 60 00 08 00 <bitmap> after every 4 frames and at the end.
  if (type == 1) {
    this->lrx_total_ = data[1] > LARGE_MAX ? LARGE_MAX : data[1];
    this->lrx_mask_ = 0;
    memcpy(this->lrx_frames_[0].data(), data + 2, 18);
    this->lrx_mask_ |= 1;
    if (data[1] > LARGE_MAX) ESP_LOGW(TAG, LT("Large answer with %u frames, only %u kept"), data[1], LARGE_MAX);
  } else {
    const uint8_t seq = data[1];
    if (this->lrx_total_ == 0 || seq >= this->lrx_total_) return;
    memcpy(this->lrx_frames_[seq].data(), data + 2, 18);
    this->lrx_mask_ |= (1 << seq);
  }
  const uint16_t full = (1u << this->lrx_total_) - 1;
  uint8_t received = 0;
  for (uint8_t i = 0; i < this->lrx_total_; i++) received += (this->lrx_mask_ >> i) & 1;
  const bool complete = (this->lrx_mask_ & full) == full;
  if (!this->app_owned_() && (complete || received % 4 == 0)) this->send_large_ack_();
  if (!complete) return;
  std::vector<uint8_t> msg;
  msg.reserve(this->lrx_total_ * 18);
  for (uint8_t i = 0; i < this->lrx_total_; i++)
    msg.insert(msg.end(), this->lrx_frames_[i].begin(), this->lrx_frames_[i].end());
  this->lrx_total_ = 0;
  this->lrx_mask_ = 0;
  this->on_message_(msg);
}

void AquaCleanHub::send_large_ack_() {
  uint8_t frame[FRAME_LEN] = {0};
  frame[0] = 0x60;
  frame[2] = 0x08;
  frame[4] = static_cast<uint8_t>(this->lrx_mask_);
  frame[5] = static_cast<uint8_t>(this->lrx_mask_ >> 8);
  if (this->write0_handle_ != 0) this->write_frame_(this->write0_handle_, frame);
}

static std::string printable_fields(const uint8_t *data, uint8_t len) {
  // text fields separated by NULs, e.g. "146.22x.xx.1 | MB2507EU... | DD.MM.YYYY | ..."
  std::string out, cur;
  for (uint8_t i = 0; i <= len; i++) {
    const char c = i < len ? static_cast<char>(data[i]) : 0;
    if (c >= 0x20 && c < 0x7f) {
      cur += c;
      continue;
    }
    if (cur.size() >= 2) {
      if (!out.empty()) out += " | ";
      out += cur;
    }
    cur.clear();
  }
  return out;
}

void AquaCleanHub::handle_device_message_(uint8_t procedure, const uint8_t *args, uint8_t len) {
  const std::string hex = to_hex(args, len);
  const std::string text = printable_fields(args, len);
  // Log at INFO only when the content changed (the app asks some of these every second). In the 59 counter
  // list entry 0b is the toilet clock: it changes every second and is left out of the comparison.
  std::string cmp = hex;
  if (procedure == 0x59 && len >= 1) {
    for (uint16_t i = 1; i + 5 <= len && (i - 1) / 5 < args[0]; i += 5)
      if (args[i] == 0x0b)
        for (uint16_t k = (i + 1) * 2; k < (i + 5) * 2 && k < cmp.size(); k++) cmp[k] = '.';
  }
  auto &last = this->device_raw_[procedure];
  const bool changed = last != cmp;
  last = cmp;
  if (changed) {
    // proc 82 holds the serial number: masked with log obfuscation
    const bool secret = procedure == 0x82 && log_obfuscate();
    ESP_LOGI(TAG, LT("Device data proc %02x: %s%s%s"), procedure, secret ? ob(hex) : hex.c_str(),
             text.empty() ? LT("") : LT(" = "), secret ? ob(text) : text.c_str());
  } else {
    ESP_LOGV(TAG, LT("Device data proc %02x: %s"), procedure, procedure == 0x82 ? ob(hex) : hex.c_str());
  }
  if (procedure == 0x82) {
    // fixed fields: article number 12, serial number 20, production date 10, device type 40
    static const uint8_t FIELDS[4][2] = {{0, 12}, {12, 20}, {32, 10}, {42, 40}};
    for (uint8_t i = 0; i < 4; i++) {
      if (FIELDS[i][0] >= len) break;
      const std::string v = printable_fields(args + FIELDS[i][0], std::min<int>(FIELDS[i][1], len - FIELDS[i][0]));
      if (this->info_sensors_[i] != nullptr && !v.empty()) this->info_sensors_[i]->publish_state(v);
      if (i == 0) this->on_article_(v);
    }
    return;
  }
  if (procedure == 0x86) {
    // commissioning date as text, e.g. "DD.MM.YYYY" (the app shows it as "Bedrijfstelling")
    if (this->commissioning_sensor_ != nullptr && !text.empty()) this->commissioning_sensor_->publish_state(text);
    return;
  }
  if (procedure == 0x59) {
    // counter list: <count> (<id> <LE32>)*
    std::string out;
    if (len >= 1) {
      for (uint8_t i = 0; i < args[0] && 1 + i * 5 + 5 <= len; i++) {
        const uint8_t *e = args + 1 + i * 5;
        const uint32_t v = e[1] | (e[2] << 8) | (e[3] << 16) | (static_cast<uint32_t>(e[4]) << 24);
        this->counter_ids_seen_.insert(e[0]);
        auto cv = this->counter_values_.find(e[0]);
        if (cv != this->counter_values_.end()) {
          // 0 kind: number; 1: UTC timestamp (0 = never); 2: signed number
          auto *sens = cv->second.sensor;
          float f = cv->second.kind == 2 ? static_cast<float>(static_cast<int32_t>(v)) : static_cast<float>(v);
          if (cv->second.kind == 1 && v == 0) f = NAN;
          if (!sens->has_state() || (sens->state != f && !(std::isnan(f) && std::isnan(sens->state))))
            sens->publish_state(f);
        }
        if (e[0] == 0x07) {
          // days until the odor extraction filter must be replaced (Mera / Tuma; the Sela does not answer 07)
          if (this->filter_sensor_ != nullptr &&
              (!this->filter_sensor_->has_state() || this->filter_sensor_->state != v))
            this->filter_sensor_->publish_state(v);
        }
        if (e[0] == 0x0B) {
          // toilet clock (the user's local time): set it again when it is off by more than two minutes
          if (this->set_clock_ && this->time_ != nullptr && this->time_synced_) {
            auto t = this->time_->utcnow();
            if (t.is_valid()) {
              const int32_t diff =
                  static_cast<int32_t>(v - static_cast<uint32_t>(t.timestamp + ESPTime::timezone_offset()));
              if (this->clock_check_pending_) {
                // first read after connecting, before the ESP sets it
                this->clock_check_pending_ = false;
                if (diff <= 120 && diff >= -120) {
                  this->clock_synced_ = true;  // still right: no need to write
                  this->last_clock_sync_ = millis();
                }
              } else if (diff > 120 || diff < -120) {
                if (this->clock_synced_) ESP_LOGI(TAG, LT("Toilet clock off by %d s"), static_cast<int>(diff));
                this->clock_synced_ = false;
              }
            }
          }
          continue;
        }
        this->counter_ids_seen_.insert(e[0]);
        if (e[0] == 0x01) this->counter_descaling_seen_ = true;
        // 00 = showers today: the toilet rolls over once a night; when it was above 0, counter 01
        // goes down by one (usage days)
        if (e[0] == 0x00 && this->shower_count_sensor_ != nullptr &&
            (!this->shower_count_sensor_->has_state() || this->shower_count_sensor_->state != v))
          this->shower_count_sensor_->publish_state(v);
        // 01 = days until the next descaling
        if (e[0] == 0x01 && this->descaling_sensor_ != nullptr &&
            (!this->descaling_sensor_->has_state() || this->descaling_sensor_->state != v))
          this->descaling_sensor_->publish_state(v);
        if (!out.empty()) out += ' ';
        out += str_sprintf("%02x=%" PRIu32, e[0], v);
      }
    }
    if (this->counters_sensor_ != nullptr &&
        (!this->counters_sensor_->has_state() || this->counters_sensor_->state != out))
      this->counters_sensor_->publish_state(out);
    return;
  }
  // Decoded device data
  std::string decoded;
  if (procedure == 0x0E && len >= 1) {
    // Firmware versions: <count> (<node> <char> <char> <build LE16>)*; node 1 = main controller.
    for (uint8_t i = 0; i < args[0] && 1 + i * 5 + 5 <= len; i++) {
      const uint8_t *r = args + 1 + i * 5;
      const std::string v = str_sprintf("RS%c%c TS%u", r[1], r[2], r[3]);
      if (!decoded.empty()) decoded += ", ";
      decoded += str_sprintf("%u: ", r[0]) + v;
      if (r[0] == 0x01 && this->firmware_sensor_ != nullptr) this->firmware_sensor_->publish_state(v);
    }
  } else if (procedure == 0x81 && len >= 3) {
    // Bluetooth node version: two characters + LE16, "10" 0x0012 = 10.18
    decoded = str_sprintf("%c%c.%u", args[0], args[1], args[2]);
  } else if (procedure == 0x45 && len >= 16) {
    // Descaling statistics (context 01): unposted showers, days until descaling, days until the shower is restricted,
    // showers until confirmation, time of last descaling, time of last prompt, number of descalings
    decoded = str_sprintf("showers %u, descale in %u d, restricted in %u d, confirm in %u, last %" PRIu32
                          ", prompt %" PRIu32 ", descalings %u",
                          args[0], args[1] | (args[2] << 8), args[3] | (args[4] << 8), args[5],
                          args[6] | (args[7] << 8) | (args[8] << 16) | (static_cast<uint32_t>(args[9]) << 24),
                          args[10] | (args[11] << 8) | (args[12] << 16) | (static_cast<uint32_t>(args[13]) << 24),
                          args[14] | (args[15] << 8));
    // The first four fields are counters 00-03 of list 59. Times are UTC seconds since 1970, 0 = never.
    const uint32_t last = args[6] | (args[7] << 8) | (args[8] << 16) | (static_cast<uint32_t>(args[9]) << 24);
    if (this->last_descaling_sensor_ != nullptr && last != 0)
      this->last_descaling_sensor_->publish_state(static_cast<float>(last));
    // descaling due in <days>, then <grace> days until the shower is restricted
    const uint16_t days = args[1] | (args[2] << 8);
    const uint16_t grace = args[3] | (args[4] << 8);
    if (this->restricted_sensor_ != nullptr &&
        (!this->restricted_sensor_->has_state() || this->restricted_sensor_->state != days + grace))
      this->restricted_sensor_->publish_state(days + grace);
    const uint16_t count = args[14] | (args[15] << 8);
    if (this->descaling_count_sensor_ != nullptr &&
        (!this->descaling_count_sensor_->has_state() || this->descaling_count_sensor_->state != count))
      this->descaling_count_sensor_->publish_state(count);
  } else if (procedure == 0x55 && len >= 1) {
    // GetDeviceRegistrationLevel: 0 not registered, 1 private, 2 public
    decoded = str_sprintf("registration %u", args[0]);
    this->publish_status_(0x100, args[0]);
  }
  if (!decoded.empty() && changed) ESP_LOGI(TAG, LT("Device data proc %02x decoded: %s"), procedure, decoded.c_str());
  this->device_data_[procedure] = !decoded.empty() ? decoded : (text.empty() ? hex : text);
  std::string out;
  for (auto &d : this->device_data_) {
    if (!out.empty()) out += " ; ";
    out += str_sprintf("%02x: ", d.first) + d.second;
  }
  if (this->device_data_sensor_ != nullptr)
    this->device_data_sensor_->publish_state(out.size() > 250 ? out.substr(0, 250) : out);
}

void AquaCleanHub::enqueue_counters_() {
  this->enqueue_(0x01, 0x59, LIST_00_0B);                                 // counter list
  if (this->discovery_) this->enqueue_(0x01, PROC_STATUS, STATUS_EXTRA);  // other models: modules and arm offsets
  if (this->status_enums_.count(0x100)) this->enqueue_(0x01, 0x55, {});   // registration level
  this->enqueue_(0x01, 0x45, {});  // descaling statistics: the same values in one answer
}

void AquaCleanHub::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event != ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT || this->signal_sensor_ == nullptr) return;
  if (!this->parent()->check_addr(param->read_rssi_cmpl.remote_addr)) return;
  if (param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS)
    this->signal_sensor_->publish_state(param->read_rssi_cmpl.rssi);
}

void AquaCleanHub::activate_profile(uint8_t profile) {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected, profile %u not activated"), profile);
    return;
  }
  uint8_t n = 0;
  const SettingKind kind = profile <= TOILET_PROFILES ? KIND_DEFAULT : KIND_LOCAL;
  for (auto *p : this->settings_) {
    if (p->kind() != kind || p->profile() != profile || p->hidden) continue;
    SettingEntity *s = this->find_setting(KIND_SESSION, p->setting_id());
    if (s == nullptr || s->hidden) continue;
    if (p->value() < 0) {
      ESP_LOGW(TAG, LT("Profile %u: %s not known yet"), profile, p->setting_name());
      continue;
    }
    this->write_setting(s, static_cast<uint16_t>(p->value()));
    n++;
  }
  ESP_LOGI(TAG, LT("Profile %u activated (%u settings)"), profile, n);
}

void AquaCleanHub::store_profile(uint8_t profile) {
  if (!this->is_ready() && profile <= TOILET_PROFILES) {
    ESP_LOGW(TAG, LT("Not connected, profile %u not stored"), profile);
    return;
  }
  uint8_t n = 0;
  const SettingKind kind = profile <= TOILET_PROFILES ? KIND_DEFAULT : KIND_LOCAL;
  for (auto *s : this->settings_) {
    if (s->kind() != KIND_SESSION || s->value() < 0 || s->hidden) continue;
    SettingEntity *p = this->find_setting(kind, s->setting_id(), profile);
    if (p == nullptr || p->hidden) continue;
    this->write_setting(p, static_cast<uint16_t>(s->value()));
    n++;
  }
  ESP_LOGI(TAG, LT("Session settings stored in profile %u (%u settings)"), profile, n);
}

void AquaCleanHub::read_device_info() {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected"));
    return;
  }
  // the requests the app sends after connecting
  this->enqueue_(0x00, 0x82, {});
  this->enqueue_(0x01, 0x81, {});
  this->enqueue_(0x01, 0x05, {});
  this->enqueue_(0x01, 0x0E, {0x02, 0x01, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  this->enqueue_(0x01, 0x13, {0x03, 0x01, 0x0f, 0x0d, 0x00});
  this->enqueue_counters_();
  this->enqueue_(0x00, 0x86, {});  // commissioning date: context 00, like 82
}

void AquaCleanHub::on_message_(const std::vector<uint8_t> &msg) {
  if (msg.size() < 6) return;
  const uint8_t id = msg[0];
  if (id != MSG_ID_RESPONSE && id != MSG_ID_NOTIFY) {
    ESP_LOGV(TAG, LT("Message with id %u ignored"), id);
    return;
  }
  const uint16_t body_len = (msg[2] << 8) | msg[3];
  if (body_len < 5 || 6u + body_len > msg.size()) {
    ESP_LOGW(TAG, LT("Message with invalid length %u"), body_len);
    return;
  }
  const uint8_t *body = msg.data() + 6;
  const uint16_t crc = (msg[4] << 8) | msg[5];
  if (crc16_(body, body_len) != crc) {
    ESP_LOGW(TAG, LT("CRC mismatch, message ignored"));
    return;
  }

  // Liveness: a valid decoded frame (ours, an app answer or a notification) proves the toilet is alive.
  this->last_healthy_rx_ = millis();
  this->last_healthy_rx_valid_ = true;

  const uint8_t procedure = body[3];
  uint8_t arg_len = body[4];
  if (5u + arg_len > body_len) arg_len = body_len - 5;
  const uint8_t *args = body + 5;

  // Answer to our own request?
  bool ours = false;
  SettingEntity *setting = nullptr;
  int16_t scan_index = -1;
  int16_t probe_index = -1;
  uint8_t req_arg0 = 0xFF;  // arguments of our request (statistics: which counter / timer and page)
  uint8_t req_arg1 = 0;
  if (this->waiting_ && procedure == this->waiting_req_.procedure) {
    ours = true;
    if (!this->waiting_req_.args.empty()) req_arg0 = this->waiting_req_.args[0];
    if (this->waiting_req_.args.size() > 1) req_arg1 = this->waiting_req_.args[1];
    this->waiting_ = false;
    setting = this->waiting_req_.setting;
    scan_index = this->waiting_req_.scan_index;
    probe_index = this->waiting_req_.probe_index;
    if (setting == nullptr) this->consecutive_timeouts_ = 0;
  }

  if (this->discovery_)
    ESP_LOGI(TAG, LT("%s proc %02x: %s%s"), id == MSG_ID_NOTIFY ? LT("Notify") : LT("Answer"), procedure,
             to_hex(args, arg_len).c_str(), ours ? LT("") : LT(" (app)"));

  if (procedure == PROC_STATUS) {
    this->handle_status_(args, arg_len);
    return;
  }
  if (ours && (procedure == 0x49 || procedure == 0x4A || procedure == 0x4B)) {
    // answer to a free request (raw_request action): always in the log and in "Last response"
    const bool raw = procedure == this->raw_proc_ && millis() - this->raw_at_ < 10000;
    if (this->discovery_ || raw) this->log_statistic_(procedure, req_arg0, args, arg_len);
    if (raw && this->last_response_sensor_ != nullptr)
      this->last_response_sensor_->publish_state(
          str_sprintf("proc %02x: %s", procedure, to_hex(args, arg_len).c_str()));
    this->handle_statistic_(procedure, req_arg0, req_arg1, args, arg_len);
    return;
  }
  if (procedure == 0x82 || procedure == 0x81 || procedure == 0x05 || procedure == 0x0E || procedure == 0x13 ||
      procedure == 0x59 || procedure == 0x55 || procedure == 0x86 || procedure == 0x45) {
    this->handle_device_message_(procedure, args, arg_len);
    return;
  }
  if (scan_index >= 0) {
    this->scan_result_(scan_index, args, arg_len);
    return;
  }
  if (probe_index >= 0) {
    this->probe_result_(probe_index, arg_len);
    return;
  }
  if (setting != nullptr) {
    this->handle_setting_read_(setting, args, arg_len);
    return;
  }
  if (!ours && this->snoop_app_response_(procedure, args, arg_len)) return;

  // Write confirmations are only logged, not shown as "last response"
  const bool write_ack = procedure == 0x08 || procedure == 0x54 || procedure == 0x52 || procedure == 0x58 ||
                         procedure == 0x0B || procedure == PROC_COMMAND;
  if (write_ack || !ours) return;
  std::string text = str_sprintf("proc %02x: %s", procedure, to_hex(args, arg_len).c_str());
  ESP_LOGD(TAG, LT("%s %s"), id == MSG_ID_NOTIFY ? LT("Notify") : LT("Answer"), text.c_str());
  if (this->last_response_sensor_ != nullptr) this->last_response_sensor_->publish_state(text);
}

// Statistics (context 01, as the Geberit Home app reads them): 49 <counter> -> LE32, 4a <histogram> -> 5 x LE32
// (levels 1-5), 4b <timer> 00 -> LE16 values. Names from the app's enums.
static const char *const STAT_COUNTERS[] = {"uptime total",
                                            "uptime run",
                                            "uptime standby",
                                            "shower total",
                                            "shower control panel",
                                            "shower remote control",
                                            "lady shower total",
                                            "lady shower control panel",
                                            "lady shower remote control",
                                            "instant heater only",
                                            "oscillation shower",
                                            "oscillation lady shower",
                                            "dryer",
                                            "odor extraction",
                                            "seat heating",
                                            "lid lifter",
                                            "descaling",
                                            "cleaning",
                                            "users",
                                            "system flush",
                                            "profile 1",
                                            "profile 2",
                                            "profile 3",
                                            "profile 4",
                                            "shower app",
                                            "lady shower app"};
static const char *const STAT_HISTOGRAMS[] = {
    "shower position",      "shower pressure",         "shower temperature", "lady shower position",
    "lady shower pressure", "lady shower temperature", "dryer temperature",  "seat heating temperature"};
static const char *const STAT_TIMERS[] = {"duration shower",       "duration lady shower",    "duration dryer",
                                          "duration seat heating", "duration user detection", "delay shower",
                                          "delay lady shower"};

void AquaCleanHub::read_statistics_() {
  ESP_LOGI(TAG, LT("Reading the statistics of the toilet"));
  this->timer_reads_.clear();
  this->stats_last_counter_ = -1;
  for (uint8_t i = 0; i < sizeof(STAT_COUNTERS) / sizeof(STAT_COUNTERS[0]); i++) this->enqueue_(0x01, 0x49, {i});
  for (uint8_t i = 0; i < sizeof(STAT_HISTOGRAMS) / sizeof(STAT_HISTOGRAMS[0]); i++) this->enqueue_(0x01, 0x4A, {i});
  for (uint8_t i = 0; i < sizeof(STAT_TIMERS) / sizeof(STAT_TIMERS[0]); i++) this->enqueue_(0x01, 0x4B, {i, 0x00});
}

// Timer -> counter with its number of entries (one entry per use, the oldest first)
static int8_t timer_counter(uint8_t timer) {
  switch (timer) {
    case 0:
      return 3;  // shower total
    case 1:
      return 6;  // lady shower total
    case 4:
      return 18;  // users
    default:
      return -1;
  }
}

void AquaCleanHub::read_stats_() {
  uint32_t need = 0;
  for (auto &st : this->stat_sensors_) {
    if (st.s->is_internal()) continue;
    const int8_t c = st.kind <= 1 ? st.index : timer_counter(st.index);
    if (c >= 0 && c < 26) need |= 1u << c;
  }
  if (need == 0) return;
  this->timer_reads_.clear();
  this->stats_last_counter_ = -1;
  for (uint8_t i = 0; i < 26; i++)
    if (need & (1u << i)) {
      this->enqueue_(0x01, 0x49, {i});
      this->stats_last_counter_ = i;
    }
}

// The counters are in: read the page of each timer with its newest entry (and the page before it for the average)
void AquaCleanHub::start_timer_reads_() {
  this->timer_reads_.clear();
  for (uint8_t t : {0, 1, 4}) {
    bool want_last = false, want_avg = false;
    for (auto &st : this->stat_sensors_)
      if (st.index == t && !st.s->is_internal()) {
        want_last |= st.kind == 2;
        want_avg |= st.kind == 3;
      }
    const int8_t c = timer_counter(t);
    if ((!want_last && !want_avg) || !(this->stat_valid_ & (1u << c)) || this->stat_counter_[c] == 0) continue;
    const uint32_t newest = this->stat_counter_[c] - 1;
    if (newest / 25 > 254) continue;
    TimerRead tr{};
    tr.timer = t;
    tr.page = newest / 25;
    tr.idx = newest % 25;
    tr.need_prev = want_avg && tr.page > 0 && tr.idx < 24;
    tr.want_avg = want_avg;
    this->timer_reads_.push_back(tr);
    if (tr.need_prev) this->enqueue_(0x01, 0x4B, {t, static_cast<uint8_t>(tr.page - 1)});
    this->enqueue_(0x01, 0x4B, {t, tr.page});
  }
}

void AquaCleanHub::handle_statistic_(uint8_t procedure, uint8_t arg0, uint8_t arg1, const uint8_t *args, uint8_t len) {
  if (procedure == 0x49) {
    if (len < 4 || arg0 >= 26) return;
    const uint32_t v = args[0] | (args[1] << 8) | (args[2] << 16) | (static_cast<uint32_t>(args[3]) << 24);
    this->stat_counter_[arg0] = v;
    this->stat_valid_ |= 1u << arg0;
    for (auto &st : this->stat_sensors_) {
      if (st.index != arg0 || st.kind > 1) continue;
      const float value = st.kind == 1 ? v / 3600.0f : static_cast<float>(v);
      if (!st.s->has_state() || st.s->state != value) st.s->publish_state(value);
    }
    if (arg0 == this->stats_last_counter_) {
      this->stats_last_counter_ = -1;
      this->start_timer_reads_();
    }
    return;
  }
  if (procedure != 0x4B) return;
  for (size_t n = 0; n < this->timer_reads_.size(); n++) {
    auto &tr = this->timer_reads_[n];
    if (tr.timer != arg0) continue;
    if (len < 50) {
      // No such page: the toilet keeps fewer entries than its counter (seen with user detection: 317 visits,
      // page 10 and 12 empty). Go back page by page to the last one with entries.
      if (arg1 == tr.page && !tr.got_last) {
        if (tr.page == 0) {
          this->timer_reads_.erase(this->timer_reads_.begin() + n);
          return;
        }
        tr.page--;
        tr.scan = true;
        tr.need_prev = false;
        tr.got_prev = false;
        this->enqueue_(0x01, 0x4B, {tr.timer, tr.page});
      }
      return;  // an empty page before it (for the average): the page itself follows and decides
    }
    uint16_t *dst = nullptr;
    if (arg1 == tr.page) {
      dst = tr.last;
      tr.got_last = true;
    } else if (tr.need_prev && arg1 + 1 == tr.page) {
      dst = tr.prev;
      tr.got_prev = true;
    } else {
      return;
    }
    for (uint8_t i = 0; i < 25; i++) dst[i] = args[2 * i] | (args[2 * i + 1] << 8);
    if (tr.scan && dst == tr.last && !tr.need_prev) {
      // found the last page with entries: the newest is its last non-zero entry
      int last = 24;
      while (last > 0 && tr.last[last] == 0) last--;
      tr.idx = static_cast<uint8_t>(last);
      if (tr.want_avg && tr.page > 0 && tr.idx < 24) {
        tr.need_prev = true;
        this->enqueue_(0x01, 0x4B, {tr.timer, static_cast<uint8_t>(tr.page - 1)});
        return;
      }
    }
    if (!tr.got_last || (tr.need_prev && !tr.got_prev)) return;
    // Check: after the newest entry the page must be empty, otherwise the list does not follow the counter
    bool ok = true;
    for (uint8_t i = tr.idx + 1; i < 25; i++) ok &= tr.last[i] == 0;
    if (!ok) {
      ESP_LOGW(TAG, LT("Statistics timer %u does not match its counter: not shown"), tr.timer);
    } else {
      uint32_t sum = 0, count = 0;
      for (int i = 0; i < 25; i++) {
        // the 25 newest entries: the end of the page before, then this page up to the newest one
        const int pos = tr.idx - 24 + i;
        const uint16_t v = pos >= 0 ? tr.last[pos] : (tr.need_prev ? tr.prev[25 + pos] : 0);
        if (v > 0) {  // 0 = stopped right away: not counted
          sum += v;
          count++;
        }
      }
      for (auto &st : this->stat_sensors_) {
        if (st.index != tr.timer) continue;
        if (st.kind == 2) {
          // the newest entry; if that one is 0 (stopped right away, or not written yet), the newest one before it
          uint16_t last = 0;
          for (int i = 0; i < 25 && last == 0; i++) {
            const int pos = tr.idx - i;
            last = pos >= 0 ? tr.last[pos] : (tr.need_prev ? tr.prev[25 + pos] : 0);
          }
          st.s->publish_state(last);
        } else if (st.kind == 3 && count > 0)
          st.s->publish_state(roundf(static_cast<float>(sum) / count));
      }
    }
    this->timer_reads_.erase(this->timer_reads_.begin() + n);
    return;
  }
}

void AquaCleanHub::log_statistic_(uint8_t procedure, uint8_t index, const uint8_t *args, uint8_t len) {
  auto le32 = [&](uint8_t o) {
    return o + 4 <= len
               ? (args[o] | (args[o + 1] << 8) | (args[o + 2] << 16) | (static_cast<uint32_t>(args[o + 3]) << 24))
               : 0u;
  };
  if (procedure == 0x49) {
    const char *name = index < sizeof(STAT_COUNTERS) / sizeof(STAT_COUNTERS[0]) ? STAT_COUNTERS[index] : "?";
    ESP_LOGI(TAG, LT("Statistics counter %u (%s) = %u   [%s]"), index, name, (unsigned)le32(0),
             to_hex(args, len).c_str());
  } else if (procedure == 0x4A) {
    const char *name = index < sizeof(STAT_HISTOGRAMS) / sizeof(STAT_HISTOGRAMS[0]) ? STAT_HISTOGRAMS[index] : "?";
    ESP_LOGI(TAG, LT("Statistics histogram %u (%s): %u %u %u %u %u   [%s]"), index, name, (unsigned)le32(0),
             (unsigned)le32(4), (unsigned)le32(8), (unsigned)le32(12), (unsigned)le32(16), to_hex(args, len).c_str());
  } else {
    const char *name = index < sizeof(STAT_TIMERS) / sizeof(STAT_TIMERS[0]) ? STAT_TIMERS[index] : "?";
    ESP_LOGI(TAG, LT("Statistics timer %u (%s): [%s]"), index, name, to_hex(args, len).c_str());
  }
}

void AquaCleanHub::publish_status_(uint16_t idx, int64_t value) {
  auto fl = this->status_flags_.find(idx);
  if (fl != this->status_flags_.end()) fl->second->publish_state(value != 0);
  auto sv = this->status_values_.find(idx);
  if (sv != this->status_values_.end()) sv->second->publish_state(static_cast<float>(value));
  for (auto &se : this->status_enums_) {
    if (se.second.idx != idx) continue;
    se.second.value = value;
    this->publish_status_enum_(se.second);
  }
  // descaling running: read the counters when it ends (days until descaling, last descaling)
  if (idx == 4 && value == 0 && (this->status_seen_ & (1u << 4))) this->enqueue_counters_();
}

void AquaCleanHub::publish_status_enum_(StatusEnum &e) {
  if (e.sensor == nullptr || e.value < 0) return;
  const uint8_t lang = this->language_ < LANGUAGE_COUNT ? this->language_ : 0;
  const char *const *texts = e.texts[lang];
  uint8_t count = e.counts[lang];
  for (auto &v : e.variants)
    if ((this->model_caps_ & v.first) == v.first && v.second.texts[lang] != nullptr) {
      texts = v.second.texts[lang];
      count = v.second.counts[lang];
    }
  std::string text;
  if (e.error_map) {
    // value is a raw fault code (status index 6). 0 = no error; otherwise lead with the code (the
    // key a user gives Geberit support, shown as 4 hex digits like the app) and append the module in
    // brackets. texts: [0]=no error, [1]="error code" word, [2..17]=modules by high byte 0x00-0x0F
    // ("" for an unknown module -> brackets omitted).
    const uint16_t code = static_cast<uint16_t>(e.value & 0xFFFF);
    if (code == 0) {
      text = (texts != nullptr && count > 0) ? texts[0] : "OK";
    } else {
      const char *prefix = (texts != nullptr && count > 1) ? texts[1] : "Error code";
      const uint8_t mod = (code >> 8) & 0xFF;
      const uint8_t ti = static_cast<uint8_t>(2 + mod);
      const char *module = (mod <= 0x0F && texts != nullptr && ti < count) ? texts[ti] : "";
      if (module != nullptr && module[0] != '\0')
        text = str_sprintf("%s %04X (%s)", prefix, code, module);
      else
        text = str_sprintf("%s %04X", prefix, code);
    }
  } else if (texts != nullptr && e.value < count) {
    text = texts[e.value];
  } else {
    text = str_sprintf("%lld", static_cast<long long>(e.value));
  }
  if (!e.sensor->has_state() || e.sensor->state != text) e.sensor->publish_state(text);
}

void AquaCleanHub::handle_status_(const uint8_t *args, uint8_t len) {
  if (len < 1) return;
  const uint8_t count = args[0];
  const bool was_seated = this->is_seated();
  const int64_t old_shower = this->status_[1];
  const int64_t old_lady = this->status_[2];
  const int64_t old_light = this->status_[9];
  bool changed = false;
  for (uint8_t i = 0; i < count && 1 + i * 5 + 5 <= len; i++) {
    const uint8_t *e = args + 1 + i * 5;
    const uint8_t idx = e[0];
    const int64_t value = static_cast<int64_t>(e[1]) | (static_cast<int64_t>(e[2]) << 8) |
                          (static_cast<int64_t>(e[3]) << 16) | (static_cast<int64_t>(e[4]) << 24);
    if (idx >= this->status_.size()) {
      // ids beyond the poll list (64-6a): kept apart, published on change
      auto it = this->status_extra_.find(idx);
      if (it != this->status_extra_.end() && it->second == value) continue;
      this->status_extra_[idx] = value;
      this->publish_status_(idx, value);
      continue;
    }
    const bool seen = this->status_seen_ & (1u << idx);
    if (seen && this->status_[idx] == value) continue;
    this->status_seen_ |= 1u << idx;
    if (this->status_[idx] != value) changed = true;
    this->status_[idx] = value;
    this->publish_status_(idx, value);
  }
  if (!changed) return;

  // Start or end of a session: read the session settings again right away
  if (was_seated != this->is_seated() || old_shower != this->status_[1] || old_lady != this->status_[2])
    this->session_read_due_ = true;
  if (was_seated != this->is_seated()) this->users_on_seated_(this->is_seated());
  // The user has left: statistics again (visit duration, showers), a little later so the toilet has stored them
  if (was_seated && !this->is_seated()) this->stats_due_at_ = millis() + 5000;
  // A shower has ended: read the counters (00 = number of showers)
  if ((old_shower == 1 && this->status_[1] == 0) || (old_lady == 1 && this->status_[2] == 0)) this->enqueue_counters_();
  if (this->light_ != nullptr && old_light != this->status_[9]) this->light_->sync_from_toilet();

  std::string raw;
  for (size_t i = 0; i < this->status_.size(); i++) {
    if (i) raw += ' ';
    raw += str_sprintf("%zu:%lld", i, static_cast<long long>(this->status_[i]));
  }
  ESP_LOGD(TAG, LT("Status: %s"), raw.c_str());
  if (this->raw_status_sensor_ != nullptr) this->raw_status_sensor_->publish_state(raw);
}

// ---------------------------------------------------------------------------------------------------
// App via the ESP
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::app_frame_in(uint8_t write_index, const uint8_t *data, uint16_t len) {
  if (len == 0) return;
  std::array<uint8_t, 20> f{};
  memcpy(f.data(), data, len > 20 ? 20 : len);
  this->app_tx_.emplace_back(write_index, f);
  ESP_LOGV(TAG, LT("App -> toilet [%u]: %s"), write_index, to_hex(f.data(), 20).c_str());

  // Single-frame request: 11 04 ff 00 <len> <crc crc> 01 <ctx> <proc> <n> <args...>
  if (write_index != 0 || f[0] != 0x11 || f[1] != MSG_ID_REQUEST) return;
  const uint8_t proc = f[9];
  const uint8_t n = f[10] > 9 ? 9 : f[10];
  this->app_req_proc_ = proc;
  this->app_req_args_.assign(f.begin() + 11, f.begin() + 11 + n);
  if (this->discovery_) {
    this->app_procedures_[proc]++;
    this->procedures_dirty_ = true;
    if (proc != PROC_STATUS && proc != 0x59)
      ESP_LOGI(TAG, LT("App request: context %02x proc %02x args %s"), f[8], proc, to_hex(f.data() + 11, n).c_str());
  }
  this->snoop_app_write_(proc, f.data() + 11, n);

  // The app writes color and brightness only as 52 <id> <lo> <hi> (stored value); the light that is on
  // follows the 0b layer. Add 0b <id> <lo> <hi>, like we do ourselves; only the last step counts.
  if (proc == 0x52 && n >= 3 && (f[11] == COMMON_LIGHT_BRIGHTNESS || f[11] == COMMON_LIGHT_COLOR)) {
    for (auto it = this->queue_.begin(); it != this->queue_.end();) {
      if (it->procedure == 0x0B && it->args.size() == 3 && it->args[0] == f[11])
        it = this->queue_.erase(it);
      else
        ++it;
    }
    this->enqueue_(0x01, 0x0B, {f[11], f[12], f[13]});
  }
}

void AquaCleanHub::snoop_app_write_(uint8_t procedure, const uint8_t *args, uint8_t len) {
  // Changes made in the app appear in Home Assistant at once
  SettingEntity *s = nullptr;
  uint16_t v = 0;
  if (procedure == 0x08 && len >= 3) {
    s = this->find_setting(KIND_SESSION, args[0]);
    v = args[1] | (args[2] << 8);
  } else if (procedure == 0x54 && len >= 4) {
    s = this->find_setting(KIND_DEFAULT, args[1], args[0]);
    v = args[2] | (args[3] << 8);
  } else if ((procedure == 0x52 || procedure == 0x0B) && len >= 3) {  // stored and active common settings
    s = this->find_setting(KIND_COMMON, args[0]);
    v = args[1] | (args[2] << 8);
  } else if (procedure == 0x58 && len >= 3) {
    s = this->find_setting(KIND_SCHEDULE, args[0]);
    v = args[1] | (args[2] << 8);
  }
  if (s != nullptr) ESP_LOGI(TAG, LT("App sets %s -> %u (proc %02X)"), s->setting_name(), v, procedure);
  if (s != nullptr && (v <= s->max_value() || (s->kind() == KIND_SCHEDULE && v == SCHEDULE_OFF))) s->update_value(v);
}

bool AquaCleanHub::snoop_app_response_(uint8_t procedure, const uint8_t *args, uint8_t len) {
  // Answer to a setting read of the app: use it for Home Assistant too
  if (procedure != this->app_req_proc_ || this->app_req_args_.empty()) return false;
  for (uint8_t k = 0; k < KIND_COUNT; k++) {
    if (procedure != READ_PROC[k]) continue;
    uint8_t id, profile = 0;
    if (k == KIND_DEFAULT) {
      if (this->app_req_args_.size() < 2) return false;
      profile = this->app_req_args_[0];
      id = this->app_req_args_[1];
    } else {
      id = this->app_req_args_[0];
    }
    SettingEntity *s = this->find_setting(static_cast<SettingKind>(k), id, profile);
    if (s != nullptr) this->handle_setting_read_(s, args, len);
    return true;
  }
  return false;
}

void AquaCleanHub::set_app_connected(bool v) {
  this->app_connected_ = v;
  if (!v) {
    this->clock_synced_ = false;  // the app may have set the clock in another time zone: set it again
    this->app_tx_.clear();
    this->app_owner_until_ = 0;
    this->app_req_proc_ = 0;
    this->app_req_args_.clear();
  }
  if (this->app_connected_sensor_ != nullptr) this->app_connected_sensor_->publish_state(v);
  ESP_LOGI(TAG, LT("App %s"), v ? LT("connected via the ESP") : LT("disconnected"));
}

// ---------------------------------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::send_command(uint8_t cmd) {
  if (is_maintenance_command(cmd) && !this->maint_active_) {
    ESP_LOGW(TAG, LT("Command 0x%02X is a maintenance procedure: hold the maintenance key on the remote control first"),
             cmd);
    return;
  }
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected, command 0x%02X not sent"), cmd);
    return;
  }
  // Shower, Lady shower and dryer (toggles 00-02) only run while someone is seated: the toilet blocks the arms
  // otherwise and would ignore the command silently. Odor extraction and the other commands work at any time.
  if (cmd <= 0x02 && !this->is_seated()) {
    ESP_LOGW(TAG, LT("%s only works while someone is seated"), this->seated_names_[cmd]);
    return;
  }
  this->enqueue_(0x01, PROC_COMMAND, {cmd});
}

void AquaCleanHub::send_raw(uint8_t context, uint8_t procedure, const std::string &args_hex) {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected"));
    return;
  }
  std::vector<uint8_t> args;
  for (size_t i = 0; i + 1 < args_hex.size(); i += 2) {
    char byte[3] = {args_hex[i], args_hex[i + 1], 0};
    args.push_back(static_cast<uint8_t>(strtoul(byte, nullptr, 16)));
  }
  ESP_LOGI(TAG, LT("Raw request: context %02x proc %02x args %s"), context, procedure, args_hex.c_str());
  this->raw_proc_ = procedure;
  this->raw_at_ = millis();
  this->enqueue_(context, procedure, args);
}

void AquaCleanHub::step_session_(uint8_t id, int delta) {
  SettingEntity *s = this->find_setting(KIND_SESSION, id);
  if (s == nullptr || s->value() < 0) {
    ESP_LOGW(TAG, LT("Session setting 0x%02X unknown, button not forwarded"), id);
    return;
  }
  int v = s->value() + delta;
  if (v < 0 || v > s->max_value()) return;  // already at the limit
  this->write_setting(s, static_cast<uint16_t>(v));
}

void AquaCleanHub::remote_button(uint8_t code) {
  // Only used when the ESP is not (yet) paired with the toilet as remote control: the main buttons are
  // translated into app commands. With passthrough the toilet handles all buttons itself.
  if (!this->is_ready()) return;
  const bool lady = this->status_[2] == 1;  // while the Lady shower runs, spray and position apply to it
  switch (code) {
    case 0x01:
      this->send_command(0x00);
      break;
    case 0x06:
      this->send_command(0x01);
      break;
    case 0x13:
      this->send_command(0x14);
      break;
    case 0x02:
    case 0x03:
      this->step_session_(lady ? 0x03 : 0x02, code == 0x03 ? 1 : -1);
      break;
    case 0x04:
    case 0x05:
      this->step_session_(lady ? 0x05 : 0x04, code == 0x05 ? 1 : -1);
      break;
    case 0x12: {
      SettingEntity *s = this->find_setting(KIND_SESSION, 0x01);
      if (s != nullptr && s->value() >= 0) this->write_setting(s, s->value() ? 0 : 1);
      break;
    }
    default:
      break;
  }
}

void AquaCleanHub::button_action(uint8_t action) {
  if (action > BTN_ACTIVATE_PROFILE && action < BTN_ACTIVATE_PROFILE + 16) {
    this->activate_profile(action - BTN_ACTIVATE_PROFILE);
    return;
  }
  if (action > BTN_STORE_PROFILE && action < BTN_STORE_PROFILE + 16) {
    this->store_profile(action - BTN_STORE_PROFILE);
    return;
  }
  if (action > BTN_IRK_SHOW && action < BTN_IRK_SHOW + 16) {
    this->show_irk_(action - BTN_IRK_SHOW - 1);
    return;
  }
  if (action > BTN_IRK_CAPTURE && action < BTN_IRK_CAPTURE + 16) {
    if (this->link_ != nullptr) this->link_->start_irk_capture(action - BTN_IRK_CAPTURE, this->irk_android_);
    return;
  }
  if (action > BTN_DELETE_PROFILE && action < BTN_DELETE_PROFILE + 16) {
    this->delete_user_profile(action - BTN_DELETE_PROFILE);
    return;
  }
  switch (action) {
    case BTN_SHOWER:
      this->send_command(0x00);
      break;
    case BTN_LADY_SHOWER:
      this->send_command(0x01);
      break;
    case BTN_FILTER_RESET:
      this->send_command(0x2F);  // odor extraction filter replaced: days until the next change start again
      break;
    case BTN_PAIR_AS_REMOTE:
      if (this->link_ != nullptr) this->link_->start_toilet_pairing();
      break;
    case BTN_REFRESH:
      this->refresh_settings();
      break;
    case BTN_UNPAIR_REMOTE:
      if (this->link_ != nullptr) this->link_->unpair();
      break;
    case BTN_ADD_PROFILE:
      this->add_user_profile();
      break;
    case BTN_SEARCH_DEVICES:
      this->search_devices();
      break;
    case BTN_SEARCH_TOILET:
      if (this->link_ != nullptr) this->link_->search_toilet();
      break;
    case BTN_SCAN:
      this->start_settings_scan();
      break;
    case BTN_READ_INFO:
      this->read_device_info();
      break;
    case BTN_READ_STATS:
      this->read_statistics_();
      break;
#ifdef USE_AQUACLEAN_HOMEKIT
    case BTN_HOMEKIT_QR:
      this->homekit_show_qr();
      break;
#endif
    case BTN_REDISCOVER:
      this->rediscover();
      break;
    default:
      break;
  }
}

void AquaCleanHub::switch_action(HubSwitch *sw, uint8_t action, bool state) {
  switch (action) {
    case SW_FORWARD:
      if (this->link_ != nullptr) this->link_->set_forward(state);
      break;
    case SW_PAIRING:
      if (this->link_ != nullptr) {
        if (state)
          this->link_->start_pairing();
        else
          this->link_->stop_pairing();
      }
      break;
    case SW_ADVERTISE:
      if (this->link_ != nullptr) this->link_->set_advertise(state);
      break;
    case SW_IRK_ANDROID:
      this->irk_android_ = state;
      break;
    case SW_MAINTENANCE:
      this->maintenance_switch_(sw, state);
      return;
    case SW_DISCOVERY:
      this->discovery_ = state;
      ESP_LOGI(TAG, LT("Discovery mode %s"), state ? LT("on") : LT("off"));
      break;
    case SW_HOMEKIT:
#ifdef USE_AQUACLEAN_HOMEKIT
      this->hk_runtime_enabled_ = state;
      // Turning on starts HomeKit live from the loop; turning it off needs a restart (the SDK has no clean
      // stop). Only react to a real user toggle, not to the boot-time restore of the switch state.
      if (this->switches_restored_ && !state && this->hk_started_) {
        ESP_LOGI(TAG, LT("HomeKit off, restarting"));
        this->set_timeout("hk_apply", 800, []() {
          global_preferences->sync();
          App.safe_reboot();
        });
      }
#endif
      break;
    default:
      break;
  }
  sw->publish_state(state);
}

// ---------------------------------------------------------------------------------------------------
// Discovery: what does another model advertise and answer?
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::publish_advertisement(const std::string &text) {
  if (this->discovery_) ESP_LOGI(TAG, LT("Toilet advertisement: %s"), text.c_str());
  if (this->advertisement_sensor_ != nullptr)
    this->advertisement_sensor_->publish_state(text.size() > 250 ? text.substr(0, 250) : text);
}

void AquaCleanHub::publish_procedures_() {
  this->procedures_dirty_ = false;
  this->last_procedures_publish_ = millis();
  std::string text;
  for (auto &p : this->app_procedures_) {
    if (!text.empty()) text += ' ';
    text += str_sprintf("%02x:%" PRIu32, p.first, p.second);
  }
  ESP_LOGI(TAG, LT("Procedures used by the app: %s"), text.c_str());
  if (this->procedures_sensor_ != nullptr)
    this->procedures_sensor_->publish_state(text.size() > 250 ? text.substr(0, 250) : text);
}

void AquaCleanHub::start_settings_scan() {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected, scan not started"));
    return;
  }
  for (auto &f : this->scan_found_) f.clear();
  this->scan_total_ = SCAN_KINDS * SCAN_IDS;
  this->scan_done_ = 0;
  ESP_LOGI(TAG, LT("Settings scan: reading ids 00-1f of every kind (takes a minute or two)"));
  for (uint8_t k = 0; k < SCAN_KINDS; k++) {
    for (uint8_t id = 0; id < SCAN_IDS; id++) {
      Request req{};
      req.context = 0x01;
      req.procedure = SCAN_PROC[k];
      if (k == KIND_DEFAULT)
        req.args = {0x00, id};
      else
        req.args = {id};
      req.scan_index = k * SCAN_IDS + id;
      this->enqueue_(std::move(req));
    }
  }
  if (this->scan_sensor_ != nullptr) this->scan_sensor_->publish_state("scanning...");
}

void AquaCleanHub::scan_result_(int16_t index, const uint8_t *args, int len) {
  const uint8_t kind = index / SCAN_IDS;
  const uint8_t id = index % SCAN_IDS;
  // An empty answer means the id does not exist (07/53/51/57 answer every id, unknown ones with no value). Known ids
  // (those with an entity) are left out of the summary, only new ones are shown.
  if (len < 0) {
    ESP_LOGI(TAG, LT("Scan %s (proc %02x) id %02x: no answer"), SCAN_NAMES[kind], SCAN_PROC[kind], id);
  } else if (len == 0) {
    ESP_LOGD(TAG, LT("Scan %s (proc %02x) id %02x: not present"), SCAN_NAMES[kind], SCAN_PROC[kind], id);
  } else {
    // value: LE16; schedules answer <id> <LE32>
    uint32_t v = 0;
    const uint8_t *p = args;
    int n = len;
    if (kind == KIND_SCHEDULE && len >= 2 && args[0] == id) {
      p++;
      n--;
    }
    for (int i = 0; i < n && i < 4; i++) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    const SettingKind sk = kind == 4 ? KIND_COMMON : static_cast<SettingKind>(kind);  // active = common ids
    const bool known = this->find_setting(sk, id) != nullptr || (kind == KIND_SCHEDULE && id == 0x0B);  // 0b clock
    ESP_LOGI(TAG, "Scan %s (proc %02x) id %02x: %s = %" PRIu32 "%s", SCAN_NAMES[kind], SCAN_PROC[kind], id,
             to_hex(args, len).c_str(), v, known ? " (known)" : " (NEW)");
    if (!known) this->scan_found_[kind] += str_sprintf(" %02x=%" PRIu32, id, v);
  }
  this->scan_done_++;
  if (this->scan_done_ < this->scan_total_) return;
  // compact summary of unknown ids with their value, e.g. "07: 0a=0 0d=1 | 51: 08=0 0a=2"
  std::string text;
  for (uint8_t k = 0; k < SCAN_KINDS; k++) {
    if (this->scan_found_[k].empty()) continue;
    if (!text.empty()) text += " |";
    text += str_sprintf(" %02x:", SCAN_PROC[k]) + this->scan_found_[k];
  }
  if (text.empty()) text = " no unknown ids";
  text = text.substr(1);
  ESP_LOGI(TAG, LT("Settings scan done, unknown ids: %s"), text.c_str());
  if (this->scan_sensor_ != nullptr) this->scan_sensor_->publish_state(text.size() > 250 ? text.substr(0, 250) : text);
}

// ---------------------------------------------------------------------------------------------------
// Feature discovery: which model is it, which settings does it answer, which entities are shown?
//
// After the first connection the ESP reads the article number (82) and every setting id an entity uses.
// An entity is shown when its feature belongs to the model and the toilet
// confirms it: its own setting id answers with a value, or, for entities without one (commands, status,
// orientation light), another setting of the same feature does. An unknown model relies on the toilet
// alone. The result is stored; when it differs from what is shown, the ESP restarts once so Home Assistant
// picks up the new list of entities.
// ---------------------------------------------------------------------------------------------------

void AquaCleanHub::apply_profile_() {
  Profile p{};
  int8_t old_model = -1;
  const bool loaded = this->profile_pref_.load(&p) && p.magic == PROFILE_MAGIC;
  this->profile_valid_ = loaded && p.layout == this->layout_ && this->layout_ != 0;
  if (loaded) {
    old_model = p.model;
  } else {
    // stored by v1.6.1-v1.7.0 (128 entities at most): only the model is still of use
    struct ProfileV161 {
      uint32_t magic;
      uint32_t layout;
      std::array<uint32_t, 4> visible;
      int8_t model;
    } old{};
    auto pref = global_preferences->make_preference<ProfileV161>(fnv1_hash("geberit_aquaclean_profile"));
    if (pref.load(&old) && old.magic == 0x41514302) old_model = old.model;
  }
  if (this->profile_valid_) {
    this->profile_ = p;
    this->fd_model_ = p.model;
  }
  // Without a valid stored result: the functions of the model known from before (the list of entities changed
  // with an update), or everything when the model is not known. The discovery after connecting decides.
  const bool model_known = old_model >= 0 && old_model < (int)this->models_.size();
  if (model_known) this->apply_variants_(this->models_[old_model].caps);
  size_t hidden = 0;
  for (size_t i = 0; i < this->gated_.size(); i++) {
    const auto &g = this->gated_[i];
    bool show;
    if (this->profile_valid_)
      show = bit_get(p.visible, i);
    else {
      // no stored result yet: the capabilities of the model known from before, otherwise only what every
      // model has; the discovery after connecting decides
      const uint64_t caps = model_known ? this->models_[old_model].caps : this->unknown_caps_;
      show = (caps & g.req) == g.req;
    }
    show = show && !g.yaml_internal && this->slot_ok_(g);
    this->gated_[i].shown = show;
    if (!show && !g.yaml_internal) hidden++;
    g.entity->set_internal(!show);
    if (g.setting != nullptr) g.setting->hidden = !show;
  }
  if (this->profile_valid_)
    ESP_LOGI(TAG, LT("Stored feature set: model %s, %u of %u entities hidden"),
             p.model >= 0 && p.model < (int)this->models_.size() ? this->models_[p.model].name : LT("unknown"),
             (unsigned)hidden, (unsigned)this->gated_.size());
  else
    ESP_LOGI(TAG, LT("No stored feature set yet, discovery runs after the first connection"));
  this->publish_model_();
}

void AquaCleanHub::rediscover() {
  if (!this->is_ready()) {
    ESP_LOGW(TAG, LT("Not connected, discovery not started"));
    return;
  }
  if (this->fd_running_) return;
  // article number and counter list first, the probes queue behind them
  this->enqueue_(0x00, 0x82, {});
  this->enqueue_counters_();
  this->start_feature_discovery_();
}

void AquaCleanHub::start_feature_discovery_() {
  if (this->fd_running_) return;
  this->fd_running_ = true;
  this->fd_article_done_ = false;
  this->counter_descaling_seen_ = false;
  this->counter_ids_seen_.clear();
  this->status_extra_.erase(0x64);
  // connected modules (status 64) for the flush entities; the Sela does not answer it
  this->enqueue_(0x01, PROC_STATUS, {1, 0x64});
  ESP_LOGI(TAG, LT("Feature discovery: reading the model and %u setting ids"), (unsigned)this->gated_.size());
  for (size_t i = 0; i < this->gated_.size(); i++) {
    auto &g = this->gated_[i];
    g.probe = -1;
    if (g.probe_kind < 0 || g.probe_kind >= PROBE_COUNTER)
      continue;  // nothing to read (commands, status, light) or the counter list (read with the device info)
    // the same id (e.g. profile 1 for every entity of that profile) is read once; the answer counts for all
    bool queued = false;
    for (size_t j = 0; j < i && !queued; j++) {
      const auto &o = this->gated_[j];
      queued = o.probe_kind == g.probe_kind && o.probe_id == g.probe_id && o.probe_profile == g.probe_profile;
    }
    if (queued) continue;
    Request req{};
    req.context = 0x01;
    req.procedure = READ_PROC[g.probe_kind];
    if (g.probe_kind == KIND_DEFAULT)
      req.args = {g.probe_profile, g.probe_id};
    else
      req.args = {g.probe_id};
    req.probe_index = static_cast<int16_t>(i);
    this->enqueue_(std::move(req));
  }
  this->check_feature_discovery_();
}

void AquaCleanHub::on_article_(const std::string &article) {
  int8_t model = -1;
  size_t best = 0;
  for (size_t i = 0; i < this->models_.size(); i++) {
    const size_t n = strlen(this->models_[i].prefix);
    if (n > best && article.compare(0, n, this->models_[i].prefix) == 0) {
      best = n;
      model = static_cast<int8_t>(i);
    }
  }
  if (!article.empty() && model < 0) ESP_LOGW(TAG, LT("Article number %s is not in the model table"), article.c_str());
  if (!article.empty() || !this->fd_article_done_) {
    this->fd_model_ = model;
    this->publish_model_();
  }
  if (this->fd_running_ && !this->fd_article_done_) {
    this->fd_article_done_ = true;
    this->check_feature_discovery_();
  }
}

void AquaCleanHub::probe_result_(int16_t index, int len) {
  if (index < 0 || index >= (int16_t)this->gated_.size()) return;
  auto &g = this->gated_[index];
  // an empty answer means the toilet does not have this id (as in the settings scan)
  g.probe = len < 0 ? 2 : (len == 0 ? 0 : 1);
  for (auto &o : this->gated_)  // entities with the same probe (read once)
    if (o.probe_kind == g.probe_kind && o.probe_id == g.probe_id && o.probe_profile == g.probe_profile)
      o.probe = g.probe;
  ESP_LOGD(TAG, LT("Discovery: %s id %02x profile %u %s"), KIND_NAMES[g.probe_kind], g.probe_id, g.probe_profile,
           g.probe == 1   ? LT("present")
           : g.probe == 0 ? LT("absent")
                          : LT("no answer"));
  this->check_feature_discovery_();
}

void AquaCleanHub::check_feature_discovery_() {
  if (!this->fd_running_ || !this->fd_article_done_) return;
  for (auto &g : this->gated_)
    if (g.probe < 0 && g.probe_kind >= 0 && g.probe_kind < PROBE_COUNTER) return;  // probes still pending
  this->fd_running_ = false;

  const bool known = this->fd_model_ >= 0 && this->fd_model_ < (int)this->models_.size();
  // The model decides: an entity is shown only when the model has every capability it needs. A toilet whose
  // model is not known gets only what every model has. Setting ids that the toilet does not answer stay hidden
  // too (the toilet answers some ids of functions it does not have, so an answer alone proves nothing).
  const uint64_t caps = known ? this->models_[this->fd_model_].caps : this->unknown_caps_;
  for (auto &g : this->gated_) {
    if (g.probe_kind == PROBE_COUNTER)
      g.probe = this->counter_ids_seen_.count(g.probe_id) ? 1 : 0;
    else if (g.probe_kind == PROBE_MODULE) {
      auto m = this->status_extra_.find(0x64);
      g.probe = m != this->status_extra_.end() && ((m->second >> g.probe_id) & 1) ? 1 : 0;
    } else if (g.probe_kind == PROBE_SLOT)
      g.probe = this->slot_ok_(g) ? 1 : 0;
  }
  if (!known) ESP_LOGW(TAG, LT("Model not known: only the entities every AquaClean has are shown"));
  this->apply_variants_(caps);

  std::array<uint32_t, MAX_GATED / 32> visible{};
  for (size_t i = 0; i < this->gated_.size(); i++) {
    const auto &g = this->gated_[i];
    bool show = (caps & g.req) == g.req;
    if (show && g.probe_kind >= 0) show = g.probe != 0;  // its own id must not be missing
    if (show) bit_set(visible, i);
  }

  Profile p{};
  p.magic = PROFILE_MAGIC;
  p.layout = this->layout_;
  p.visible = visible;
  p.model = known ? this->fd_model_ : -1;
  // what is visible right now (all entities without a stored result)
  std::array<uint32_t, MAX_GATED / 32> current{};
  for (size_t i = 0; i < this->gated_.size(); i++)
    if (this->gated_[i].shown || this->gated_[i].yaml_internal) bit_set(current, i);
  const bool changed = current != visible;

  std::string hidden;
  for (size_t i = 0; i < this->gated_.size(); i++)
    if (!bit_get(visible, i)) {
      if (!hidden.empty()) hidden += ", ";
      hidden += this->gated_[i].entity->get_name().c_str();
    }
  ESP_LOGI(TAG, LT("Feature discovery done: %s; hidden: %s"),
           known ? this->models_[this->fd_model_].name : LT("unknown model"),
           hidden.empty() ? LT("none") : hidden.c_str());

  this->profile_ = p;
  this->profile_valid_ = true;
  this->profile_pref_.save(&p);
  global_preferences->sync();
  this->publish_model_();
  if (changed) this->restart_pending_ = true;
}

void AquaCleanHub::apply_variants_(uint64_t caps) {
  // options and ranges that depend on the model (OPTION_VARIANTS / RANGE_VARIANTS in __init__.py)
  this->model_caps_ = caps;
  const uint8_t lang = this->language_ < LANGUAGE_COUNT ? this->language_ : 0;
  for (auto &v : this->select_variants_) {
    v.select->use_alt((caps & v.cap) == v.cap);
    v.select->apply_language(lang);
  }
  for (auto &v : this->number_variants_) {
    if ((caps & v.cap) != v.cap) continue;
    v.number->set_max_value(v.max_raw);
    v.number->traits.set_max_value(v.max_raw + v.number->offset());
  }
  for (auto &e : this->status_enums_) this->publish_status_enum_(e.second);
  this->sela_light_publish_();
}

void AquaCleanHub::publish_model_() {
  const Texts &t = this->texts_[this->language_ < LANGUAGE_COUNT ? this->language_ : 0];
  const bool known = this->fd_model_ >= 0 && this->fd_model_ < (int)this->models_.size();
  if (this->model_sensor_ != nullptr) {
    if (known) {
      const auto &m = this->models_[this->fd_model_];
      std::string name = m.name;
      if (m.variant >= 0 && t.variants != nullptr) name += std::string(" (") + t.variants[m.variant] + ")";
      this->model_sensor_->publish_state(name);
    } else if (this->profile_valid_ || this->fd_article_done_) {
      this->model_sensor_->publish_state(t.model_unknown);
    }
  }
  if (this->functions_sensor_ == nullptr || t.features == nullptr || !this->profile_valid_) return;
  // the functions of the model (model overview); an unknown model has none listed
  std::string text;
  if (known) {
    const uint16_t features = this->models_[this->fd_model_].features;
    for (uint8_t f = 0; f < FEATURE_COUNT; f++) {
      if (!((features >> f) & 1)) continue;
      if (!text.empty()) text += ", ";
      text += t.features[f];
    }
  } else {
    text = t.model_unknown;
  }
  this->functions_sensor_->publish_state(text.size() > 250 ? text.substr(0, 250) : text);
}

// ---------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------

uint16_t AquaCleanHub::crc16_(const uint8_t *data, size_t len) {
  // CRC-CCITT with byte swap, start value 0x1234
  uint16_t crc = 0x1234;
  for (size_t i = 0; i < len; i++) {
    crc = static_cast<uint16_t>(((crc << 8) & 0xFF00) | ((crc >> 8) & 0x00FF)) ^ data[i];
    crc ^= (crc & 0xFF) >> 4;
    crc ^= static_cast<uint16_t>(crc << 12);
    crc ^= static_cast<uint16_t>((crc & 0xFF) << 5);
  }
  return crc;
}

}  // namespace esphome::geberit_aquaclean

#endif
