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

## Embedded assets

| Asset | License | Where / what |
|---|---|---|
| **Lucide** icons (`lucide-static`) — © Lucide Contributors | ISC | The icons embedded in the installation step diagrams (`docs/steps/*.svg`); <https://lucide.dev> |
| **IANA Time Zone Database** (tzdata) | Public domain | POSIX time-zone rules baked into the web-installer firmware at generation time |

The SVG/PNG diagrams and other documentation artwork in `docs/` are original project artwork, except for the
embedded Lucide icons noted above. The step diagrams reference common system fonts by name
(`-apple-system`, `Segoe UI`, `Noto Sans`, …) as CSS fallbacks; no font files are embedded or redistributed.

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
