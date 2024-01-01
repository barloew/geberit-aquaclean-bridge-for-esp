// Built-in setup & log portal for the Series 250 bridge (test convenience).
//
// Brings up a WiFi SoftAP and a small web server so a tester can, with no serial
// cable and no external tools:
//   - watch the live log in a browser,
//   - enter the toilet's BLE address (stored in NVS), and
//   - optionally turn WiFi off (to take WiFi out of the picture if wanted).
//
// Not part of the shipped bridge. WiFi and BLE coexist fine on this hardware
// (the main bridge runs its BLE dual-role alongside WiFi); /radio off frees the
// radio if you want to isolate WiFi — see TEST_PLAN.md.
//
// UNVERIFIED: written against ESP-IDF 5.x but not compiled here; expect to
// adjust API/Kconfig names when you build on hardware.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Install the ESP-LOG capture hook. Call once, early in app_main(), so the log
// ring buffer mirrors everything ESP_LOGx prints (serial output is preserved).
void web_portal_install_log_hook(void);

// Read the toilet BLE address stored by the web form. Returns true and fills
// addr[6] (little-endian, the form of TOILET_ADDR) if a value is stored.
bool web_portal_get_toilet_addr(uint8_t addr[6]);

// Start the SoftAP and the web server. Call after the BT MAC clone.
void web_portal_start(void);

// Report a BLE device seen by the central's scan, for the web "pick your toilet"
// list. addr_le is the NimBLE little-endian address; name may be "" if none.
// Safe to call from the BLE host task; deduplicates by address.
void web_portal_report_device(const uint8_t addr_le[6], uint8_t addr_type, int rssi, const char *name);

// ---- health & recovery (persisted across reboots) ----------------------------
// The recovery breadcrumb: how many recovery reboots have happened this bad
// episode. Bounds the reboot ladder so a wedge can never loop reboots.
uint8_t web_portal_recovery_count_get(void);
void web_portal_recovery_count_set(uint8_t n);
// A short reason string persisted to NVS before a self-reboot and shown on the
// page after the reboot (the RAM log ring does not survive a restart).
void web_portal_set_last_reason(const char *reason);
// The live coarse health verdict shown on the page (e.g. "ok", "recovering",
// "needs power-cycle"). Not persisted.
void web_portal_set_health(const char *state);
