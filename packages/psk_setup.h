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
//
// Localized: the user-facing strings go through lt() (log_text.h), the same compile-time English -> chosen
// language lookup the rest of the component uses; add the English strings to lang/log/<code>.json to translate
// them (missing entries fall back to English). Page prose/labels are emitted as HTML text or double-quoted
// attributes (so only & < > " need escaping); the two JS result messages are carried in hidden <span>s and read
// by textContent, so no JS-string escaping is needed. The JSON error strings are escaped for a JSON string.

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

// Defined in log_text.cpp: English -> chosen-language text (compile-time table). Forward-declared here so this
// packages/ include needs no path into the component dir; it links against the component.
const char *lt(const char *en);

static const char *const PSK_TAG = "psk_setup";

// Escape for HTML text and double-quoted attribute values.
static std::string psk_h(const char *s) {
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
static std::string psk_j(const char *s) {
  std::string o;
  for (char c : std::string(s)) {
    if (c == '\\' || c == '"') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}
// {"error":"<localized>"}
static std::string psk_err(const char *en) { return std::string("{\"error\":\"") + psk_j(lt(en)) + "\"}"; }

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
      std::string html =
          "<!DOCTYPE html><html><head><meta charset=utf-8>"
          "<meta name=viewport content='width=device-width,initial-scale=1'><title>";
      html += psk_h(lt("AquaClean setup"));
      html +=
          "</title><style>body{font-family:sans-serif;max-width:30rem;margin:2rem auto;padding:0 1rem}"
          "input,button{font-size:1rem;padding:.6rem;width:100%;box-sizing:border-box;margin:.3rem 0}"
          "#k{word-break:break-all}</style></head><body><h2>";
      html += psk_h(lt("Native API encryption key"));
      html += "</h2><p>";
      html += psk_h(lt("Generate a key, copy it into your controller (Homey, ioBroker, Home Assistant), then "
                       "save it to the bridge. Once saved it cannot be changed without a factory reset."));
      html += "</p><input id=k readonly placeholder=\"";
      html += psk_h(lt("32-byte base64 key"));
      html += "\"><button onclick=gen()>";
      html += psk_h(lt("Generate random key"));
      html += "</button><button onclick=cp()>";
      html += psk_h(lt("Copy"));
      html += "</button><button onclick=sv()>";
      html += psk_h(lt("Save to bridge"));
      html += "</button><p id=m></p>"
              "<span id=t_ok hidden>";
      html += psk_h(lt("Saved. Add the bridge in your controller with this key."));
      html += "</span><span id=t_err hidden>";
      html += psk_h(lt("Error: "));
      html += "</span><span id=t_e hidden>";
      html += psk_h(lt("Error"));
      html += "</span><script>"
              "function gen(){var a=new Uint8Array(32);crypto.getRandomValues(a);"
              "document.getElementById('k').value=btoa(String.fromCharCode.apply(null,a));}"
              "function cp(){navigator.clipboard.writeText(document.getElementById('k').value);}"
              "function T(i){return document.getElementById(i).textContent;}"
              "function sv(){var k=document.getElementById('k').value;"
              "fetch('/api/set_psk',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
              "body:'psk='+encodeURIComponent(k)}).then(r=>r.json()).then(j=>{"
              "document.getElementById('m').innerText=j.status?T('t_ok'):(T('t_err')+j.error);})"
              ".catch(e=>{document.getElementById('m').innerText=T('t_e');});}"
              "</script></body></html>";
      request->send(200, "text/html; charset=utf-8", html.c_str());
      return;
    }

    // POST /api/set_psk  (form field: psk = 32-byte base64 key)
#if defined(USE_API_NOISE) && !defined(USE_API_NOISE_PSK_FROM_YAML)
    auto *api = esphome::api::global_api_server;
    if (api == nullptr) {
      request->send(500, "application/json", psk_err("api unavailable").c_str());
      return;
    }
    if (api->get_noise_ctx().has_psk()) {
      request->send(409, "application/json", psk_err("key already set; factory-reset to change").c_str());
      return;
    }
    auto *p = request->getParam("psk");
    if (p == nullptr) {
      request->send(400, "application/json", psk_err("missing psk").c_str());
      return;
    }
    std::string b64 = p->value();
    esphome::noise::psk_t psk{};
    size_t n = esphome::base64_decode(b64, psk.data(), psk.size());
    if (n != psk.size()) {
      request->send(400, "application/json", psk_err("invalid key (need 32 bytes base64)").c_str());
      return;
    }
    if (api->save_noise_psk(psk, true)) {
      ESP_LOGI(PSK_TAG, "Native-API encryption key set over HTTP and stored in flash");
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(500, "application/json", psk_err("failed to save key").c_str());
    }
#else
    request->send(409, "application/json", psk_err("runtime key unavailable (built with a YAML key)").c_str());
#endif
  }
};

inline void register_psk_endpoint(esphome::web_server_base::WebServerBase *base) {
  base->add_handler(new PskSetupHandler());
}

}  // namespace geberit_aquaclean
}  // namespace esphome
