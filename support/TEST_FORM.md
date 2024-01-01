<!-- generated from support/matrix.yaml by support/generate.py — do not edit by hand -->

# Field-test form

Fill **Result** with PASS / FAIL / N/A, plus the device firmware, bridge release and a log
reference. A FAIL opens or updates the ticket named by **Key**. A PASS on real hardware is what
promotes a surface from `supported` to `validated` in `support/matrix.yaml`.

## AquaClean Sela

| Key | Surface | Matrix | Result | Device fw | Bridge | Log / notes |
|---|---|---|---|---|---|---|
| `sela/shower` | Shower start/stop | `validated` |  |  |  |  |
| `sela/lady_shower` | Lady shower | `validated` |  |  |  |  |
| `sela/jet_strength` | Shower jet strength (levels) | `validated` |  |  |  |  |
| `sela/arm_position` | Shower-arm position (levels) | `validated` |  |  |  |  |
| `sela/water_temperature` | Water temperature (levels) | `validated` |  |  |  |  |
| `sela/water_heating` | Water heating on/off | `validated` |  |  |  |  |
| `sela/oscillation` | Oscillating spray | `validated` |  |  |  |  |
| `sela/orientation_light` | Orientation light | `validated` |  |  |  |  |
| `sela/descaling` | Descaling + shower-head cleaning | `validated` |  |  |  |  |
| `sela/user_detection` | User detected / seated | `validated` |  |  |  |  |
| `sela/last_error` | Error-code label (module-aware text) | `validated` |  |  |  |  |
| `sela/profiles` | Bridge user profiles 1-6 (store / activate) | `validated` |  |  |  |  |
| `sela/irk_recognition` | Automatic user recognition via IRK | `validated` |  |  |  |  |
| `sela/remote_passthrough` | Remote control stays connected / keys pass through | `validated` |  |  |  |  |
| `sela/flush` | Pre-flush / automatic flush | `supported` |  |  |  |  |

## AquaClean Mera Comfort  (`supported`)

_Feature set resolved at runtime by feature discovery; not hardware-validated._

## AquaClean Mera Classic  (`supported`)

_Feature set resolved at runtime by feature discovery; not hardware-validated._

## AquaClean Tuma Comfort  (`supported`)

_Feature set resolved at runtime by feature discovery; not hardware-validated._

## AquaClean Tuma Classic  (`supported`)

_Feature set resolved at runtime by feature discovery; not hardware-validated._

## AquaClean Cama  (`supported`)

_Feature set resolved at runtime by feature discovery; not hardware-validated._

## AquaClean 4000  (`unsupported`)

_No app and no remote control, so the bridge cannot reach it. Listed for completeness._

## Integration routes

| Key | Route | Matrix | Result | Device fw | Bridge | Log / notes |
|---|---|---|---|---|---|---|
| `route/native_api` | ESPHome native API route | `validated` |  |  |  |  |
| `route/mqtt` | MQTT route | `validated` |  |  |  |  |
| `route/homekit` | Apple HomeKit (optional, can run stand-alone) | `validated` |  |  |  |  |

