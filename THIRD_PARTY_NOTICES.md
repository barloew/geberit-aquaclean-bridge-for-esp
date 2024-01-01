# Third-Party Notices

This project (the **Geberit AquaClean Bridge for ESP**, MIT-licensed — see [LICENSE](LICENSE)) includes,
builds on, or refers to third-party software, assets and names. Their licenses and trademarks are listed
below. This file is a good-faith summary; where it and an upstream project's own license differ, the
upstream license governs. Attribution for the reverse-engineering knowledge behind the code (not a licensing
matter) is in [`docs/SOURCES.md`](docs/SOURCES.md).

## Bundled source code

### QR Code generator library — Project Nayuki (MIT)

Files `components/geberit_aquaclean/qrcodegen.c` and `qrcodegen.h`, used to render the HomeKit setup QR code.
<https://www.nayuki.io/page/qr-code-generator-library>

```
QR Code generator library (C)

Copyright (c) Project Nayuki. (MIT License)
https://www.nayuki.io/page/qr-code-generator-library

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:
- The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
- The Software is provided "as is", without warranty of any kind, express or
  implied, including but not limited to the warranties of merchantability,
  fitness for a particular purpose and noninfringement. In no event shall the
  authors or copyright holders be liable for any claim, damages or other
  liability, whether in an action of contract, tort or otherwise, arising from,
  out of or in connection with the Software or the use or other dealings in the
  Software.
```

### Apple HomeKit Accessory Protocol SDK — Espressif (ESPRESSIF MIT License)

Vendored under `homekit_sdk/` (`esp_hap_core`, `esp_hap_apple_profiles`, `esp_hap_platform`), the HomeKit
accessory implementation. Copyright (c) 2020 Espressif Systems (Shanghai) Pte Ltd, under the "ESPRESSIF MIT
License" (use on Espressif products only). The full text is in each source file's header and in
`homekit_sdk/README.md`.

### mu_srp (Secure Remote Password) — Apache-2.0

`homekit_sdk/mu_srp/` — the SRP implementation used in HomeKit pairing. Apache License 2.0; see
`homekit_sdk/mu_srp/LICENSE`.

### SHA / HMAC / HKDF (RFC 6234 reference code) — IETF Trust

`homekit_sdk/hkdf-sha/upstream/` — the SHA and HMAC/HKDF reference implementation from RFC 6234.
Copyright (c) 2011 IETF Trust and the persons identified as the authors; BSD-style license, full text in
`homekit_sdk/hkdf-sha/upstream/sha.h`.

### Ed25519 (ref10, from libsodium) — ISC

`components/geberit_aquaclean/homekit_ed25519.c` — Ed25519 key-pair and signing (ref10), the same algorithm
as libsodium 1.0.21 (`crypto_sign/ed25519/ref10`). ISC license, Copyright (c) 2013-2025 Frank Denis.
<https://github.com/jedisct1/libsodium>

## Embedded assets

