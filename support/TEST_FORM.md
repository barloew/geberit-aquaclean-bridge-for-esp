<!-- generated from support/matrix.yaml by support/generate.py — do not edit by hand -->

# Field-test form

Fill **Result** with PASS / FAIL / N/A, plus the device firmware, bridge release and a log
reference. A FAIL opens or updates the ticket named by **Key**. A PASS on real hardware is what
promotes a surface from `supported` to `validated` in `support/matrix.yaml`.

## AquaClean Alba

| Key | Surface | Matrix | Result | Device fw | Bridge | Log / notes |
|---|---|---|---|---|---|---|
| `alba/shower` | Shower start/stop | `supported` |  |  |  |  |
| `alba/jet_strength` | Shower jet strength (levels) | `supported` |  |  |  |  |
| `alba/arm_position` | Shower-arm position (levels) | `supported` |  |  |  |  |
| `alba/water_temperature` | Water temperature (levels) | `supported` |  |  |  |  |
| `alba/water_heating` | Water heating on/off | `supported` |  |  |  |  |
| `alba/oscillation` | Oscillating spray | `supported` |  |  |  |  |
| `alba/descaling` | Descaling + shower-head cleaning | `supported` |  |  |  |  |
| `alba/user_detection` | User detected / seated | `supported` |  |  |  |  |
| `alba/remote_passthrough` | Remote control stays connected / keys pass through | `supported` |  |  |  |  |
| `alba/composed_lady_shower` | Lady shower composed from a preset (arm position + strength + temperature + oscillation) | `planned` |  |  |  |  |
| `alba/profiles` | Bridge user profiles (up to six), applied as a settings batch | `planned` |  |  |  |  |
| `alba/irk_recognition` | Automatic user recognition via IRK | `planned` |  |  |  |  |
| `alba/last_error` | Error-code label (status index 6 -> module-aware text + hex) | `planned` |  |  |  |  |
| `alba/event_storage` | Event Storage fault-log read channel (250-specific; Phase 1 inventory/read, Phase 2 live push) | `planned` |  |  |  |  |

## Integration routes

| Key | Route | Matrix | Result | Device fw | Bridge | Log / notes |
|---|---|---|---|---|---|---|
| `route/native_api` | ESPHome native API route | `planned` |  |  |  |  |
| `route/mqtt` | MQTT route | `planned` |  |  |  |  |
| `route/homekit` | Apple HomeKit (optional, can run stand-alone) | `planned` |  |  |  |  |

