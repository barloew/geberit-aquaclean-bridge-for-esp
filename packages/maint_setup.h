#pragma once
// Runtime physical-maintenance consent over HTTP — web-installer onboarding for the physical-maintenance
// functions (descaling, arm cleaning, calibration, counter resets). Those need someone at the toilet, so they
// are hidden unless consent is given AND the maintenance key on the remote is long-pressed. A YAML build opts
// in with maintenance_disclaimer_accepted: true; the web-installer firmware has no YAML to edit, so this page
// lets the user accept it once, from the bridge's own web UI.
//
// Reachable at http://<device>/maintenance on the normal network (the web server). It shows the current
// status, the full warning, and how to change it. Saving is PSK-style (mirrors /setup): it can be accepted
// until it is set once, and once stored only a factory reset changes it (POST returns 409 afterwards).
//
// NOTE: do not expect this to work while the "AquaClean Setup" captive portal is active — ESPHome's captive
// portal claims every GET while it is up (canHandle = active && GET) and is checked before this handler, so it
// would serve the Wi-Fi page instead. That is fine: consent is given on the LAN after Wi-Fi setup, not during
// the portal.
//
// Requires: web_server:, plus the component's consent free functions (maintenance.cpp).
// ESP32 / ESP-IDF only. Signatures mirror psk_setup.h, verified against ESPHome 2026.10.2.

#include <string>
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/components/web_server_base/web_server_base.h"  // WebServerBase::add_handler
#include "esphome/components/web_server_idf/web_server_idf.h"    // AsyncWebHandler / AsyncWebServerRequest (ESP32)

// Same namespace as the component (esphome::geberit_aquaclean), so these resolve against the free functions
// defined in maintenance.cpp without pulling in hub.h. A bare global `namespace geberit_aquaclean` would
// collide with it and make every `geberit_aquaclean::` reference in the generated lambdas ambiguous.
namespace esphome {
namespace geberit_aquaclean {

// Defined in maintenance.cpp (reach the hub via global_aquaclean_hub).
bool maintenance_consent_accepted();
bool maintenance_consent_yaml();
bool maintenance_consent_locked();
bool accept_maintenance_consent();

static const char *const MAINT_SETUP_TAG = "maint_setup";

// The warning shown on the page; kept in step with the README's physical-maintenance warning.
static const char MAINT_WARNING[] =
    "A maintenance procedure started from a distance cannot be supervised. An interrupted or wrongly confirmed "
    "step can damage the toilet or lock the shower until descaling is completed. Unlocking the shower or "
    "resetting counters without doing the maintenance hides a real need for it. You use these functions at your "
    "own risk. This project is not made by Geberit; claims under warranty or liability towards Geberit, and the "
    "service terms of your installer or service provider, may be affected. The authors accept no liability for "
    "any injury or damage, including scalding, slipping or water damage.";

class MaintSetupHandler : public esphome::web_server_idf::AsyncWebHandler {
 public:
  bool canHandle(esphome::web_server_idf::AsyncWebServerRequest *request) const override {
    char buf[esphome::web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    auto url = request->url_to(buf);
    return url == "/maintenance" || url == "/api/accept_maintenance";
  }

  void handleRequest(esphome::web_server_idf::AsyncWebServerRequest *request) override {
    char buf[esphome::web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    auto url = request->url_to(buf);

    const bool yaml = maintenance_consent_yaml();
    const bool locked = maintenance_consent_locked();
    const bool on = maintenance_consent_accepted();
    const bool can_enable = !yaml && !locked;  // PSK-style: acceptable until set once, then permanently locked

    if (url == "/api/accept_maintenance") {
      if (!can_enable) {
        request->send(409, "application/json", "{\"error\":\"already set; factory-reset to change\"}");
        return;
      }
      auto *c = request->getParam("confirm");
      if (c == nullptr || c->value() != "yes") {
        request->send(400, "application/json", "{\"error\":\"missing confirmation\"}");
        return;
      }
      if (accept_maintenance_consent()) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      } else {
        request->send(409, "application/json", "{\"error\":\"already set; factory-reset to change\"}");
      }
      return;
    }

    // GET /maintenance — always reachable on the LAN: status + warning + what to do.
    std::string page;
    page.reserve(sizeof(MAINT_WARNING) + 2600);
    page +=
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'><title>AquaClean maintenance</title>"
        "<style>body{font-family:sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem}"
        "button{font-size:1rem;padding:.6rem;width:100%;box-sizing:border-box;margin:.4rem 0}"
        ".w{border:1px solid #c33;background:#fff4f4;border-radius:.4rem;padding:.2rem .9rem;margin:1rem 0}"
        ".s{font-weight:bold}</style></head><body>"
        "<h2>Physical maintenance</h2>"
        "<p>Descaling, arm cleaning, calibration and counter resets. These need someone at the toilet, so they "
        "stay hidden until this is enabled <em>and</em> the maintenance key on the remote is long-pressed.</p>"
        "<p class=s>Status: ";
    if (yaml)
      page += "enabled in this firmware's YAML build.";
    else if (on || locked)
      page += "enabled (stored on the bridge).";
    else
      page += "disabled.";
    page += "</p>";

    page += "<div class=w><p><b>Read first.</b> ";
    page += MAINT_WARNING;
    page += "</p></div>";

    if (can_enable) {
      page +=
          "<p>Enabling is permanent: it is stored on the bridge and only a factory reset turns it off again.</p>"
          "<button onclick=sv()>I have read the warning &mdash; enable physical maintenance</button>"
          "<p id=m></p><script>"
          "function sv(){fetch('/api/accept_maintenance',{method:'POST',"
          "headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'confirm=yes'})"
          ".then(r=>r.json()).then(j=>{document.getElementById('m').innerText=j.status?"
          "'Enabled. Hold the maintenance key on the remote to show the functions.':('Error: '+j.error);})"
          ".catch(e=>{document.getElementById('m').innerText='Error';});}"
          "</script>";
    } else if (yaml) {
      page +=
          "<p>This is set in the firmware's YAML (<code>maintenance_disclaimer_accepted</code>). To change it, "
          "edit the YAML and reflash.</p>";
    } else {  // locked
      page +=
          "<p>Already enabled on this bridge. It is permanent; to turn it off again, factory-reset the bridge "
          "(see the README).</p>";
    }
    page +=
        "<p>Once enabled, hold the maintenance key on the toilet's remote to actually show the functions; they "
        "hide again after 60 minutes, the <em>Physical maintenance</em> switch, or a restart.</p>"
        "</body></html>";

    request->send(200, "text/html", page.c_str());
  }
};

inline void register_maintenance_endpoint(esphome::web_server_base::WebServerBase *base) {
  base->add_handler(new MaintSetupHandler());
}

// Route-agnostic registration: both web-installer routes (api and mqtt) expose a web server, so attach to the
// shared global base rather than a route-specific id. Mirrors how the HomeKit QR handler registers.
inline void register_maintenance_endpoint() {
  if (esphome::web_server_base::global_web_server_base != nullptr)
    esphome::web_server_base::global_web_server_base->add_handler(new MaintSetupHandler());
}

}  // namespace geberit_aquaclean
}  // namespace esphome
