<!-- generated from support/matrix.yaml by support/generate.py — do not edit by hand -->

# Support matrix

Branch **series-250-bridge** · maturity **experimental**

> Experimental — Alba supported · Independent open-source project, not affiliated with Geberit AG

## Models

| Model | SAP prefix | Status | Evidence / note |
|---|---|---|---|
| AquaClean Alba | — | `supported` | Basic shower toilet. Protocol/relay implemented; end-to-end validation pending (see TEST_PLAN.md). |

### AquaClean Alba — surfaces

| Surface | Status |
|---|---|
| Shower start/stop | `supported` |
| Shower jet strength (levels) | `supported` |
| Shower-arm position (levels) | `supported` |
| Water temperature (levels) | `supported` |
| Water heating on/off | `supported` |
| Oscillating spray | `supported` |
| Descaling + shower-head cleaning | `supported` |
| User detected / seated | `supported` |
| Remote control stays connected / keys pass through | `supported` |
| Lady shower composed from a preset (arm position + strength + temperature + oscillation) | `planned` |
| Bridge user profiles (up to six), applied as a settings batch | `planned` |
| Automatic user recognition via IRK | `planned` |
| Error-code label (status index 6 -> module-aware text + hex) | `planned` |
| Event Storage fault-log read channel (250-specific; Phase 1 inventory/read, Phase 2 live push) | `planned` |
| Native lady shower (hardware function) | `unsupported` |
| Dryer | `unsupported` |
| Odor extraction | `unsupported` |
| Orientation light | `unsupported` |
| Automatic lid | `unsupported` |
| Pre-flush / automatic flush | `unsupported` |
| Seat heating | `unsupported` |

## Integration routes

| Route | Status |
|---|---|
| ESPHome native API route | `planned` |
| MQTT route | `planned` |
| Apple HomeKit (optional, can run stand-alone) | `planned` |

## Boards

| Board | Status | Note |
|---|---|---|
| lilygo-t-dongle-s3 | `builds` | target board (ESP32-S3, >=8 MB); firmware unverified end-to-end |

## AquaClean device firmware seen

_None captured yet._
