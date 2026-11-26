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
// Localized like /setup: the user-facing strings go through lt() (log_text.h), the compile-time English ->
// chosen-language lookup; every English string here must also be in all lang/log/<code>.json. Page prose/labels
// are HTML text or double-quoted attributes (escape & < > "); the two JS result messages are carried in hidden
// <span>s and read by textContent (no JS-string escaping); JSON errors are escaped for a JSON string.
//
// Requires: web_server:, plus the component's consent free functions (maintenance.cpp).
// ESP32 / ESP-IDF only. Signatures mirror psk_setup.h, verified against ESPHome 2026.10.2.

#include <string>
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/components/web_server_base/web_server_base.h"  // WebServerBase::add_handler
#include "esphome/components/web_server_idf/web_server_idf.h"    // AsyncWebHandler / AsyncWebServerRequest (ESP32)

// Same namespace as the component (esphome::geberit_aquaclean), so these resolve against the free functions
// defined in maintenance.cpp without pulling in hub.h.
namespace esphome {
namespace geberit_aquaclean {

// Defined in maintenance.cpp (reach the hub via global_aquaclean_hub).
bool maintenance_consent_accepted();
bool maintenance_consent_yaml();
bool maintenance_consent_locked();
bool accept_maintenance_consent();
// Defined in log_text.cpp: English -> chosen-language text (compile-time table).
const char *lt(const char *en);

static const char *const MAINT_SETUP_TAG = "maint_setup";

// Escape for HTML text and double-quoted attribute values.
static std::string maint_h(const char *s) {
  std::string o;
  for (char c : std::string(s)) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}
// Escape for a JSON string value.
static std::string maint_j(const char *s) {
  std::string o;
  for (char c : std::string(s)) {
    if (c == '\\' || c == '"') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}
static std::string maint_err(const char *en) { return std::string("{\"error\":\"") + maint_j(lt(en)) + "\"}"; }

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
        request->send(409, "application/json", maint_err("already set; factory-reset to change").c_str());
        return;
      }
      auto *c = request->getParam("confirm");
      if (c == nullptr || c->value() != "yes") {
        request->send(400, "application/json", maint_err("missing confirmation").c_str());
        return;
      }
      if (accept_maintenance_consent()) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      } else {
        request->send(409, "application/json", maint_err("already set; factory-reset to change").c_str());
      }
      return;
    }

    // GET /maintenance — always reachable on the LAN: status + warning + what to do.
    std::string page;
    page.reserve(2800);
    page +=
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'><title>";
    page += maint_h(lt("Physical maintenance"));
    page +=
        "</title><style>body{font-family:sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem}"
        "button{font-size:1rem;padding:.6rem;width:100%;box-sizing:border-box;margin:.4rem 0}"
        ".w{border:1px solid #c33;background:#fff4f4;border-radius:.4rem;padding:.2rem .9rem;margin:1rem 0}"
        ".s{font-weight:bold}</style></head><body><h2>";
    page += maint_h(lt("Physical maintenance"));
    page += "</h2><p>";
    page += maint_h(lt("Descaling, arm cleaning, calibration and counter resets. These need someone at the "
                       "toilet, so they stay hidden until this is enabled and the maintenance key on the remote "
                       "is long-pressed."));
    page += "</p><p class=s>";
    if (yaml)
      page += maint_h(lt("Status: enabled in this firmware's YAML build."));
    else if (on || locked)
      page += maint_h(lt("Status: enabled (stored on the bridge)."));
    else
      page += maint_h(lt("Status: disabled."));
    page += "</p><div class=w><p><b>";
    page += maint_h(lt("Read first."));
    page += "</b> ";
    page += maint_h(lt("Only use these functions while you are at the toilet yourself: a procedure run from a "
                       "distance cannot be supervised, and an interrupted or wrongly confirmed step can damage "
                       "the toilet, lock the shower, or cause scalding, slipping or injury from hot water and "
                       "moving arms. You use these functions at your own risk; this project is not made by "
                       "Geberit, and warranty or liability claims towards Geberit may be affected."));
    page += "</p></div>";

    if (can_enable) {
      page += "<p>";
      page += maint_h(lt("Enabling is permanent: it is stored on the bridge and only a factory reset turns it "
                         "off again."));
      page += "</p><button onclick=sv()>";
      page += maint_h(lt("I have read the warning - enable physical maintenance"));
      page += "</button><p id=m></p><span id=t_ok hidden>";
      page += maint_h(lt("Enabled. Hold the maintenance key on the remote to show the functions."));
      page += "</span><span id=t_err hidden>";
      page += maint_h(lt("Error: "));
      page += "</span><span id=t_e hidden>";
      page += maint_h(lt("Error"));
      page += "</span><script>"
              "function T(i){return document.getElementById(i).textContent;}"
              "function sv(){fetch('/api/accept_maintenance',{method:'POST',"
              "headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'confirm=yes'})"
              ".then(r=>r.json()).then(j=>{document.getElementById('m').innerText=j.status?T('t_ok'):"
              "(T('t_err')+j.error);}).catch(e=>{document.getElementById('m').innerText=T('t_e');});}"
              "</script>";
    } else if (yaml) {
      page += "<p>";
      page += maint_h(lt("This is set in the firmware's YAML (maintenance_disclaimer_accepted). To change it, "
                         "edit the YAML and reflash."));
      page += "</p>";
    } else {  // locked
      page += "<p>";
      page += maint_h(lt("Already enabled on this bridge. It is permanent; to turn it off again, factory-reset "
                         "the bridge (see the README)."));
      page += "</p>";
    }
    page += "<p>";
    page += maint_h(lt("Once enabled, hold the maintenance key on the toilet's remote to actually show the "
                       "functions; they hide again after 60 minutes, the Physical maintenance switch, or a "
                       "restart."));
    page += "</p></body></html>";

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
