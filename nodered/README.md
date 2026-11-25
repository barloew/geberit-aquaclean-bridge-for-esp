# Geberit AquaClean Bridge over MQTT

Brings your **Geberit AquaClean shower toilet** into Node-RED through the
[Geberit AquaClean Bridge for ESP](https://github.com/barloew/geberit-aquaclean-bridge-for-esp): a small ESP32-S3 next
to the toilet that keeps the toilet's only Bluetooth connection and shares it with your smart home, the Geberit
remote control and the Geberit Home app at the same time. The bridge publishes every function of the toilet to your
MQTT broker; this flow turns those topics into tidy Node-RED messages, and your messages back into commands.

Start the shower from a flow, send yourself a message when descaling is due, switch the orientation light at night,
or let a long press on the remote control's arrow key switch on the bathroom fan.

> **You need the bridge.** This flow does not talk to the toilet itself. Build the bridge first (an ESP32-S3 with at
> least 8 MB flash, flashed with the **MQTT** firmware from the
> [web installer](https://barloew.github.io/geberit-aquaclean-bridge-for-esp/), or your own ESPHome build with
> `route: mqtt`) and enter your broker on the bridge's web page. Supported toilets: AquaClean Sela (validated), Mera,
> Tuma and Cama (supported through feature discovery). The AquaClean 4000 and the Alba are not supported.

## Highlights

- **One message per function.** `msg.topic` is a fixed key such as `shower_active`, `water_temperature` or
  `light_color`; `msg.payload` is a real value: `true`/`false`, a number or a text. Route it with a **switch** node on
  `msg.topic`, like any other device.
- **Any firmware language.** The bridge names its MQTT topics after the entity names in its firmware language, so
  `…/binary_sensor/user_detected/state` in English is `…/binary_sensor/gebruiker_gedetecteerd/state` in Dutch. The
  flow recognizes the topics of all 29 firmware languages and always gives you the same key.
- **Selections as numbers, labels in your language.** A selection such as the light color arrives as its option
  number (`0`, `1`, `2`, …), with the text in `msg.aquaclean.label`. Labels and the flow's own messages are available in
  32 languages: the bridge's 29 plus the Node-RED editor languages Japanese, Traditional Chinese and Brazilian
  Portuguese.
- **Commands with one link.** Send `msg.topic` + `msg.payload` to the **AquaClean command** link. The flow finds the
  right topic, converts the value, and checks it first: read-only entities, value ranges and unknown options are
  reported with `node.error`, so a **catch** node can handle them.
- **Remote control buttons as events.** Every button of the Geberit remote control, short and long press, arrives on
  its own output: use a spare button to trigger anything in your home.
- **Connection and alarms on their own output.** Bridge online/offline (MQTT availability), toilet connected, toilet
  problem, bridge problem and bridge health, ready for a notification flow.
- **Every state in global context.** `global.get("aquaclean").state.seated` works in any function node, on any tab;
  `…label` holds the option texts.
- **Only core nodes.** MQTT, function, link, switch, inject, debug and catch: no extra palette, no dashboard needed.
  Node-RED 3.0 or newer.
- **Generated from the bridge.** The entity tables come straight from the bridge's own entity list, so the keys
  match the bridge's other integrations (the Homey app and the ioBroker adapter use the same keys).

## Install

1. **Import.** In Node-RED: *Menu* → *Import*, paste the flow and click *Import*. A tab *AquaClean Bridge* appears.
2. **Broker.** Double-click the node *AquaClean bridge topics*, edit the server *MQTT broker (edit me)* and enter
   your broker (the same one the bridge uses), with user and password if needed.
3. **Settings.** Open the inject node *Settings*. The defaults fit a bridge with default settings:

   | Setting | Default | What it means |
   |---|---|---|
   | `topicPrefix` | `aquaclean` | The bridge's *MQTT topic prefix* (a field on its web page). |
   | `discoveryPrefix` | `homeassistant` | The discovery prefix: the shared convention that openHAB, Domoticz, Home Assistant and others read too. Change it only if you changed it on the bridge. |
   | `language` | `auto` | Language of labels and messages: `auto` follows the bridge, or a code such as `en`, `nl`, `de`, `fr`, `ja`, `zh-TW`, `pt-BR`. |
   | `onlyChanges` | `true` | Send a state only when its value changed (the remote control buttons always). |
   | `contextName` | `aquaclean` | Name of the global context object with every state. |

4. **Deploy.** The *Decode* node turns green: *Online · n entities*, counting up as the states arrive.

## How it works

```
[Settings] → [Apply settings] → [mqtt in: dynamic] → [Decode] ─┬→ (link) AquaClean states
                                                               ├→ (link) AquaClean remote buttons
                                                               └→ (link) AquaClean connection & alarms

(link) AquaClean command → [Encode] → [mqtt out]
```

- *Apply settings* subscribes the **mqtt in** node (dynamic subscription) to `<topicPrefix>/#` and to the discovery
  topics, so every setting lives in one place.
- *Decode* reads three kinds of topics: the bridge's availability (`<prefix>/status`), its discovery messages (names,
  option lists, ranges and units in the firmware language), and its states (`<prefix>/<component>/<entity>/state`).
- *Encode* sends to `<prefix>/<component>/<entity>/command`. It learns these topics from discovery or from the first
  state, so a command works once the bridge has been seen on the broker.
- The node *Entity tables (generated, do not edit)* holds the keys, the topic table of every firmware language, the
  option and button labels, and the flow's own texts. It puts them in global context when the flow starts.

## Reading states

Link to **AquaClean states** with a **link in** node on any tab. Every message looks like this:

```json
{
  "topic": "light_color",
  "payload": 6,
  "aquaclean": {
    "key": "light_color",
    "known": true,
    "group": "toilet",
    "kind": "select",
    "name": "Orientation light color",
    "label": "White",
    "unit": null,
    "shared": false,
    "mqtt": "aquaclean/select/orientation_light_color/state",
    "raw": "White"
  }
}
```

- `payload`: booleans for sensors, switches and the light; numbers for measurements, settings and option numbers;
  times as `"HH:MM"`; dates as milliseconds, ready for `new Date(msg.payload)`.
- `aquaclean.name` is the entity's name as the bridge shows it (its firmware language), `aquaclean.label` the option
  or button text in the language of your settings.
- A topic the tables do not know yet (a newer firmware) still arrives, as `unknown/<component>/<entity>` with the raw
  text as payload.

Typical patterns:

- **switch** node on `msg.topic` → one output per function (the examples do this for `seated` and
  `days_until_descaling`).
- **change** or **function** node: read any state with `global.get("aquaclean").state.<key>`, for instance to check
  whether someone is seated before you start something.
- **filter** (RBE) is built in: with `onlyChanges` a state only comes when its value changes.

## Sending commands

Wire a **link out** node to **AquaClean command** (the import already contains six examples), or point any node at it.
Set `msg.topic` to the key and `msg.payload` to the value:

| Kind of entity | `msg.payload` | Example |
|---|---|---|
| Button (start/stop, descaling step, …) | anything | `shower_toggle`, `true` |
| Switch | `true`, `false`, `"on"`, `"off"` or `"toggle"` | `default_oscillating_spray`, `true` |
| Light | `true`, `false` or `"toggle"` | `orientation_light`, `"toggle"` |
| Number | a number in its range (see the reference) | `default_water_temperature`, `3` |
| Selection | the option number, or its text in any of the 29 languages | `light_color`, `6` or `"White"` or `"Wit"` |
| Time | `"HH:MM"` | `light_window_1_start`, `"22:30"` |
| Text | a text | (texts are mostly setup fields) |

Starting the shower, the Lady shower or the dryer only works while someone is seated, just like on the remote
control. The **Encode errors** catch node shows every refused command in the debug sidebar.

## Remote control buttons

With the remote control paired to the bridge, **AquaClean remote buttons** sends `msg.topic = "remote_button"` and
`msg.payload` = the button id, with `_long` appended for a long press: `shower`, `spray_minus`, `spray_plus`,
`arm_back`, `arm_forward`, `lady_shower`, `profile_1` … `profile_3`, `arrow_right`, `arrow_left`, `arrow_down`,
`arrow_up`, `oscillation`, `orientation_light`, `ok`, `temperature`, `home`, `maintenance`. The button still does its
normal job on the toilet. `msg.aquaclean.label` names it in your language, e.g. *Arrow up (long press)*.

## Languages

- **Topics:** the flow knows the topic names of every firmware language: English, Dutch, German, French, Italian,
  Spanish, Polish, Czech, Hungarian, Danish, Swedish, Norwegian, Finnish, Portuguese, Arabic, Turkish, Estonian,
  Croatian, Latvian, Lithuanian, Romanian, Russian, Slovak, Slovenian, Serbian, Ukrainian, Chinese, Korean and Hebrew.
  Arabic, Russian, Ukrainian, Chinese, Korean and Hebrew firmware keeps English entity names over MQTT (topics are
  built from the names); option texts are still translated.
- **Labels and messages:** the 29 firmware languages plus Japanese (`ja`), Traditional Chinese (`zh-TW`) and
  Brazilian Portuguese (`pt-BR`). With `language: "auto"` the flow follows the bridge as soon as it recognizes its
  language from an option text.
- **Keys** (`msg.topic`) are the same in every language, so a flow written for an English bridge works unchanged with
  a Polish one.

## User profiles

The bridge keeps up to six user profiles (profiles 1–3 in the toilet, 4–6 on the bridge). Each profile's settings have
their own keys over MQTT, `profile_1_…` through `profile_6_…` (for example `profile_2_spray_intensity`), which you can
read and write independently of the running session and of the other profiles — the same keys the Homey app and the
ioBroker adapter use. The *Default …* settings and the profile buttons (`profile_N_activate`, `profile_N_store`) are
separate again.

## Troubleshooting

- **The Decode node stays yellow** (*Waiting for the bridge*): check the broker in the MQTT node, the `topicPrefix`,
  and that the bridge runs the MQTT firmware and is connected (its web page shows the broker settings).
- **"not seen on the broker yet"**: the flow learns each command topic from discovery or from the first state. Is the
  bridge online, and is discovery on (it is by default)?
- **Several bridges:** copy the tab and give each copy its own `topicPrefix` and `contextName`.
- **New entities after a bridge update** arrive as `unknown/…` until you import the matching version of this flow.

## Entity reference

What every standard entity does. Which entities exist depends on your toilet: the bridge detects the model and shows
only what it has. Names are the English ones; your bridge shows them in its firmware language.

<!-- entities:begin -->
### `bridge` — the bridge (the ESP)

| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |
|---|---|---|---|---|---|
| `remote_pairing` | Pair remote control | boolean | state + command | true / false | Opens the pairing window for the remote control: then hold + on the remote for about 30 s. |
| `remote_unpair` | Unpair remote control | – | command | any payload | Forgets the paired remote control. |
| `connected` | Toilet connected | boolean | state | true / false | The bridge is connected to the toilet. |
| `bridge_health` | Bridge health | number | state | 0 = OK, 1 = Degraded, 2 = Recovering, 3 = Needs power-cycle | Overall health of the bridge. |
| `bridge_problem` | Bridge problem | boolean | state | true / false | The bridge has a problem it cannot solve itself (e.g. it needs a power cycle). |
| `free_heap` | Free memory | number | state | B | Free memory on the ESP. |
| `min_free_heap` | Lowest free memory | number | state | B | Lowest free memory since start. |
| `largest_free_block` | Largest free block | number | state | B | Largest free memory block. |
| `pair_as_remote` | Pair with toilet | – | command | any payload | Pairs the bridge with the toilet (then switch the toilet off for about 30 s and on again). |
| `irk_android` | Capture IRK as Android keyboard | boolean | state + command | true / false | For Android phones: the bridge offers itself as a keyboard during IRK capture. |
| `recognition_threshold` | User recognition signal strength | number | state + command | -100–-30 dBm | Signal strength from which a linked device counts as near (default −60 dBm; about −80 dBm for a phone in a pocket). |
| `maintenance_mode` | Physical maintenance | boolean | state + command | true / false | Shows or hides the Physical maintenance device. Switching on needs the maintenance key on the remote control; see Physical maintenance. |
| `homekit_enable` | Apple Home | boolean | state + command | true / false | Turns the bridge's Apple Home accessory on or off. |
| `homekit_qr` | Show HomeKit QR code | – | command | any payload | Sends the HomeKit QR code as a Home Assistant notification (Home Assistant only). |
| `homekit_code` | HomeKit code | string | state | text | HomeKit setup code and the link to the bridge's /homekit page with the QR code. |

Setup entities of the web-installer firmware (normally set on the bridge's own web page):

| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |
|---|---|---|---|---|---|
| `restart` | Restart | – | command | any payload | Restarts the bridge. |
| `mqtt_broker` | MQTT broker | string | state + command | text | Web-installer firmware: host name or address of the MQTT broker. |
| `mqtt_port` | MQTT port | number | state + command | 1–65535 | Web-installer firmware: port of the MQTT broker (1–65535). |
| `mqtt_user` | MQTT user | string | state + command | text | Web-installer firmware: user name at the broker. |
| `mqtt_password` | MQTT password | string | state + command | text | Web-installer firmware: password at the broker; it is never shown again. |
| `mqtt_topic_prefix` | MQTT topic prefix | string | state + command | text | Web-installer firmware: base of every topic of the bridge. |
| `mqtt_connect` | Connect to MQTT broker | – | command | any payload | Web-installer firmware: stores the MQTT settings and restarts. |
| `time_zone` | Time zone | string | state + command | text | Web-installer firmware: time zone of the bridge's clock (the option text). |

### `toilet` — AquaClean Toilet

| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |
|---|---|---|---|---|---|
| `default_spray_intensity` | Default spray intensity | number | state + command | 1–5 | Spray intensity every new session starts with. |
| `default_spray_arm_position` | Default spray arm position | number | state + command | 1–5 | Spray arm position every new session starts with. |
| `default_lady_spray_intensity` | Default spray intensity Lady | number | state + command | 1–5 | Lady shower spray intensity every new session starts with. |
| `default_lady_spray_arm_position` | Default spray arm position Lady | number | state + command | 1–5 | Lady shower arm position every new session starts with. |
| `default_water_temperature` | Default water temperature | number | state + command | 0–5 | Water temperature every new session starts with. |
| `default_oscillating_spray` | Default oscillating spray | boolean | state + command | true / false | Oscillating spray at the start of every session. |
| `default_seat_heating` | Default seat heating | number | state + command | 0–5 | Seat heating level at the start of every session. |
| `default_dryer_temperature` | Default dryer temperature | number | state + command | 0–5 | Dryer temperature at the start of every session. |
| `default_dryer_intensity` | Default dryer intensity | number | state + command | 1–5 | Dryer fan power at the start of every session. |
| `default_dryer` | Default dryer | boolean | state + command | true / false | Dryer on or off at the start of every session. |
| `default_odor_extraction` | Default odor extraction | boolean | state + command | true / false | Odor extraction on or off at the start of every session. |
| `default_seat_heating_on` | Default seat heating on | boolean | state + command | true / false | Seat heating on or off at the start of every session. |
| `default_preflush` | Default pre-flush | boolean | state + command | true / false | Pre-flush on or off at the start of every session. |
| `default_wc_water_heating` | Default WC water heating | boolean | state + command | true / false | WC water heating on or off at the start of every session. |
| `orientation_light` | Orientation light | boolean | state + command | true / false (command also `"toggle"`) | Orientation light on or off. |
| `light_activation` | Orientation light activation | number | state + command | 0 = Off, 1 = On, 2 = Automatic, 3 = Time-controlled | When the orientation light is on: off, on, automatic (on approach) or time-controlled; models with a light sensor offer “in the dark”. |
| `light_color` | Orientation light color | number | state + command | 0 = Blue, 1 = Turquoise, 2 = Magenta, 3 = Orange, 4 = Yellow, 5 = Warm white, 6 = White | Color of the orientation light (7 colors). |
| `light_brightness` | Orientation light brightness | number | state + command | 1–5 | Brightness of the orientation light, 1–5. |
| `light_run_on_time` | Orientation light run-on time | number | state + command | 0 = 5 min, 1 = 10 min, 2 = 20 min, 3 = 30 min, 4 = 60 min | How long the orientation light stays on after use. |
| `light_window_1_start` | Orientation light window 1 start | string | state + command | `"HH:MM"` | Start of the first time window (HH:MM) for time-controlled light. |
| `light_window_1_end` | Orientation light window 1 end | string | state + command | `"HH:MM"` | End of the first time window (HH:MM). |
| `light_window_2_start` | Orientation light window 2 start | string | state + command | `"HH:MM"` | Start of the second time window (HH:MM). |
| `light_window_2_end` | Orientation light window 2 end | string | state + command | `"HH:MM"` | End of the second time window (HH:MM). |
| `demo_mode` | Demo mode | boolean | state + command | true / false | Demo mode of the toilet, as used in showrooms. |
| `lid_auto_open` | Open lid automatically | boolean | state + command | true / false | The lid opens automatically on approach. |
| `lid_auto_close` | Close lid automatically | boolean | state + command | true / false | The lid closes automatically after use. |
| `lid_sensor_range` | Lid sensor range | number | state + command | 1–5 | How far away the lid sensor reacts, 1–5. |
| `odor_extraction_run_on` | Odor extraction run-on | boolean | state + command | true / false | Odor extraction keeps running for a while after use. |
| `days_until_descaling` | Next descaling in | number | state | d | Days until the next descaling is due. |
| `showers_until_unlock` | Showers until unlock | number | state | number | Showers left before the shower locks itself until descaling is done. |
| `descaling_requested` | Descaling requested since | number | state | time in ms (JavaScript `Date`) | Since when the toilet asks for descaling. |
| `shower_count` | Showers today | number | state | number | Showers today. |
| `stat_uptime` | Toilet operating time | number | state | h | Total operating time of the toilet. |
| `stat_run_time` | Active operating time | number | state | h | Time the toilet was actively in use. |
| `stat_showers` | Showers total | number | state | number | Number of showers since commissioning. |
| `stat_lady_showers` | Lady showers total | number | state | number | Number of Lady showers since commissioning. |
| `stat_oscillation` | Showers with oscillation | number | state | number | Showers with oscillating spray. |
| `stat_lady_oscillation` | Lady showers with oscillation | number | state | number | Lady showers with oscillating spray. |
| `stat_dryer` | Dryer uses total | number | state | number | Number of dryer uses. |
| `stat_odor` | Odor extraction uses | number | state | number | Number of odor extraction uses. |
| `stat_seat_heating` | Seat heating uses | number | state | number | Number of seat heating uses. |
| `stat_lid` | Lid lifter uses | number | state | number | Number of lid movements. |
| `stat_cleaning` | Cleanings | number | state | number | Number of cleanings. |
| `stat_users` | Toilet visits total | number | state | number | Number of toilet visits. |
| `stat_system_flush` | System flushes | number | state | number | Number of system flushes. |
| `stat_profile_N` | Profile N uses | number | state | number | How often user profile N was used (N is 1–4). |
| `stat_last_shower` | Last shower duration | number | state | s | Duration of the last shower. |
| `stat_avg_shower` | Average shower duration | number | state | s | Average shower duration. |
| `stat_last_lady` | Last lady shower duration | number | state | s | Duration of the last Lady shower. |
| `stat_avg_lady` | Average lady shower duration | number | state | s | Average Lady shower duration. |
| `stat_last_sitting` | Last toilet visit duration | number | state | s | Duration of the last toilet visit. |
| `stat_avg_sitting` | Average toilet visit duration | number | state | s | Average duration of a toilet visit. |
| `last_descaling` | Last descaling | number | state | time in ms (JavaScript `Date`) | When the toilet was last descaled. |
| `descaling_count` | Number of descalings | number | state | number | Number of descalings so far. |
| `days_until_filter_change` | Days until filter change | number | state | d | Days until the filter needs replacing. |
| `odor_toggle` | Toggle odor extraction | – | command | any payload | Starts or stops odor extraction, at any time (also without anyone seated). |
| `odor_run_on_toggle` | Toggle odor extraction run-on | – | command | any payload | Starts or stops the run-on of the odor extraction. |
| `last_error_text` | Last error | string | state | text | The toilet's last reported error, as text. |
| `toilet_problem` | Toilet fault | boolean | state | true / false | The toilet reports a fault. Use it for an alarm or a notification. |
| `service_state` | Arm cleaning status | number | state | 0 = OK, 1 = Shower arm not cleaned, 2 = Dryer arm not cleaned, 3 = Shower and dryer arm not cleaned | Whether the shower and dryer arms are due for cleaning. |
| `lid_toggle` | Open or close lid | – | command | any payload | Opens or closes the lid. |
| `auto_flush` | Automatic flush | number | state + command | 0 = Off, 1 = On, 2 = Pre-flush | Automatic flush: off, on, or pre-flush. Needs Geberit's interface module. |
| `light_sensor_sensitivity` | Light sensor sensitivity | number | state + command | 1–5 | How dark it must be before the orientation light reacts, 1–5. |
| `accessibility_mode` | Accessibility mode | boolean | state + command | true / false | Accessibility mode for users who need more time or support. |
| `water_hardness` | Water hardness | number | state + command | 0 = Soft, 1 = Medium, 2 = Hard | Water hardness at your home; sets the descaling interval. |
| `reconnects` | Reconnections | number | state | number | How often the bridge had to reconnect to the toilet. |
| `connection_uptime` | Connected for | number | state | time in ms (JavaScript `Date`) | Moment the bridge last connected to the toilet. |
| `model` | Model | string | state | text | Model name, e.g. AquaClean Sela. |
| `firmware` | Firmware | string | state | text | Firmware versions of the toilet. |
| `toilet_signal` | Toilet signal | number | state | dBm | Bluetooth signal strength of the toilet at the bridge (about −60 dBm is ideal). |
| `toilet_address` | Address | string | state | text | Bluetooth address of the toilet. |
| `device_type` | Device type | string | state | text | Device type the toilet reports. |
| `article_number` | Article number | string | state | text | Article number from the type plate. |
| `serial_number` | Serial number | string | state | text | Serial number of the toilet. |
| `production_date` | Production date | string | state | text | Production date of the toilet. |
| `commissioning_date` | Commissioning | string | state | text | Commissioning date of the toilet. |
| `registration` | Registration | number | state | 0 = Not registered, 1 = Private, 2 = Public | Registration status of the toilet with Geberit. |
| `flush` | Flush | – | command | any payload | Flushes the toilet. Needs Geberit's interface module (built in on the floor-standing Mera). |

### `session` — Active session

`N` in `profile_N_activate` / `profile_N_store` is 1–6.

| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |
|---|---|---|---|---|---|
| `seated` | User detected | boolean | state | true / false | Someone is sitting on the toilet. |
| `shower_active` | Shower active | boolean | state | true / false | The shower is running. |
| `lady_shower_active` | Lady shower active | boolean | state | true / false | The Lady shower is running. |
| `shower_toggle` | Start or stop shower | – | command | any payload | Starts the shower, or stops it while it runs. Only while someone is seated. |
| `lady_shower_toggle` | Start or stop Lady shower | – | command | any payload | Starts the Lady shower, or stops it while it runs. Only while someone is seated. |
| `dryer_active` | Dryer running | boolean | state | true / false | The dryer is running. |
| `dryer_toggle` | Toggle dryer | – | command | any payload | Starts or stops the dryer. Only while someone is seated. |
| `stop_all` | Stop shower and dryer | – | command | any payload | Stops the shower and the dryer. |
| `stop_shower` | Stop shower | – | command | any payload | Stops the shower (models without dryer). |
| `spray_intensity` | Spray intensity | number | state + command | 1–5 | Spray intensity of the shower in the running session, 1–5 like on the remote control. |
| `spray_arm_position` | Spray arm position | number | state + command | 1–5 | Position of the spray arm for the shower, 1–5. |
| `lady_spray_intensity` | Spray intensity Lady | number | state + command | 1–5 | Spray intensity of the Lady shower, 1–5. |
| `lady_spray_arm_position` | Spray arm position Lady | number | state + command | 1–5 | Position of the spray arm for the Lady shower, 1–5. |
| `water_temperature` | Water temperature | number | state + command | 0–5 | Water temperature level; 0 means water heating off. |
| `oscillating_spray` | Oscillating spray | boolean | state + command | true / false | Oscillating spray (the spray arm moves back and forth). |
| `seat_heating` | Seat heating | number | state + command | 0–5 | Seat heating level; 0 is off. |
| `dryer_temperature` | Dryer temperature | number | state + command | 0–5 | Dryer temperature level. |
| `dryer_intensity` | Dryer intensity | number | state + command | 1–5 | Dryer fan power. |
| `dryer` | Dryer | boolean | state + command | true / false | Dryer on or off in the session settings. |
| `odor_extraction` | Odor extraction | boolean | state + command | true / false | Odor extraction on or off in the session settings. |
| `seat_heating_on` | Seat heating on | boolean | state + command | true / false | Seat heating on or off. |
| `preflush` | Pre-flush | boolean | state + command | true / false | Pre-flush before use on or off. |
| `wc_water_heating` | WC water heating | boolean | state + command | true / false | Heating of the WC water on or off. |
| `profile_N_activate` | Activate profile N settings | – | command | any payload | Loads the settings of user profile N into the session, as if that user was recognized. |
| `profile_N_store` | Store settings in profile N | – | command | any payload | Stores the current session settings in user profile N. |
| `remote_connected` | Remote control connected | boolean | state | true / false | The Geberit remote control is connected through the bridge. |
| `remote_button` | Remote control button | string | event | button id, e.g. `shower`, `shower_long` | The last button pressed on the remote control (e.g. shower, shower_long). Updated on every press, also when the same button is pressed again: trigger on any update. |
| `app_connected` | Geberit Home app connected | boolean | state | true / false | The Geberit Home app is connected. While it is, the app has priority. |
| `recognized_user` | Recognized user | string | state | text | Name of the user the bridge has recognized and whose profile is loaded. |
| `active_user` | Active user | string | state | text | Name of the recognized user who is seated. |

### `profile` — User profiles 1–6

Profiles 1–3 are stored in the toilet (profile keys 1–3 on the remote control), profiles 4–6 only on the bridge. `N` in `profile_N_…` is the profile number 1–6: each profile has its own keys (e.g. `profile_2_spray_intensity`), which you can read and write.

| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |
|---|---|---|---|---|---|
| `profile_N_spray_intensity` | Spray intensity | number | state + command | 1–5 | Spray intensity stored in this profile. |
| `profile_N_spray_arm_position` | Spray arm position | number | state + command | 1–5 | Spray arm position stored in this profile. |
| `profile_N_lady_spray_intensity` | Spray intensity Lady | number | state + command | 1–5 | Lady shower spray intensity stored in this profile. |
| `profile_N_lady_spray_arm_position` | Spray arm position Lady | number | state + command | 1–5 | Lady shower arm position stored in this profile. |
| `profile_N_water_temperature` | Water temperature | number | state + command | 0–5 | Water temperature stored in this profile. |
| `profile_N_oscillating_spray` | Oscillating spray | boolean | state + command | true / false | Oscillating spray stored in this profile. |
| `profile_N_seat_heating` | Seat heating | number | state + command | 0–5 | Seat heating level stored in this profile. |
| `profile_N_dryer_temperature` | Dryer temperature | number | state + command | 0–5 | Dryer temperature stored in this profile. |
| `profile_N_dryer_intensity` | Dryer intensity | number | state + command | 1–5 | Dryer fan power stored in this profile. |
| `profile_N_dryer` | Dryer | boolean | state + command | true / false | Dryer on or off in this profile. |
| `profile_N_odor_extraction` | Odor extraction | boolean | state + command | true / false | Odor extraction on or off in this profile. |
| `profile_N_seat_heating_on` | Seat heating on | boolean | state + command | true / false | Seat heating on or off in this profile. |
| `profile_N_preflush` | Pre-flush | boolean | state + command | true / false | Pre-flush on or off in this profile. |
| `profile_N_wc_water_heating` | WC water heating | boolean | state + command | true / false | WC water heating on or off in this profile. |
| `profile_N_delete` | Clear profile | – | command | any payload | Clears the profile: name, link, welcome light and IRK; profiles 4–6 also lose their settings. |
| `profile_N_irk_capture` | Capture IRK | – | command | any payload | Starts capturing the IRK of a phone, watch or tracker: pair it with the bridge within two minutes. |
| `profile_N_irk_show` | Show IRK | – | command | any payload | Sends the full IRK as a Home Assistant notification (Home Assistant only). |
| `profile_N_user_name` | User | string | state + command | text | Name of the user of this profile. |
| `profile_N_user_ha_device` | Home Assistant device | string | state + command | text | Home Assistant device tracker linked to this profile (Home Assistant only; elsewhere use an IRK instead). |
| `profile_N_welcome_light` | Welcome light | number | state + command | 0 = Off, 1 = Blue, 2 = Turquoise, 3 = Magenta, 4 = Orange, 5 = Yellow, 6 = Warm white, 7 = White | Color in which the orientation light greets this user once recognized; Off for no greeting. |
| `profile_N_user_recognized` | Recognized | boolean | state | true / false | The linked device of this user is near the toilet: the profile is loaded. |
| `profile_N_user_active` | Seated | boolean | state | true / false | This user is recognized and seated (the active user). |
| `profile_N_user_rssi` | Device signal user profile N | number | state | dBm | Signal strength of this user's linked device as the bridge hears it. |
| `profile_N_user_irk` | IRK | string | state | text | Last four digits of the stored IRK, to check which key is stored. |
| `profile_N_user_irk_set` | Enter IRK | string | state + command | text | Write a known IRK here (32 hex digits or base64). The field is emptied again right away; an empty value removes the stored IRK. |
| `profile_N_lady_oscillating_spray` | Oscillating spray Lady | boolean | state + command | true / false | Profiles 4–6 only: oscillating spray for the Lady shower, separate from the shower. |
| `profile_N_lady_water_temperature` | Water temperature Lady | number | state + command | 0–5 | Profiles 4–6 only: water temperature for the Lady shower, separate from the shower. |

### `maintenance` — Physical maintenance (only while unlocked)

| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |
|---|---|---|---|---|---|
| `descaling_state` | Descaling status | number | state | 0 = Idle, 1 = Preparing, 2 = Waiting for user, 3 = Descaling, 4 = Flushing | Current step of the descaling procedure. |
| `descaling_time_left` | Descaling time left | number | state | min | Minutes left in the current descaling step. |
| `descaling_start` | Start descaling | – | command | any payload | Starts descaling. Only at the toilet, following Geberit's instructions. |
| `descaling_continue` | Continue descaling | – | command | any payload | Confirms the next descaling step. |
| `descaling_cancel` | Cancel descaling | – | command | any payload | Cancels the descaling. |
| `shower_unlock` | Unlock shower | – | command | any payload | Unlocks a shower that is locked because descaling is overdue. |
| `filter_reset` | Filter replaced | – | command | any payload | Confirms that the filter has been replaced and restarts the countdown. |
| `arm_cleaning_start` | Start arm cleaning | – | command | any payload | Extends the spray arm for cleaning. |
| `arm_cleaning_next` | Arm cleaning next step | – | command | any payload | Next step of the arm cleaning. |
| `spray_calibration` | Spray calibration | boolean | state | true / false | The spray calibration is running. |
| `lid_calibration` | Calibrate lid | – | command | any payload | Calibrates the automatic lid. |
| `lid_angle_start` | Adjust lid opening angle | – | command | any payload | Starts adjusting the lid's opening angle. |
| `lid_angle_up` | Lid opening angle larger | – | command | any payload | Makes the lid's opening angle larger. |
| `lid_angle_down` | Lid opening angle smaller | – | command | any payload | Makes the lid's opening angle smaller. |
| `lid_angle_save` | Save lid opening angle | – | command | any payload | Saves the lid's opening angle. |

#### Advanced entities

Discovery and diagnostic tools for other models, disabled by default on the bridge. When you enable one on the bridge, it arrives with its own key too: Adjust dryer arm position, Adjust shower arm position, Advertisement, Connected modules, Discovery mode, Drain pump and pipe, Draining, Dryer arm offset, Dryer arm position higher, Dryer arm position lower, Extended trial remaining, Found functions, Last response, Lid offset, Procedures seen, Raw request, Raw status, Read statistics, Rediscover functions, Refresh settings, Remote control address, Remote control firmware, Remote control forward to toilet, Remote control last button, Remote control pass-through to toilet active, Save dryer arm position, Save shower arm position, Scan settings, Search for a nearby toilet, Settings scan, Shower arm offset, Shower arm position higher, Shower arm position lower, Timeouts, Trial period end, Trial period remaining, Without descaling the shower stops in.
<!-- entities:end -->

## About

This flow belongs to the [Geberit AquaClean Bridge for ESP](https://github.com/barloew/geberit-aquaclean-bridge-for-esp)
project (MIT license) and is generated from it (`nodered/tools/generate.py`). Problems with the flow or the bridge:
[issues](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/issues).

This is an independent, unofficial project, **not** affiliated with, endorsed or supported by Geberit AG. *Geberit*
and *AquaClean* are trademarks of Geberit AG, used here only to identify compatible products. The software is
provided "as is", without warranty of any kind; use it at your own risk.
