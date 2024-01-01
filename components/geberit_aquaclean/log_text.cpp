#include "log_text.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace geberit_aquaclean {

static const LogText *g_texts = nullptr;
static uint16_t g_count = 0;

void set_log_texts(const LogText *texts, uint16_t count) {
  g_texts = texts;
  g_count = count;
}

const char *lt(const char *en) {
  if (g_texts == nullptr || en == nullptr) return en;
  int lo = 0, hi = static_cast<int>(g_count) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int c = strcmp(en, g_texts[mid].en);
    if (c == 0) return g_texts[mid].text;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return en;  // not translated: English
}

static bool g_obfuscate = false;

void set_log_obfuscate(bool on) { g_obfuscate = on; }
bool log_obfuscate() { return g_obfuscate; }

// a few buffers in turn: one log line may mask several texts
static char g_buf[6][200];
static uint8_t g_next = 0;

static const char *masked(const char *text) {
  char *out = g_buf[g_next];
  g_next = (g_next + 1) % 6;
  // count letters and digits, keep the last four of them; other signs (: . _ space) stay
  size_t alnum = 0;
  for (const char *p = text; *p; p++)
    if (isalnum(static_cast<unsigned char>(*p))) alnum++;
  size_t seen = 0, n = 0;
  for (const char *p = text; *p && n + 4 < sizeof(g_buf[0]); p++) {
    const unsigned char ch = static_cast<unsigned char>(*p);
    if (isalnum(ch)) {
      seen++;
      if (seen + 4 <= alnum) {
        out[n++] = '\xE2';  // U+2022 bullet
        out[n++] = '\x80';
        out[n++] = '\xA2';
        continue;
      }
    } else if (ch >= 0x80) {
      continue;  // other UTF-8 letters (names): left out, not shown
    }
    out[n++] = *p;
  }
  out[n] = 0;
  return out;
}

const char *ob(const char *text) {
  if (text == nullptr) return "";
  return g_obfuscate ? masked(text) : text;
}

const char *ob(const std::string &text) {
  if (!g_obfuscate) {
    // copy: the caller's string may be a temporary
    char *out = g_buf[g_next];
    g_next = (g_next + 1) % 6;
    snprintf(out, sizeof(g_buf[0]), "%s", text.c_str());
    return out;
  }
  return masked(text.c_str());
}

const char *ob_mac(const uint8_t *bda) {
  char tmp[18];
  snprintf(tmp, sizeof(tmp), "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
  return ob(std::string(tmp));
}

}  // namespace geberit_aquaclean
}  // namespace esphome
