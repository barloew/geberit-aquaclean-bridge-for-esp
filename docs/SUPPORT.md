<!-- generated from support/matrix.yaml by support/generate.py — do not edit by hand -->

# Support matrix

Branch **main**

> Validated on AquaClean Sela (device firmware RS08 TS57, bridge v1.38.0) · Mera Comfort, Mera Classic, Tuma Comfort, Tuma Classic, Cama supported via feature discovery · 29 languages · routes: native_api, mqtt, homekit validated · Independent open-source project, not affiliated with Geberit AG

## Models

| Model | SAP prefix | Status | Evidence / note |
|---|---|---|---|
| AquaClean Sela | 146.22 | `validated` | bridge v1.38.0, device fw RS08 TS57 |
| AquaClean Mera Comfort | 146.21 | `supported` | Feature set resolved at runtime by feature discovery; not hardware-validated. |
| AquaClean Mera Classic | 146.20, 146.24 | `supported` | Feature set resolved at runtime by feature discovery; not hardware-validated. |
| AquaClean Tuma Comfort | 146.27, 146.29, 146.31 | `supported` | Feature set resolved at runtime by feature discovery; not hardware-validated. |
| AquaClean Tuma Classic | 146.07, 146.09, 146.32 | `supported` | Feature set resolved at runtime by feature discovery; not hardware-validated. |
| AquaClean Cama | 146.34, 146.30 | `supported` | Feature set resolved at runtime by feature discovery; not hardware-validated. |
| AquaClean 4000 | 146.13 | `unsupported` | No app and no remote control, so the bridge cannot reach it. Listed for completeness. |

### AquaClean Sela — surfaces

| Surface | Status |
|---|---|
| Shower start/stop | `validated` |
| Lady shower | `validated` |
| Shower jet strength (levels) | `validated` |
| Shower-arm position (levels) | `validated` |
| Water temperature (levels) | `validated` |
| Water heating on/off | `validated` |
| Oscillating spray | `validated` |
| Orientation light | `validated` |
| Descaling + shower-head cleaning | `validated` |
| User detected / seated | `validated` |
| Error-code label (module-aware text) | `validated` |
| Bridge user profiles 1-6 (store / activate) | `validated` |
| Automatic user recognition via IRK | `validated` |
| Remote control stays connected / keys pass through | `validated` |
| Pre-flush / automatic flush | `supported` |
| Dryer | `unsupported` |
| Odor extraction | `unsupported` |
| Automatic lid | `unsupported` |
| Seat heating | `unsupported` |

## Integration routes

| Route | Status |
|---|---|
| ESPHome native API route | `validated` |
| MQTT route | `validated` |
| Apple HomeKit (optional, can run stand-alone) | `validated` |

## Boards

| Board | Status | Note |
|---|---|---|
| m5stack-atoms3-lite | `validated` | recommended; reference board |
| m5stack-atoms3r | `builds` | builds, not yet tested on hardware. Has PSRAM — the board to prefer for MQTT + HomeKit at the same time. |
| xiao-esp32s3 | `builds` | builds (also Plus), not yet tested on hardware |
| esp32-s3-generic | `builds` | any ESP32-S3 with >=8 MB flash; builds, not yet tested on hardware |
| lilygo-t-dongle-s3 | `validated` | ESP32-S3, 16 MB flash, no PSRAM. Hardware-validated on a Sela. The T-Dongle-S3-Plus (8 MB PSRAM) needs a psram: block. HomeKit runs here too, but MQTT + HomeKit at the same time is tight on a board without PSRAM (lowest free heap ~5 KB during Apple Home pairing, log c67782f9) — it works, yet for heavy simultaneous use prefer a board with PSRAM (e.g. AtomS3R). |

## AquaClean device firmware seen

| Firmware | Model | Status | Since | Note |
|---|---|---|---|---|
| `RS08 TS57` | sela | `known-good` |  | BT node 10.18; remote 3.60.101.900/0001 |

## Build checks (device-independent)

| Check | Value | Status | How |
|---|---|---|---|
| languages | 29 | `structural` | tools/check_lang_keys.py |
