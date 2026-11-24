# AquaClean Bridge — Homey app

Homey community app for the [Geberit AquaClean Bridge for ESP](https://github.com/barloew/geberit-aquaclean-bridge-for-esp).
It connects to the bridge over the **ESPHome native API** (Noise-encrypted, local) and turns every standard
entity of the bridge into Homey flow cards, in all 13 Homey languages.

## What you get

- **461 flow cards** (177 triggers, 126 conditions, 158 actions) for the 301 entities the bridge creates
  enabled by default (discovery/diagnostic entities that are disabled by default are left out).
- The 6 user profiles share one card per setting with a **profile picker** instead of six copies.
- Card kinds per entity type:

  | Entity | Triggers | Conditions | Actions |
  |---|---|---|---|
  | binary sensor | turned on / turned off | is on/off | – |
  | switch, light | turned on / turned off | is on/off | turn on, turn off, toggle |
  | sensor | changed (number token) | is above | – |
  | timestamp sensor | changed (date token) | – | – |
  | number | changed | is above | set to (range from the bridge) |
  | select | changed | is (options read from the bridge) | set to (options read from the bridge) |
  | text sensor | changed | is | – |
  | text | changed (not for *Enter IRK*) | – | set to |
  | time | changed | – | set to |
  | button | – | – | press (card title = entity name) |
  | remote control event | any button pressed (button + long-press tokens), specific button pressed | – | – |

- **Entity names come from the bridge's own `lang/*.yaml`** (Geberit terminology); only the sentence around the
  name is translated in `tools/templates.py`.
- The toilet fault also drives `alarm_problem` on the device tile.

Cards for functions a toilet model lacks stay in the list but fail with "not available on this toilet" — the
bridge decides what exists (feature discovery), the same as on every other route.

## How entities are recognised

ESPHome API 1.14+ no longer sends `object_id`, and the bridge's names depend on its firmware language. The app
therefore recognises each entity by **(sub-device name, entity name)** in *every* bridge language (29), from a
table generated out of the component. Commands carry the sub-device id, because names repeat across devices
("Spray intensity" exists in the session and in each profile). Renaming entities in your own YAML hides them
from the app.

## Regenerating after a bridge change

The cards and `lib/entities.json` are generated from `components/geberit_aquaclean/__init__.py` (`ENTITIES`,
`CAPS_OF`, …) and `lang/*.yaml`. Run with a Python that has ESPHome installed (the component imports it):

```sh
python3 tools/generate.py          # run inside homey/; reads ../components
homey app validate --level publish
```

Only names, card kinds and value ranges are exported — nothing about the toilet protocol.

## Pairing

1. Bridge on the native API route; save a key on `http://aquaclean-xxxxxx.local/setup`.
2. Homey → *Add device* → *AquaClean Bridge*. Bridges found via mDNS (`_esphomelib._tcp`, project
   `geberit.aquaclean`) are pre-selected; otherwise enter host/IP. Paste the key → *Connect*.
3. The app follows IP changes through mDNS. Host, port and key can be changed in the device settings.

Do not keep Homey's generic ESPHome app connected to the same bridge at the same time: every API client
costs the ESP memory and sockets.

## Testing

`test/run.js` runs the real flow-card listeners against a real ESPHome API server: an ESPHome *host* build made by
`test/gen_host_yaml.py` (template entities with the bridge's Dutch names and sub-devices).

```sh
python3 test/gen_host_yaml.py .. > aquaclean-test.yaml  # remove api.encryption
esphome compile aquaclean-test.yaml && .esphome/build/aquaclean-test/.pioenvs/aquaclean-test/program &
node test/run.js
```

## Layout

```
app.js                       app: registers all flow listeners (lib/flows.js)
drivers/aquaclean/           driver (pairing, mDNS) and device (one bridge)
drivers/aquaclean/driver.flow.compose.json   GENERATED flow cards
lib/entities.json            GENERATED runtime table (cards per entity, name matching)
lib/esphome.js               native API link (transport from @2colors/esphome-native-api)
lib/flows.js                 run listeners + trigger firing
tools/generate.py            generator; tools/templates.py card phrasing; tools/ui_strings.py UI texts
```

Not affiliated with or endorsed by Geberit.
