# Third-Party Notices — Series 250 Bridge (experimental)

This is the experimental **Series 250 (AquaClean Alba)** branch: ESP-IDF / NimBLE firmware in C, separate from
the main ESPHome bridge and **never merged into it**. It builds on and refers to the third-party software and
names below. This file is a good-faith summary; where it and an upstream project's own license differ, the
upstream license governs.

## Bundled source code

None. This firmware contains no copied third-party source. The Arendi transport in `firmware/main/`
(HDLC framing, COBS byte-stuffing, CRC-16/Kermit, the v2 security envelope and command relay) is original code;
CRC-16/Kermit is a published algorithm, not copied source.

## Embedded assets

None. The configuration web portal (`firmware/main/web_portal.c`) is inline HTML/CSS using the `system-ui`
font only — no bundled icons, fonts, JavaScript or CSS libraries, and no CDN references.

## Build-time dependencies (not redistributed in this repository)

Fetched and compiled when you build the firmware with ESP-IDF; this repository does not redistribute their
source or binaries. Each is governed by its own license — consult the project for the authoritative terms.

| Project | License (as commonly distributed) | Role |
|---|---|---|
| **ESP-IDF** (Espressif Systems) | Apache-2.0 | ESP32-S3 SDK; build system, Wi-Fi, HTTP server, NVS |
| **NimBLE** host (Espressif `esp-nimble`, originally Apache Mynewt NimBLE) | Apache-2.0 | Dual-role BLE central + peripheral stack |
| **Mbed TLS** (via ESP-IDF) | Apache-2.0 | X25519 (Curve25519 ECDH), CMAC and HKDF for the v2 handshake |

## Trademarks

All product names, trade names, logos and marks are the property of their respective owners and are used here
only for **nominative and descriptive purposes** (identifying compatible hardware, models, protocols and
integrations). Their use does not imply any affiliation, endorsement or sponsorship. This project is an
independent, unofficial effort and is **not** affiliated with, endorsed or sponsored by Geberit AG. In
particular:

- **Geberit**, **AquaClean**, **Alba** (and the other model names) — trademarks of **Geberit AG** and/or its
  group companies.
- **Apple**, **Apple Home**, **HomeKit**, **Siri** — trademarks of **Apple Inc.**
- **Bluetooth** — trademark of **Bluetooth SIG, Inc.**
- **Wi-Fi** — trademark of the **Wi-Fi Alliance**.
- **Espressif**, **ESP32** — trademarks of **Espressif Systems**.
- **MQTT** — a trademark of **OASIS**.
- **Home Assistant**, **ESPHome** — trademarks of their respective project / owner (Open Home Foundation).
- **Homey**, **openHAB**, **Node-RED**, **Domoticz**, **ioBroker** — trademarks or marks of their respective
  projects and owners.
- **Apache Mynewt**, **Mbed TLS** — projects of the **Apache Software Foundation** / **Linaro / TrustedFirmware**.

All other trademarks remain the property of their respective owners.
