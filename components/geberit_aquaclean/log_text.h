#pragma once
// Log texts in the language chosen at compile time. The code keeps the English text; __init__.py hands over a
// sorted table English -> chosen language (lang/log/<language>.json), looked up when a line is logged.
#include <cstdint>
#include <string>

namespace esphome {
namespace geberit_aquaclean {

struct LogText {
  const char *en;
  const char *text;
};

void set_log_texts(const LogText *texts, uint16_t count);
const char *lt(const char *en);

/// Log obfuscation (option log_obfuscate): addresses, names, serial numbers and keys show only their last four
/// letters or digits, the rest as dots. For logs shared in a public issue.
void set_log_obfuscate(bool on);
bool log_obfuscate();
/// The text as given, or masked when obfuscation is on. The pointer stays valid for the next few calls.
const char *ob(const std::string &text);
const char *ob(const char *text);
/// A Bluetooth address (6 bytes, most significant first) as AA:BB:CC:DD:EE:FF, masked when obfuscation is on.
const char *ob_mac(const uint8_t *bda);

}  // namespace geberit_aquaclean
}  // namespace esphome

#define LT(s) ::esphome::geberit_aquaclean::lt(s)
