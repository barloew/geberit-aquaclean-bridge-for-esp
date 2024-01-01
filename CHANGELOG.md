# Changelog

## Planned

- **MQTT device split.** Over the MQTT route all entities currently appear under one Home Assistant device;
the native API route already splits them into separate devices (toilet, active session, maintenance, user
profiles 1–6). ESPHome's opt-in MQTT sub-device support (`sub_device_topics`, PR #20353 — approved, not yet
merged/released) will let MQTT do the same. When it ships, set `sub_device_topics: true` in
`packages/mqtt.yaml` and rebuild the web-installer firmware so MQTT installs get the device split too, with
no ESPHome needed on the user's side.

## v1.36.0 (not released yet)

- **The overview image is generated.** `tools/overview/generate.py` builds `images/overview/` (SVG and the 3×
PNG the README shows) from `tools/overview/overview.yaml`, in the same way as the step cards and badges. Models,
the language count, the recommended board and the drawn routes come from `support/matrix.yaml`, so the image
can't claim more than the matrix. It also refuses Bluetooth, Wi-Fi, USB, Apple and Geberit logos and a route
that lists only Home Assistant. Text is measured and the PNG rendered with a bundled Inter subset (OFL 1.1), so
the output is identical on every machine; CI checks it with `--check`.
- **Overview image updated.** New pill *Fully local, no cloud* (new generic icon `images/ui/cloud-off.svg`). The
native API and MQTT rows now read the same way: both install from the web installer with no ESPHome build, and
the native API key is set on the bridge. HomeKit reads *built in · off until turned on*. The highlights follow
the order a user meets them: installation, feature discovery, six profiles, recognition, welcome light.
- **Translated overview images.** Dutch, German, French and Polish versions of the overview in
`images/overview/translations/`, as source material for articles (not used in the README). Texts are in
`tools/overview/i18n/<lang>.yaml`; the generator fails on a missing or stale translation and on text that doesn't
fit its box, and `--check` covers every language.
- **All images in one top-level folder, `images/`** (see `images/README.md`): the overview image
(`images/overview/`, new version, PNG plus its SVG source), the step cards (`images/steps/`, were
`docs/steps/`), the platform badges (`images/badges/`), the project's own line icons and drawings
(`images/ui/`, `images/illustrations/`) and the third-party logos (`images/brands/`). `tools/steps/steps.json`
now writes to `images/steps`.
- **README badges for every platform**, grouped by route: native API (Home Assistant, Homey, ioBroker), MQTT
(Home Assistant, openHAB, Node-RED, Domoticz) and Apple Home (optional, also stand-alone). Built with each
platform's own logo by `tools/badges/generate.py`; no external badge service. The Node-RED icon is credited to
Nick O'Leary (CC BY 3.0) in the README and `THIRD_PARTY_NOTICES.md`, which now lists every logo and its terms.
- **Breaking: entity keys and English labels are now US English.** `odour_extraction` → `odor_extraction`,
`light_colour` → `light_color`, `recognised_user` → `recognized_user`, `user_recognised` → `user_recognized`,
`stat_odour` → `stat_odor` and the matching profile/default/command keys. Dashboards, automations and YAML that
reference the old keys must be updated. Only the English spelling changed; the other 28 translations keep their
wording, and their keys follow the new names.
- **Web installer installs the MQTT route** (was: native API without encryption). Firmware for every board
(AtomS3 Lite, AtomS3R, XIAO ESP32S3) and every language. After installing, the MQTT broker, port, user,
password and time zone are entered on the bridge's web page and stored on the ESP (`packages/mqtt_setup.yaml`);
*Connect to MQTT broker* restarts with the new settings. Without a broker MQTT stays off, and an unreachable
broker no longer restarts the ESP. The password is never shown or published again.
- `tools/gen_firmware.py` generates the firmware files, `mqtt_setup.yaml` (50 time zones from tzdata), the
board list of the install page and the build matrix (board x language).
- `provisioning.yaml` no longer contains `dashboard_import` (that needs the native API).
- **LilyGo T-Dongle-S3 board** (`packages/board-lilygo-tdongle-s3.yaml`): ESP32-S3, 16 MB flash, no PSRAM; APA102 RGB status LED (GPIO40 data / GPIO39 clock) and BOOT button (GPIO0). Hardware-validated on a Sela over the MQTT route (model, firmware RS08 TS57 and settings read out). No PSRAM; the T-Dongle-S3-Plus (8 MB PSRAM) needs a `psram:` block. Offered by the web installer and ESPHome YAML. The web-installer image makes no flash-vendor assumption; self-builders can uncomment `advanced: flash_chip: winbond` in the board file to free IRAM (the tested unit's flash is Winbond).
- **Enter IRK** per profile (password field, in Home Assistant and on the web page) and the action
`aquaclean_set_irk` (native API): store a known IRK as 32 hex digits (as Show IRK and Private BLE Device show
it) or base64 (iOS keychain); empty removes it. The key is never shown or published again.
- Shower, Lady shower and dryer are refused while nobody is seated, from every source (Home Assistant, MQTT,
HomeKit, actions), with a log line in the chosen language; the toilet blocks the arms then and used to ignore
the command silently (Geberit's installation test also needs weight on the seat). Odor extraction stays
possible at any time. Before, HomeKit only checked the showers.
- Tuma Comfort complete system (146.29x): automatic flush and flush (with the interface module) added, from Geberit's
product information; feature discovery still hides them without the module.
- README and `docs/models.md`: light sensor sensitivity is on the Sela (not the Mera Comfort); which models have
flush actuation and draining.
- Docs: the model table in the README and `docs/models.md` now list every model including the AquaClean Alba
with an "In this bridge" / not-supported marking and their functions.
- Docs: the AquaClean Alba / Series 250 is listed in the README and `docs/models.md` with a not-supported marking
and its functions. It uses an encrypted protocol (protocol v2); that protocol documentation is not public
and is kept privately by the author. A separate, experimental `series-250-bridge` branch (its own NimBLE firmware) exists.
This component is unchanged; Series 250 is still not supported here.
- Docs: corrected the AquaClean Alba function list (README model table and `docs/models.md`) to match the Alba
manual. The Alba is a basic shower toilet — jet strength and oscillation on the remote (`<+>`/`<–>`, and
together for oscillation), arm position / water temperature / water heating in the app, descaling via
`<Shower>` + `<–>` 5 s or the app. It has **no** lady shower, dryer, odor extraction, orientation light,
app-controlled lid or flush actuation (earlier the list wrongly included these).
- Docs: new [`docs/privacy.md`](docs/privacy.md) — which entities expose per-person usage (recognized / active
user, per-profile recognition, the `stat_*` counters and timers) and how to switch each off with `internal: true`
or `disabled_by_default: true`, plus the feature-level opt-out of simply not linking a device.
- README and the Physical maintenance notification (all languages): the service terms of your installer and/or
service provider may be affected too.
- Physical maintenance warning (README and notification, all languages): water, possibly hot, and hot air from the
dryer can come out of the toilet onto people nearby or on the seat (scalding, slipping, water damage); the
authors accept no liability for injury or damage. The legal disclaimer in the README excludes liability too.
- README: settings don't need anyone seated (session settings are kept for the next session); the sentence that
they are ignored was wrong.
- XIAO ESP32S3: the welcome light now blinks twice instead of staying on for 1.6 s.
- New README with installation steps as cards (`tools/steps/`: `steps.json` → `docs/steps/*.svg` and the
README section between the `steps` markers).
- **Self-monitoring and automatic recovery of the toilet link.** The bridge now watches its own health and
recovers from a wedged or lost link without losing the "no timeouts, zero reboots" behaviour in normal use:
  - *Liveness probe during an app session.* Any decoded frame from the toilet (a poll answer, an app answer or a
  notification) refreshes a liveness timestamp. While the Geberit app is connected the hub stays silent, so a
  toilet that wedges mid app-session would go unnoticed; after 90 s of silence it injects one status poll so the
  existing timeout path can detect a dead toilet. A healthy app session keeps the timestamp fresh, so it never
  fires then.
  - *Peripheral advertising guardian.* If advertising as the toilet ever stops with nothing scheduled to bring it
  back (a silent peripheral wedge, invisible to any request timeout), it is re-asserted after a 5 s grace — so the
  remote and app can always reach the bridge while the toilet link stays healthy.
  - *Recovery ladder.* Reconnect flapping (≥ 5 in 10 min) or 5 min continuously unhealthy escalates: at most two
  gated reboots per bad episode — never while the app is connected — bounded by a persisted NVS counter so a wedge
  can never loop reboots, then a sticky "needs power-cycle" state. A sustained healthy period (10 min) clears the
  episode. Runs only after the first successful connection, so a missing toilet never triggers a reboot.
  - *`bridge_health` entity* (ok / degraded / recovering / needs power-cycle), localized in all 29 languages, plus
  `free_heap`, `min_free_heap` and `largest_free_block` diagnostics (bytes, disabled by default). Memory is
  sampled every 30 s; a final reason line is logged before any self-reboot.
- New maintainer guide `docs/MAINTAINERS.md`: this branch's components, how they fit together, the build and
generation pipeline, and how to reproduce each CI job locally before pushing. (The `series-250-bridge` branch is
a separate firmware with its own `docs/MAINTAINERS.md` and its own changelog.)

## v1.35.0

- Board packages for the **M5Stack AtomS3R** (`board-atoms3r.yaml`: 8 MB octal PSRAM, the screen shows the status
color, backlight through the LP5562) and the **Seeed XIAO ESP32S3** (`board-xiao-esp32s3.yaml`: 8 MB octal
PSRAM, the orange LED shows the status as a pattern, BOOT button pairs). Both build; not yet tried on the hardware.
- README: every model from the app is listed at the top (Mera Comfort and Classic, Tuma Comfort and Classic in all
article versions, Cama); the Cama was missing from the model table. The AquaClean Alba is listed as not supported
(the app's new platform, series 250, another protocol). The Cama test set is explained as a Geberit trial unit,
not a model; the README now says that the three trial period sensors belong to it.
- README: at least 8 MB flash is needed (firmware about 3.2 MB, two OTA slots).

## v1.34.0

- The *Language* select on the Bridge device is gone: the language is set only by `language:` in the YAML. A
choice made in the device used to be stored in flash and win over the YAML, which gave a mix of YAML-language
entity names and device-language option texts. A previously stored choice is now ignored. Home Assistant may
keep the old *Language* entity as unavailable; delete it there.

## v1.33.3

- *Last / average toilet visit duration* stayed unknown: the toilet keeps fewer user-detection entries than its
visit counter (317 visits; page 0 full, pages 10 and 12 answer empty). An empty page now makes the ESP go back
page by page to the last page with entries; the newest entry is the last non-zero one there.

## v1.33.2

- Statistics: the answer to a free request for counters, histograms and timers (`aquaclean_raw_request` with
procedure 49 / 4A / 4B) now always appears in the log and in *Last response*; before, only in discovery mode.
- *Last shower duration* and the other *last …* durations: when the newest entry is 0 (shower stopped right away,
or the entry is not written yet), the newest non-zero entry is shown instead of 0 s.

## v1.33.1

- Project name: **Geberit AquaClean Bridge for ESP** (it serves both the ESPHome and the MQTT route); repository
`geberit-aquaclean-bridge-for-esp` (the `github://` addresses in the example and the dashboard import follow).
New boot banner. The device names stay *Geberit AquaClean Bridge* and *AquaClean Bridge*.

## v1.33.0

**Renamed: Geberit AquaClean Bridge for ESPHome.** The ESP's device is now *Geberit AquaClean Bridge*
(`friendly_name` in the examples and the ready-made firmware; your own YAML keeps its name until you change it),
its group in the web interface *AquaClean Bridge*. New boot banner in the log.

**Apple Home (HomeKit), optional** (`homekit: enabled: true`, see README): the ESP itself is an Apple Home
accessory, without Home Assistant, with switches for shower, lady shower, dryer and odor extraction (only those
the toilet has).
- Espressif's esp-homekit-sdk, vendored in `homekit_sdk/` (non-commercial HAP); works next to ESPHome's
  libsodium (Ed25519 key pair and signing added in `homekit_ed25519.c`, random numbers from the hardware RNG).
- Translated switch names in the Home app (ConfiguredName); a name written by the Home app is put back.
- Setup QR code: a Home Assistant notification while not paired (native API route) and the button
  *Show HomeKit QR code*; the page `http://<ESP>/homekit` with the web server (MQTT route).
- Shower and lady shower only while someone is seated; a switch that did nothing returns to the real state
  after 6 s (20 s after switching off, while the arm retracts).
- `reset_pairings: true` forgets all pairings once after flashing.
- Quiet log: HomeKit's own messages and the memory line only at log level VERBOSE; all HomeKit log texts in
  all languages.

**One YAML for both routes:** `examples/atoms3-lite.yaml` with the substitution `route: api` or `route: mqtt`
(replaces `atoms3-lite-esphome.yaml` and `atoms3-lite-mqtt.yaml`); only the chosen route is built
(`packages/route_api.yaml` / `route_mqtt.yaml`). A configuration with both `api:` and `mqtt:` is refused.

**More free memory**
- Native API route without the web server (example and ready-made firmware): Home Assistant shows everything;
  `packages/web.yaml` is part of the MQTT route.
- AtomS3 Lite: Wi-Fi code from flash instead of IRAM and 6 instead of 10 static Wi-Fi receive buffers (about
  24 KB more free RAM).

**Toilet link**
- After a dropped connection Bluedroid could lose the stored toilet key (encryption then fails with 0x52 and
  every request logged `BTM_GetSecurityFlags failed`): the ESP restarts once to load the key again, and that
  error flood is silenced.

## v1.32.0

- Thirteen more languages (29 in all): Estonian, Croatian, Latvian, Lithuanian, Romanian, Russian, Slovak, Slovenian, Serbian, Ukrainian, Chinese, Korean (`ko`, also `kr`) and Hebrew (`he`, also `iw`). Terms from Geberit's translations in the Geberit Home app; Hebrew from geberit.co.il and Geberit Israel's AquaClean brochure.
- Russian, Ukrainian, Chinese, Korean and Hebrew entities keep the key of the English name, like Arabic.

## v1.31.0

- Ten more languages: Polish, Czech, Hungarian, Danish, Swedish, Norwegian (`nb`, also `"no"`), Finnish, Portuguese, Arabic and Turkish — entity names, option texts, remote control buttons and the log. Terms from Geberit's own translations in the Geberit Home app; Arabic (not in the app) is our own translation.
- Arabic entities keep the key of the English name (`object_id` / entity key), so entity ids stay unique and readable; with MQTT their names stay English.
- Web installer and GitHub Actions build firmware for all 16 languages.

## v1.30.1

- New boot banner in the log (ESP32 board with the Geberit logo, "Geberit AquaClean ESP Proxy").

## v1.30.0

- Usage statistics as sensors under AquaClean Toilet (models of the classic AquaClean family: Sela, Mera, Tuma), each only when the model has the function: operating time (total / in operation, hours), showers and lady showers total, with oscillation, dryer, odor extraction, seat heating, lid lifter, cleanings, toilet visits, system flushes, use per profile, and duration of the last and average of the last 25 showers, lady showers and toilet visits (seconds). Read every hour and 5 s after the user leaves.

## v1.29.0

- New diagnostic button "Read statistics" (discovery mode): reads the toilet's statistics the way the Geberit Home app does — 26 usage counters (proc 49), 8 histograms with 5 levels each (proc 4a) and 7 timers (proc 4b) — and writes them to the log with names. Read-only; nothing is reset. Sensors follow once it is clear which values the Sela fills in.

## v1.28.3

* Sela light mode as in the app: *Off* 0, *On* 1, *Automatic* 2, each with the time windows off; *Time-controlled*
  is 2 with time windows (kept, or the app's defaults 06:00-08:00 and 21:00-23:00). Before, time-controlled was
  written as value 3, which the toilet does not use, and the select showed "On" or "Automatic" while the app
  showed time-controlled. Setting a time window while the mode is automatic shows time-controlled.

## v1.28.2

* Settings the app writes are logged (*App sets … -> value*), including the active common layer (0b), and
  shown in Home Assistant at once. For finding out how the app's light modes map to the toilet's values.

## v1.28.1

* Physical maintenance is shown with a long press of the **maintenance key** on the remote control (event
  `maintenance_long`) instead of −. Texts, warning and README follow.

## v1.28.0

**Entities per model.** Every entity now names the capabilities it needs, and every model has an exact list of
capabilities (`docs/models.md`), taken from the device definitions in the Geberit Home app (data points per
model), checked against the model overview and the Sela manual. The build stops on an entity without a
classification. After the feature discovery an entity exists only when the detected model has all its
capabilities; an unknown model gets only what every model has. The old rule ("the toilet answers the id")
let through functions the Sela does not have, because the Sela answers ids of functions it lacks.

Changes on the Sela:
* *Stop shower and dryer* is *Stop shower* (models without a dryer); *Arm cleaning status* is gone (only the
  Mera reports it).
* *Light sensor sensitivity* is back: the Sela manual describes it for the light mode *Automatic*. Removing it
  in v1.25.2 was wrong.
* *Descaling status* and *Descaling time left* moved to *Physical maintenance*; *Registration* to the toilet.
* Remote control entities only on models with a remote control (not the Tuma Classic).

Options follow the model: light mode (Sela: off, on, automatic, time-controlled; Mera Comfort: off, on, on
approach, in the dark), descaling steps (three variants), spray intensity (Cama: 3 levels). New models: Cama
(146.34) and Cama test set (146.30).

## v1.27.1

* Fix: while physical maintenance was hidden, Home Assistant showed an empty device with the name of the ESP.
  The API always sends the full device list (fixed length); the removed entry went out empty. The hidden
  device's slot now points to another device. An empty device left over in Home Assistant can be deleted there.

## v1.27.0

* Physical maintenance is shown with a **long press of − on the remote control** instead of a confirmation code
  and a switch: someone has to be at the toilet. `maintenance_disclaimer_accepted: true` is still needed;
  `maintenance_confirmation_code` is no longer used (ignored with a warning). The warning comes as a Home
  Assistant notification at every activation.
* No restart any more with the ESPHome API: the ESP closes the API connections and Home Assistant connects again
  and loads the shown or hidden entities. The MQTT route still restarts.
* Hidden again after 60 minutes, another long press of −, the switch (only there while shown) or a restart.

## v1.26.0

* New device **Physical maintenance** with the procedures that need someone at the toilet (descaling start /
  continue / cancel, unlock shower, arm cleaning, spray calibration, lid calibration and opening angle, filter
  replaced, draining, arm position adjustment, offsets). Hidden after every start of the ESP; it can only be
  switched on with `maintenance_disclaimer_accepted: true` and the confirmation code from the warning
  notification in the YAML, and then shows for one boot, at most 60 minutes. The ESP refuses the maintenance
  commands while it is off. Information about maintenance (next descaling, error code, arm cleaning status,
  filter days) stays with the toilet.

## v1.25.2

* *Light sensor sensitivity* belongs to a new function *Light sensor* (Mera Comfort) and is hidden on the Sela,
  which answers the setting id although it has no light sensor. Before it was only disabled by default, which
  does not affect an entity Home Assistant already knows.
* A changed function of an entity now also starts the feature discovery again after an update.

## v1.25.1

* Requests checked against the decompiled Geberit Home app: 11 and 13 are node info and firmware SAP number
  reads (not subscriptions), 81 is the Bluetooth node version, the layouts of 0e, 82 and 86 are confirmed.
  Context 01 procedure 44 sets the toilet's real-time clock in UTC (not used). Names in the code and
  `docs/protocol.md` corrected; `docs/SOURCES.md` updated. The descaling statistics (45) are confirmed
  against the Sela.

## v1.25.0

* Source references removed from all code comments (C++, Python, language files); they are collected in the new
  `docs/SOURCES.md`, with what each source was used for and where.
* Examples refer to v1.25.0 (they still pointed to v1.15.0).

## v1.24.5

* Clock test removed now that the cause is fixed: the *Toilet clock history* sensor, the clock jump watch with
  its context, the restart check, the read-back after setting, the time zone line (RTC id 13, which the Sela does
  not answer) and the log lines for app clock writes. What stays: after connecting the ESP reads the clock and
  sets it only when it is off by more than two minutes (*Setting the toilet clock*, *Toilet clock off by*).

## v1.24.4

* **Fix: the ESP wrote UTC to the toilet clock after every restart.** After a software restart the ESP32 keeps
  its time but applies the time zone only once Home Assistant (or SNTP) has synced; the ESP set the clock before
  that, with UTC as "local" time, and corrected it a little later. The clock history showed it: *after
  connecting +3600 s* (the toilet was right, the ESP was not). The ESP now reads and sets the toilet clock only
  after the first time sync. The toilet keeps its local time across disconnects.

## v1.24.3

* New diagnostic sensor *Toilet clock history*: the last five clock events with their local time (after
  connecting, set, after setting, off by, jumped, app sets, changed during restart). The first seconds after a
  restart never reach the log viewer, this sensor keeps them.
* After setting the clock the ESP reads it back at once (*Toilet clock after setting: +0 s*).

## v1.24.2

* After connecting the ESP first reads the toilet clock and logs it (*Toilet clock after connecting: -3600 s from
  local time*), then sets it only when it is off. Before, it set the clock right away, so the log never showed
  what the clock was after a restart or reconnect, and the restart check compared with its own fresh write.

## v1.24.1

* Pre-flush, automatic flush and *Flush* are shown only with a flush trigger module (status 64 bit 0, connected
  SSM modules). The Sela answers their setting ids (value 0) but has no such module.
* *Light sensor sensitivity* is disabled by default: the Sela answers the id but has no light sensor.
* Feature discovery reads each id once: the profile probe used to be read once per profile entity (12 times),
  which could cause a timeout.
* Counter entities are confirmed by their own id in the counter list (before: by the descaling counter).
* *Functions* lists all functions with entities (the "without known ids" part is obsolete since v1.24.0).

## v1.24.0

Every status, command and setting of the Geberit Home app 2.14.2 data point table (`docs/protocol.md`).

* Status: dryer running, descaling status (idle, preparing, waiting for user, descaling, flushing) and minutes
  left, last error code, arm cleaning status, spray calibration; disabled: draining, connected modules,
  lid / shower arm / dryer arm offset (read only in discovery mode).
* Buttons (command 09): toggle dryer, stop shower and dryer, start / continue / cancel descaling, unlock shower,
  start arm cleaning and next step, toggle odor extraction and its run-on, open/close lid, calibrate lid,
  lid opening angle (adjust, larger, smaller, save); disabled: flush, drain pump and pipe, shower / dryer arm
  position adjustment.
* Settings: seat heating on/off, pre-flush, WC water heating (session, basic and user profiles), automatic flush
  (off, on, pre-flush), light sensor sensitivity, accessibility mode.
* Counters: showers until unlock, descaling requested since (UTC); disabled: trial period end and remaining.
* Registration level (proc 55: not registered, private, public).
* Home Assistant profiles store setting ids 0a-0d (dryer intensity and the new switches) in a separate store, so
  existing profiles keep their data.
* Status values are published at the first answer after a start (before: only on a change).
* Entities follow their model function (dryer, descaling, spray arm, lid, odor extraction, seat heating);
  settings without a known model are shown only when the toilet answers their id.

## v1.23.0

Based on the data point table of the Geberit Home app 2.14.2 (see `docs/protocol.md`).

* New: *Dryer intensity* (level 1-5) as session setting and in the basic and user profiles (models with a dryer).
* New: *Days until filter change* and a *Filter replaced* button (models with odor extraction).
* The counter list also asks the RTC of the toilet (id 13, UTC); the log shows the time zone the toilet uses
  (*Toilet time zone: UTC+3600 s*) whenever it changes.
* *Last descaling* is UTC, not local time (DP 0x1004C is a UTC timestamp).

## v1.22.5

* Toilet clock watch: every read of counter 0b is compared with the previous one. A jump of more than 30 s is
  logged as a warning with what happened just before it (toilet connection, app state and last request,
  last frame of the remote control). An app that sets the clock is logged with its offset from UTC.
* After a software restart the ESP checks whether the toilet clock changed while it was away (kept in RTC memory).

## v1.22.4

* The device signal carries its profile number: *Device signal user profile N*.

## v1.22.3

* Two sensors were both called *Signal*: now *Toilet signal* (AquaClean Toilet) and *Device signal* (user
  profile). Entity ids in Home Assistant stay the same.

## v1.22.2

* README: installation step to allow the ESP to perform Home Assistant actions (for *Show IRK*).

## v1.22.1

* Docs: README (entities, IRK for all device kinds, timeouts, bond watchdog), protocol notes on link quality,
  the lost security record and the toilet clock.

## v1.22.0

* Log obfuscation (substitution `log_obfuscate: "true"`): Bluetooth addresses, user names, linked Home
  Assistant devices, the serial number (proc 82) and keys show only their last four letters or digits
  (`••:••:••:••:33:5E`, `•arry`), for logs in a public issue. ESPHome's own lines (Wi-Fi, BLE address, IP)
  and the entity states that the log viewer shows (`[S]` lines) are not changed: copy the log from the
  ESP's web interface, which has no `[S]` lines.
* IRK notification: "the device of barloew (smartphone, smartwatch, wearable or tracker)" instead of "the phone".

## v1.21.1

* Bond watchdog: when the bond with the toilet disappears while the connection stays up, Bluedroid has no
  security record for it any more and logs `BTM_GetSecurityFlags failed for device [toilet]` on every request,
  2-4 times a second for hours. The ESP now checks the bond every 30 s and connects
  again when it is gone, which creates the record again.

## v1.21.0

* IRK no longer shown in full: the *IRK* sensor shows only its last four digits (`•••••••• 5e0f`), and the log
  of a capture too. A new button *Show IRK* per profile sends the whole key to Home Assistant as a
  notification (title "IRK of barloew", the key on its own line to copy), like ESPHome's "Show encryption key".
  This needs *Allow the device to perform Home Assistant actions* in the options of the ESPHome integration;
  without it (or in the MQTT route) the key goes to the log only.
* Log translations: format codes are now really checked at build time; a translation that differs is left
  out with a warning.

## v1.20.3

* Banner just before "Boot seems successful" instead of after it.

## v1.20.2

* Web interface group names follow the language by themselves (`web_toilet`, `web_session`, `web_profile`
  default to `auto`); set them only for names of your own. `web_session` / `web_profile` can be removed from
  existing YAML.

## v1.20.1

* Default name of the main device (`friendly_name`) in the examples and the ready-made firmware: *Geberit
  AquaClean ESP Proxy*. The web interface group keeps the short name *AquaClean Proxy*.

## v1.20.0

* Log in the chosen language: the messages of this component (about 210) appear in the language of the
  language select, ESPHome's own messages stay English. The code keeps the English text; the translations are
  in `components/geberit_aquaclean/lang/log/<language>.json` (English -> translation) and are checked at
  build time against the format codes of the English text. Still English: the configuration dump at
  startup and the decoded device data (raw protocol detail).

## v1.19.1

* Banner in the log right after "Boot seems successful".

## v1.19.0

* *Connected since* is now the moment of connecting (timestamp) instead of a number of seconds: Home Assistant
  shows "3 minutes ago", "5 hours ago", "2 days ago" by itself, and it is published once instead of every
  minute. Home Assistant may report that the old statistics of this sensor no longer fit: delete them there.
* Timeouts: an unanswered request is asked once more before it counts as a timeout; a single unanswered status
  poll is left to the next poll (500 ms later). In a log, 657 single missed status polls in
  seven hours (about 1 %, mostly in one hour), never two in a row, were all counted and logged as warnings.

## v1.18.4

* A user with only a captured IRK (no Home Assistant device) was never recognized: advertisements were only
  looked at when some profile had a device or a Home Assistant link.
* IRK capture works on iOS.

## v1.18.3

* IRK capture, three fixes after the first test (nothing appeared on the phone):
  * the services are built step by step: ESPHome hands GATT events over later, by then the handle list of a
    table creation is cleared (handles 0x0000);
  * the scanner pauses while the random address is set (Bluedroid refuses it while scanning);
  * the IRK is read from Bluedroid's bond list after the pairing: ESPHome drops `ESP_GAP_BLE_KEY_EVT`.
* Docs updated accordingly.

## v1.18.2

* IRK capture: the services could not be created ("GATT table not created (133)"): Bluedroid allows 8 GATT
  services by default and the ESP already has 7. Raised to 16 (`CONFIG_BT_GATT_MAX_SR_PROFILES`). A failed
  capture now ends at once (toilet advertisement back) instead of after two minutes, and the next attempt
  creates the services again.

## v1.18.1

* Scan boost paused during an IRK capture (radio time for advertising and pairing).

## v1.18.0

* **IRK capture** per user profile (*Capture IRK*): the phone pairs once with the ESP (as a heart rate sensor
  for iPhone / Apple Watch, or as a keyboard for Android with *Capture IRK as Android keyboard*) and hands over
  its Identity Resolving Key. The ESP stores it in the profile and resolves the phone's random addresses
  itself (AES, Bluetooth `ah` function): recognition no longer waits for Home Assistant after an address
  change, and the Geberit Home app is recognized from Android phones too. The key is shown under *IRK* for
  Home Assistant's Private BLE Device. Idea after irk-capture by Derek Seaman / Kyle Teal (MIT), rewritten
  for Bluedroid.
* GATT server attribute limit raised to 150.

## v1.17.1

* Toilet clock: was only set when Home Assistant's time was already known at connect (usually not after a
  boot), otherwise not for 24 hours. It is now set as soon as the time is known, after every app connection
  (the Android app sets UTC), and whenever counter 0b is more than two minutes off.
* Docs: the daily rollover of the counters happens at night, not at midnight (around 01:45).

## v1.17.0

* No more flapping *Recognized user*: once recognized, a user stays recognized while his device is within
  15 dB below the threshold, while he is seated, and up to 30 s (was 15 s) without such a signal.
* Scan boost at most 60 s in one go, then a 2 minute pause: a phone lying just outside the range no longer
  keeps it on.

## v1.16.1

* Scan boost stops as soon as the user is recognized (was: 20 s after the last sighting near the threshold).

## v1.16.0

* Faster welcome: when a linked device is seen up to 20 dB below the recognition threshold, the ESP scans
  faster for 20 s (scan window 30 -> 160 ms of every 320 ms), so the step over the threshold is noticed
  within about a second instead of several seconds.
* The welcome light goes before everything else in the request queue (a settings refresh no longer delays
  it) and comes before the profile load.

## v1.15.1

* Documentation: README (devices and entities, users and recognition, Geberit Home app, logging),
  `docs/protocol.md` (counter list 59, midnight rollover confirmed, light activation 3, answers of 05/13/55/86,
  app adding window and app addresses).

## v1.15.0

* Two deployment routes, each with its own example: `examples/atoms3-lite-esphome.yaml` (ESPHome
  integration) and `examples/atoms3-lite-mqtt.yaml` (MQTT). `examples/atoms3-lite.yaml` is gone.
* **Breaking:** the API part (actions, Home Assistant clock) moved from `packages/aquaclean.yaml` to the new
  `packages/api.yaml`. Route 1 adds `api: !include .../packages/api.yaml` to its packages.
* MQTT route: SNTP clock (`timezone`), and linking a profile to a Home Assistant device through HA's
  `mqtt_statestream` (`mqtt_statestream` substitution, default `statestream`).

## v1.14.0

* `packages/web_auth.yaml`: password on the web interface (digest authentication, `web_username`,
  `web_password`).
* `packages/mqtt_tls.yaml`: encrypted MQTT with certificate check (`mqtt_ca`, port 8883).

## v1.13.3

* Adding a second phone in the app: the app-adding window (long press +) closed at once when another app was
  already connected. Only an app that connects during the window closes it now. A missed app disconnect is
  also caught on the GATT server side, so *Geberit Home app connected* no longer sticks.
* Less log and recorder noise: user signal at most every 30 s (or 10 dB change), *Connected since* every
  60 s, profile entities only when they change, device data (proc 59 etc.) at INFO only when the content
  changes (59 entry 0b, the toilet clock, ignored).

## v1.13.2

* Orientation light activation: *Time-controlled* (value 3, uses the time windows) is back in the list.

## v1.13.1

* *Without descaling the shower stops in* (AquaClean Toilet, diagnostics, disabled by default): days until
  descaling plus the grace period (proc 45) after which the shower is restricted.

## v1.13.0

* Profile keys 1-3 of the remote control recognize the user of that profile for two minutes (welcome light,
  no profile load: the toilet does that itself). Sitting down in that time, or already seated, makes him
  the active user.

## v1.12.0

* A user is also recognized when the Geberit Home app connects from his or her phone (its address matches
  the linked Private BLE Device or fixed address), whatever the signal strength. The welcome light is
  shown; the profile is not loaded, as the app loads its own user profile into the session. The user stays
  recognized while the app is connected.

## v1.11.1

* Device identification (article number, model, serial number, dates, firmware) is asked again when the
  toilet does not answer right after connecting (3 times, then at every settings refresh until known).

## v1.11.0

* Optional packages: `web.yaml` (web interface with the entities grouped like the devices, log, OTA from
  the browser), `mqtt.yaml` (MQTT with Home Assistant discovery), `provisioning.yaml` (Wi-Fi via Improv and
  the hotspot *AquaClean Setup*, dashboard adoption).
* Ready-made firmware per language (`firmware/`) and a GitHub Actions workflow that builds it and publishes
  an ESP Web Tools install page (`static/index.html`) on GitHub Pages.

## v1.10.0

New layout of the devices in Home Assistant:

* **AquaClean Proxy** (the ESP, friendly_name): connection, pairing of toilet and remote control, language,
  recognition threshold, discovery mode, remote control diagnostics.
* **AquaClean Toilet**: orientation light, descaling, default settings ("Default ..."), device settings and
  all toilet diagnostics, including the link signal (now from the component; the package sensor is gone).
* **Active session**: status, shower commands, session settings, activate/store buttons of profiles 1-6,
  remote control and app, recognized and active user.
* **User profile 1-6**: profile settings, user, Home Assistant device, welcome light, recognized, seated,
  signal, clear.

Removed: *Advertise as toilet* (always on), *Search for Bluetooth devices* and the *Bluetooth device*
selects, *Read device info*, *Device data*, *Counters*. Several diagnostics are disabled by default.
Names without the device name in them; the old devices *Shower session*, *Default settings*, *Device
settings* and *Remote control* can be deleted in Home Assistant.

## v1.9.3

* Link script: first the user profile (1-6), then the Bluetooth device; no device = clear the profile. The
  script finds the profile's field itself (max length 100 + profile number), whatever the language.

## v1.9.2

* A user recognized right after a (re)start or a reconnect: the profile and the welcome light wait until the
  profile and the light color/brightness have been read (at most a minute); before, the profile was
  activated with 0 settings and the orientation light was skipped.

## v1.9.1

* All six user profiles always exist; *Add user profile* is gone, *Delete profile* became *Clear profile*
  (name, devices, welcome light; profiles 4-6 back to the default settings), without a restart.
* Per profile *Signal* (dBm of the linked device as the ESP receives it) to see recognition and to set the
  threshold; the log shows the address / iBeacon Home Assistant sends for a linked device tracker.
* The link script writes the profile's *Home Assistant device* field (both chosen from drop-downs); no
  action or node name to fill in.

## v1.9.0

* A user can be linked to a device Home Assistant already knows: a Private BLE Device (the ESP follows its
  current address) or an iBeacon (uuid/major/minor), also as found by Bermuda. Per profile *Home Assistant
  device* (entity id); the script `homeassistant/aquaclean_link_user.yaml` offers both as drop-downs.
  The ESP measures the distance itself and restarts once after a change. The device search stays as a
  fallback for trackers with a fixed address.

## v1.8.0

* User profiles 1-3 are the toilet's (profile keys of the remote control); profile 4 of the toilet is no
  longer shown. *Add user profile* creates profiles 4-6, which only exist on the ESP (settings, activate,
  store, delete); creating or deleting restarts the ESP once so Home Assistant shows the change.
* Per profile: *User* (name), *Bluetooth device*, *Welcome light* (off or one of the 7 toilet colors),
  *Recognized* and *Seated*. Global: *Recognized user*, *Active user*, *Search for Bluetooth devices*,
  *Recognition signal strength* (default -60 dBm, about 1 m).
* *Search for Bluetooth devices* (20 s, hold the device next to the ESP) lists the devices with a fixed
  identity (address of a tracker, iBeacon of the Home Assistant Companion app) strongest first in every
  *Bluetooth device* select; the ESP restarts once so Home Assistant gets the new options.
* When a user's device comes above the threshold: the profile is loaded into the session settings (not
  while somebody is seated, at most every 5 min per user) and the welcome light blinks twice: the
  orientation light in the chosen color at full brightness (then back as it was) and the board LED.
  Seated while recognized = *Active user*.

## v1.7.0

* Automatic discovery: without `toilet_mac` the ESP takes the Geberit toilet with the strongest signal
  (manufacturer data `.. 01 .. "146.."`, 10 s search) and stores it; without `remote_mac` the remote
  control that pairs in the pairing window is used and stored. New: *Toilet address*, *Search for the
  nearest toilet*, *Address* of the remote control; *Unpair* also forgets a learned remote control.
* *Showers today* (was *Shower count*): counter 00 is reset at midnight; on a day with showers counter 01
  (days until descaling) goes down by one.

## v1.6.1

* The Sela answers proc 45 (descaling statistics); it is read with the counters. New sensors *Last
  descaling* and *Number of descalings*.
* The stored feature set has a version mark, so an update can always recognize it (v1.6.0 restarted once
  because of a misread model).

## v1.6.0

Procedure names and layouts from reverse engineering:

* *Demo mode* (common setting 09) as a switch.
* Mera / Tuma Comfort functions, untested, shown only when the model has them and the toilet answers:
  seat heating (07), dryer temperature (08), dryer (09), odor extraction (00) per session, default settings
  and user profile; automatic lid open/close (common 06/07), lid sensor range (04), odor extraction
  run-on (05).
* *Firmware* sensor (proc 0e, component 1 = main controller); SOC version (81) shown as 10.18.
* Descaling statistics (proc 45) are requested and decoded when the toilet answers.
* Feature discovery handles up to 128 entities; after an update the stored model keeps the entities of
  other models hidden, so Home Assistant does not see them come and go.

## v1.5.1

* Sensor *Shower count* (counter 00 of list 59, confirmed with two showers); counters are read again at the
  end of every shower.

## v1.5.0

* Per user profile: *Activate* (copies the profile into the session settings, as the profile keys of the
  remote control do) and *Store current settings* (session settings into the profile).

## v1.4.1

* Lady shower spray arm position: 5 positions (raw 0-4), as the app and profiles 2-4 use; values above 2
  were rejected before.
* Session settings can be written without somebody seated, as the app does with its user profile.

## v1.4.0

* User profiles 1-4: the default settings per profile (`53/54 <profile> <id>`, profile 0 = default
  settings; the Sela answers profiles 1-4 and nothing from 5). One sub-device per profile, shown only for
  the profiles the toilet answers. Profile values read or written by the app appear at once.
* Raw request action: context and procedure are hex strings now, like args (`procedure: "53"`).

## v1.3.0

* Feature discovery after the first connection: model from the article number (table of the Mera Comfort,
  Mera Classic, Sela, Tuma Comfort, Tuma Classic and AquaClean 4000), every setting id read once, only the
  entities of the functions the toilet has are shown. Stored; one restart when the set changes.
* New entities: *Model*, *Functions* (diagnostic), *Rediscover functions*.
* The toilet clock is set in local time, like the app (it was set in UTC).

## v1.2.0

* Device information as separate sensors: device type, article number, serial number, production date,
  commissioning date (proc 86, the "Bedrijfstelling" in the app).
* Sensor *Next descaling in* (days, counter 01 of list 59; matches the app).
* Discovery mode logs every request of the app with its arguments.

## v1.1.0

* Device info: article number, serial number, production date and description (proc 82, read on connect).
* Large transfers (type 1/2 frames) are received and acknowledged.
* Discovery: *Read device info* (82, 81, 05, 0e, 13, 59), *Device data*, *Counters* (59 list, read with the
  settings); settings scan also reads the active common values (0a) and only lists unknown ids with values.

## v1.0.0

First release, based on the proof of concept `aquaclean_sela` v7.7.

* One permanent connection to the toilet; the ESP is paired with the toilet as remote control.
* Remote control paired with the ESP, screens and buttons passed through (full menu).
* Geberit Home app via the ESP: the ESP advertises the app-adding state after a passed-through long press
  of +; app frames are relayed, own requests are slipped in after each app transaction (< 0.2 s delay).
* Home Assistant keeps status and control while the app is open; settings read or written by the app appear
  in Home Assistant immediately.
* All entities created by the component, grouped in sub-devices, names in en/nl/de/fr/it/es following
  Geberit's terms; option texts switchable at runtime.
* Orientation light as a light entity (on/off, color, brightness), writes to the active layer (`0b`).
* Discovery entities for other models: raw status, advertisement, procedures seen, settings scan, raw
  requests.
* Package for the M5Stack AtomS3 Lite with status LED and pairing button.
