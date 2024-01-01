# esp-homekit-sdk (vendored)

Components of Espressif's esp-homekit-sdk (ESPRESSIF MIT License, see the file headers), ESPHome variant by
rednblkx (https://github.com/rednblkx/esp-homekit-sdk, branch `esphome`, commit 1d1ea30): mDNS is left to ESPHome.
Changes: `esp_hap_core/idf_component.yml` no longer names libsodium, and `mu_srp` (public) and `esp_hap_core`
require mbedtls themselves (Espressif's libsodium used to pass it on, ESPHome's does not); `geberit_aquaclean` adds the libsodium
ESPHome already uses for API encryption, so `esphome/libsodium` and `espressif/libsodium` never meet in one build.

Non-commercial HomeKit Accessory Protocol: for personal use, not for products that are sold.
- Ed25519 key pair/signing for libsodium builds that leave them out (ESPHome's) is in `components/geberit_aquaclean/homekit_ed25519.c`, not here.
- `ESP_MFI_DEBUG` calls `hap_log_forward()` (weak, prints as before) instead of `printf`, so the application can put the SDK messages in its own log.
