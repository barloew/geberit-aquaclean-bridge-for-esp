#pragma once
// Runtime native-API encryption-key (PSK) setup over HTTP — codeless onboarding for controllers that speak
// the ESPHome native API (Homey, ioBroker, Home Assistant, ...). The device starts keyless (unencrypted);
// the user sets a 32-byte base64 key once via http://<device>/setup (served on the web server and the
// "AquaClean Setup" captive portal), after which save_noise_psk() stores it in flash and turns encryption on.
//
// Requires: api: encryption: {}  (NO key in YAML — a YAML key compiles save_noise_psk() out), plus
// web_server: and/or captive_portal:. ESP32 / ESP-IDF only. Signatures verified against ESPHome 2026.10.2.
//
// Overwrite guard: once a key is active the endpoint refuses to change it (409) until a factory reset, so a
// later client on the LAN cannot silently swap the key.

#include <array>
#include <string>
#include "esphome/core/helpers.h"   // base64_decode(std::string, uint8_t*, size_t)
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"            // global_api_server, save_noise_psk, noise::psk_t
#include "esphome/components/web_server_base/web_server_base.h"  // WebServerBase::add_handler
#include "esphome/components/web_server_idf/web_server_idf.h"    // AsyncWebHandler / AsyncWebServerRequest (ESP32)

// Same namespace as the component (esphome::geberit_aquaclean). A bare global `namespace
// geberit_aquaclean` would collide with it and make every `geberit_aquaclean::` reference in the
// ESPHome-generated lambdas ambiguous.
namespace esphome {
namespace geberit_aquaclean {

static const char *const PSK_TAG = "psk_setup";

static const char PSK_SETUP_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>AquaClean setup</title>"
    "<style>body{font-family:sans-serif;max-width:30rem;margin:2rem auto;padding:0 1rem}"
    "input,button{font-size:1rem;padding:.6rem;width:100%;box-sizing:border-box;margin:.3rem 0}"
    "#k{word-break:break-all}</style></head><body>"
    "<h2>Native API encryption key</h2>"
    "<p>Generate a key, copy it into your controller (Homey, ioBroker, Home Assistant), then save it to the "
    "bridge. Once saved it cannot be changed without a factory reset.</p>"
    "<input id=k readonly placeholder='32-byte base64 key'>"
    "<button onclick=gen()>Generate random key</button>"
    "<button onclick=cp()>Copy</button>"
    "<button onclick=sv()>Save to bridge</button>"
    "<p id=m></p><script>"
    "function gen(){var a=new Uint8Array(32);crypto.getRandomValues(a);"
    "document.getElementById('k').value=btoa(String.fromCharCode.apply(null,a));}"
    "function cp(){navigator.clipboard.writeText(document.getElementById('k').value);}"
    "function sv(){var k=document.getElementById('k').value;"
    "fetch('/api/set_psk',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'psk='+encodeURIComponent(k)}).then(r=>r.json()).then(j=>{"
    "document.getElementById('m').innerText=j.status?'Saved. Add the bridge in your controller with this key.':"
    "('Error: '+j.error);}).catch(e=>{document.getElementById('m').innerText='Error';});}"
    "</script></body></html>";

class PskSetupHandler : public esphome::web_server_idf::AsyncWebHandler {
 public:
  bool canHandle(esphome::web_server_idf::AsyncWebServerRequest *request) const override {
    char buf[esphome::web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    auto url = request->url_to(buf);
    return url == "/setup" || url == "/api/set_psk";
  }

  void handleRequest(esphome::web_server_idf::AsyncWebServerRequest *request) override {
    char buf[esphome::web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    auto url = request->url_to(buf);

    if (url == "/setup") {
      request->send(200, "text/html", PSK_SETUP_PAGE);
      return;
    }

    // POST /api/set_psk  (form field: psk = 32-byte base64 key)
#if defined(USE_API_NOISE) && !defined(USE_API_NOISE_PSK_FROM_YAML)
    auto *api = esphome::api::global_api_server;
    if (api == nullptr) {
      request->send(500, "application/json", "{\"error\":\"api unavailable\"}");
      return;
    }
    if (api->get_noise_ctx().has_psk()) {
      request->send(409, "application/json", "{\"error\":\"key already set; factory-reset to change\"}");
      return;
    }
    auto *p = request->getParam("psk");
    if (p == nullptr) {
      request->send(400, "application/json", "{\"error\":\"missing psk\"}");
      return;
    }
    std::string b64 = p->value();
    esphome::noise::psk_t psk{};
    size_t n = esphome::base64_decode(b64, psk.data(), psk.size());
    if (n != psk.size()) {
      request->send(400, "application/json", "{\"error\":\"invalid key (need 32 bytes base64)\"}");
      return;
    }
    if (api->save_noise_psk(psk, true)) {
      ESP_LOGI(PSK_TAG, "Native-API encryption key set over HTTP and stored in flash");
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(500, "application/json", "{\"error\":\"failed to save key\"}");
    }
#else
    request->send(409, "application/json", "{\"error\":\"runtime key unavailable (built with a YAML key)\"}");
#endif
  }
};

inline void register_psk_endpoint(esphome::web_server_base::WebServerBase *base) {
  base->add_handler(new PskSetupHandler());
}

}  // namespace geberit_aquaclean
}  // namespace esphome