| Asset | License | Where / what |
|---|---|---|
| **Lucide** icons (`lucide-static`) — © Lucide Contributors | ISC | The icons embedded in the installation step diagrams (`images/steps/*.svg`); <https://lucide.dev> |
| **Inter** typeface (Latin subset of Regular, Medium, SemiBold, Bold, ExtraBold, ExtraBold Italic) — © 2016-2023 The Inter Project Authors | [SIL OFL 1.1](tools/overview/fonts/OFL.txt) | `tools/overview/fonts/`: used by `tools/overview/generate.py` to measure text and render the overview PNG. Not embedded in any image; <https://github.com/rsms/inter> |
| **IANA Time Zone Database** (tzdata) | Public domain | POSIX time-zone rules baked into the web-installer firmware at generation time |
| **Node-RED** icon — by **Nick O'Leary**, OpenJS Foundation | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) | `images/brands/node-red.svg`, also embedded unmodified in `images/badges/node-red-mqtt.svg`. Attribution required; the license is recorded in the file's own metadata. |
| Third-party logos (`images/brands/`) | Owners' brand terms, **not** this project's license | See [Logos](#logos) below |

All images live in [`images/`](images/README.md). The overview image (`images/overview/`), the step cards
(`images/steps/`), the line icons (`images/ui/`), the drawings (`images/illustrations/`) and the badges
(`images/badges/`) are original project artwork under this project's license, except for the embedded
Lucide icons, the Espressif symbol on the chip drawing, and the third-party logos embedded in the overview
image and the badges. The step diagrams, the overview image and the badges reference common system fonts by
name (`Inter`, `Segoe UI`, `Verdana`, …) as CSS fallbacks; no font files are embedded in them. The overview
generator bundles a subset of Inter (OFL 1.1, see above) only to measure text and render the PNG.

### Logos

The logos in `images/brands/` are **not** covered by this project's license. They remain the property of their
owners and are used only to show compatibility or protocol support (nominative use), unmodified. Their use
does not suggest endorsement, certification or affiliation. Before using one somewhere new, check the owner's
brand guidelines.

| File | Owner | Source | Notes |
|---|---|---|---|
| `esphome.svg` | Open Home Foundation | ESPHome media assets | The "Made for ESPHome" badge is not used |
| `home-assistant.svg` | Open Home Foundation | Home Assistant brand guidelines | The "Works with Home Assistant" badge is not used |
| `mqtt.svg` | MQTT.org / OASIS Open | mqtt/mqttorg-graphics on GitHub | |
| `homey.svg` | Athom B.V. | Homey press kit | |
| `iobroker.svg` | ioBroker | ioBroker repositories | License terms to be verified (CC BY-SA 4.0 was reported) |
| `openhab.svg` | openHAB Foundation e.V. | openHAB artwork page | The Foundation Member logo is not used |
| `node-red.svg` | OpenJS Foundation | Node-RED resources | Icon by Nick O'Leary, CC BY 3.0 (see above) |
| `domoticz.png` | Domoticz project | Domoticz repository | No SVG available |
| `espressif-logo.svg` | Espressif Systems | Espressif logo guidelines | Full logo (symbol and wordmark), unmodified |
| `espressif-symbol.svg`, `espressif-symbol-white.svg` | Espressif Systems | Derived from `espressif-logo.svg` | Symbol only (white: recoloured for the chip drawing); to be checked against Espressif's guidelines, which normally ask for symbol and wordmark |

Deliberately **not** used: the Bluetooth, Wi-Fi, USB, Apple / "Works with Apple Home" and Geberit logos. Their
use needs a membership, certification or license (Bluetooth SIG, Wi-Fi Alliance, USB-IF, Apple MFi) that this
project doesn't have, or isn't nominative. Generic line icons from `images/ui/` stand in for them.

## Code this project is derived from (no code copied)

| Source | License | What was used |
|---|---|---|
| **irk-capture** by Derek Seaman and Kyle Teal — <https://github.com/DerekSeaman/irk-capture> | MIT | The *idea* of IRK capture (advertising as a device whose characteristics require encryption so a phone hands over its Identity Resolving Key). Re-implemented from scratch for Bluedroid in `irk_capture.cpp`; no source was copied. |

## Build-time dependencies (not redistributed in this repository)

These are fetched and compiled when you build the firmware with ESPHome; this repository does not redistribute
their source or binaries. Each is governed by its own license — consult the project for the authoritative terms.

| Project | License (as commonly distributed) | Role |
|---|---|---|
| **ESPHome** (Open Home Foundation / Nabu Casa) | GPL-3.0 (core); MIT for `aioesphomeapi` | Firmware framework and native-API client this component plugs into |
| **ESP-IDF** (Espressif Systems) | Apache-2.0 | ESP32-S3 SDK |
| **Arduino-ESP32** (Espressif Systems) | LGPL-2.1 | Arduino framework layer, where used |
| **Mbed TLS** | Apache-2.0 | Cryptographic primitives used via the platform |
| **libsodium** (© Frank Denis) | ISC | API encryption, via ESPHome (the bundled `homekit_ed25519.c` supplements it) |

## Trademarks

All product names, trade names, logos and marks are the property of their respective owners and are used here
only for **nominative and descriptive purposes** (identifying compatible hardware, models, protocols and
integrations). Their use does not imply any affiliation, endorsement or sponsorship. In particular:

- **Geberit**, **AquaClean**, **Sela**, **Mera**, **Tuma**, **Cama**, **Alba** — trademarks of **Geberit AG**
  and/or its group companies. This project is independent and **not** affiliated with Geberit AG (see the
  full disclaimer in the [README](README.md#trademark--legal-disclaimer)).
- **Apple**, **Apple Home**, **HomeKit**, **Siri** — trademarks of **Apple Inc.**
- **Bluetooth** — trademark of **Bluetooth SIG, Inc.**
- **Wi-Fi** — trademark of the **Wi-Fi Alliance**.
- **Espressif**, **ESP32** — trademarks of **Espressif Systems**.
- **MQTT** — a trademark of **OASIS**.
- **Home Assistant**, **ESPHome** — trademarks of their respective project / owner (Open Home Foundation).
- **Homey**, **openHAB**, **Node-RED**, **Domoticz**, **ioBroker**, **Lucide** — trademarks or marks of their
  respective projects and owners.

All other trademarks remain the property of their respective owners.
