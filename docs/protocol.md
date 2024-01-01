# Geberit AquaClean Sela – Bluetooth protocol notes

Reverse engineered with Sniffle recordings. The detailed working notes (Dutch) live in the
author's protocol document; this is the summary the component relies on.

## Advertising

| Phase | Manufacturer data (AD type ff) |
|---|---|
| ~6 s after power-on | `00` (short) |
| normal | `00 01 00 31 34 36 32 32` ("14622") |
| pairing window for the remote control (after power-on) | `aa 01 00 31 34 36 32 32` |
| open for adding in the app (after a long press of + on the remote control) | `00 01 01 31 34 36 32 32` |

Service UUID 0x3EA0 (incomplete list), scan response name `Geberit AC PRO`, interval 62.5 ms. The toilet stops
advertising while a client is connected and accepts one connection at a time.

## App protocol (unencrypted, toilet = GATT server)

Service `3334429d-90f3-4c41-a02d-5cb3a03e0000`; a1/a2 write, a3/a4 write, a5–a8 notify.

Frames are 20 bytes. Header byte: bits 7–5 type (0 data, 3 control, 4 info, 1/2 large transfers), for type 0
bit 0 = first frame with the frame count in bits 2–1, otherwise the index. `11` = single frame,
`13` + `12` = two frames (the second on a2). Control frames `70 00 08 00 <bitmap>` acknowledge received
frames; there are no sequence numbers.

Message: `<04 request | 05 response | 06 notify> ff <len16 BE> <crc16 BE> <body>`,
body `01 <context> <procedure> <n> <args>`. CRC: start 0x1234, see `crc16_` in `hub.cpp`.

| Procedure | Meaning |
|---|---|
| `0d 0c 00 01 … 0b` | status of indexes 0–11 (`<idx> <LE32>` each): 0 seated, 1 shower, 2 Lady shower, 9 orientation light |
| `09 <cmd>` | command: 00 shower, 01 Lady shower, 14 orientation light toggle |
| `07 <id>` / `08 <id> <LE16>` | session settings: 01 oscillation, 02/03 spray intensity, 04/05 arm position, 06 water temperature |
| `53 00 <id>` / `54 00 <id> <LE16>` | default settings (same ids); 53 0N = user profile N |
| `51 <id>` / `52 <id> <LE16>` | common settings: 00 water hardness, 01 light brightness, 02 light color, 03 light activation (0 off, 1 on, 2 automatic, 3 time-controlled: assumed, uses the windows 0c-0f), 0b run-on time |
| `0b <id> <LE16>` | active value of a common setting (the light uses this one; the app writes it only for 03) |
| `57 <id>` / `58 <id> <LE32>` | schedules: 0b clock (unix time), 0c–0f light time windows (minutes, ffff = off) |
| `11`, `13` | subscriptions the app sends after connecting |
| `82`, `81`, `0e` | device information (large transfers) |

## Remote control (encrypted, Just Works with bonding)

The remote control is a GATT server; the toilet connects to it on every button press and, after encryption,
subscribes to 0x001e (buttons, notify 0x001d `<code> <state>`) and 0x0022. Screens are written to 0x0024 as
three chunks `<offset 00/13/26> <data>` (UTF-16LE text and menu markup), status bytes to 0x002a.

Button codes: 01 shower, 02/03 spray −/+, 04/05 arm back/forward, 06 Lady shower, 09–0b user profiles,
0d right, 0e left, 0f down, 10 up, 12 oscillation, 13 orientation light, 16 OK, 17 temperature, 18 home,
19 maintenance; state 00 pressed, 01 long, 02 released.

## The toilet as client of the remote control

When the ESP is connected as central and offers the remote control's GATT services, the toilet reads the
Device Information model number (`3.60.101.900/0001` for the remote, `iPhone…` for the app) to tell the two
apart. It pairs only in its power-on window and drops an unencrypted "remote control" within a second, so a
bonded ESP encrypts right after connecting. Over that encrypted link the app protocol keeps working.

## Geberit data points

Geberit's source code names functions as data points (DP); the list we have covers the Mera, the model with
the most features. The DP numbers are not the ids used on the BLE app protocol of the Sela, and the value ranges
in that list are unvalidated estimates; the observed values below take precedence.

| DP | Name | On the Sela (observed) |
|---|---|---|
| 563 / 568, 868 | start/stop shower / Lady shower | command `09 00` / `09 01` |
| 564 / 872 | shower / Lady shower status | status index 1 / 2 (0/1 seen) |
| 570, 571 | active shower spray intensity | session 02, 0-4 |
| 572, 573 | active spray arm position | session 04, 0-4 |
| 574, 575 | active shower water temperature | session 06, 0-5 (0 = heating off) |
| 576, 577 | active spray arm oscillation | session 01, 0/1 |
| 858 | active Lady spray intensity | session 03, 0-4 |
| 580-583 | stored intensity / arm position / temperature / oscillation | default (53/54) 02 / 04 / 06 / 01 |
| 42, 43 | orientation light LED (state / set) | status index 9 / command `09 14` (toggle) |
| 44 | orientation light mode | common 03, 0-2 (off, on, automatic) |
| 48 | orientation light intensity | common 01, 0-4 |
| 382 | LED color | common 02, 0-6 (color index, not RGB) |
| 587 | water hardness | common 00, 0-2 |
| 15 | RTC time | schedule `0b`, unix time |

Candidates for the unknown status indexes 3-8, 10, 11 and for settings not yet mapped: descaling status and
progress (585, 586), days until descaling (589), spray arm cleaning status (567), shower progress (565, 873),
global error / warning (359, 360), shower time (849-851), flush settings (119, 126, 127). Check them with
discovery mode (raw status, settings scan) while starting the matching function.

Features of other models in the same list (not on the Sela, to be mapped with discovery mode on such a model):
dryer (874-895: start/stop, status, progress, fan intensity, heater temperature), odor extraction (20-29, 88),
pre-flush (118, Mera), maintenance done/status (474, 475, Mera), seat heater errors (819, Mera Comfort /
Tuma Comfort), RGB LED color and brightness in percent (340, 341, 382) where the Sela uses a color index and
five levels.

## Data points of the Geberit Home app 2.14.2

Source: static analysis of the decompiled .NET assemblies of the Android app (`Geberit.ComLib.Bluetooth`,
`AcDataPointDefinitionFactory`), supplied by the user. Every data point has an index (DP `0x1_00xx`) and a
category; the category decides the procedure (RPC number), and **the id on the line is the index minus the
first index of its block**. This matches every id observed on the Sela.

| Block (first DP) | Read / write proc | Ids |
|---|---|---|
| commands (0x04) | – / 09 | 00 shower, 01 Lady shower, 02 dryer, 03 stop shower and dryer, 04 start spray arm cleaning, 05 next cleaning step, 06 start descaling, 07 continue descaling, 08 cancel descaling, 09 reset the shower unlock counter, 0a lid open/close, 0b lid calibration, 0c odor extraction toggle, 0d odor run-on toggle, 14 orientation light toggle, 21-24 lid open angle (start, save, +, -), 25 full flush (interface module), 2f odor filter replaced, 34 drain pump and pipe |
| status (0x3C) | 0d / – | 00 user seated (shower allowed), 01 shower, 02 Lady shower, 03 dryer running, 04 descaling step, 05 descaling minutes left, 06 last error (shown on the remote and the status light), 07 service (0 ok, 1 spray arm, 2 dryer arm, 3 both not cleaned), 08 spray calibration running, 09 orientation light on, 0a draining, 64 connected SSM modules (bit 0 flush trigger, 1 odor extraction, 2 orientation light), 68-6a lid / shower arm / dryer arm offset |
| maintenance counters and time (0x48) | 59 (list), 57 / 58 | 00 showers since the last descaling-credit calculation (about daily), 01 days until descaling, 02 showers allowed after the request, 03 showers until unlock, 04 last descaling (UTC), 05 first request after it (UTC), 06 number of descalings, 07 days until filter change, 0b user's local time, 0c-0f light time slots 1/2 start/end (minutes), 11 end of trial (UTC), 13 RTC (UTC), 56 / 5a remaining trial period regular / extended (signed) |
| active settings (0x5C) | 07 / 08 | 00 odor extraction, 01 oscillation, 02 / 03 shower / Lady intensity, 04 / 05 shower / Lady arm position, 06 water heating (0 off, 1-5), 07 seat heating (0 off, 1-5), 08 dryer heating (0 off, 1-5), 09 dryer starts automatically, 0a pre-flush, 0b seat heating on/off, 0c WC water heating on/off, 0d dryer air intensity (0-4 = level 1-5) |
| device settings (0x6B) | 0a / 0b, stored 51 / 52 | 00 water hardness (soft, medium, hard), 01 light brightness, 02 color (blue, turquoise, magenta, orange, yellow, warm white, white), 03 light mode, 04 lid detection range, 05 odor run-on, 06 / 07 lid auto open / close, 08 auto flush (off, on, pre-flush), 09 demo mode, 0a light sensor sensitivity, 0b light run-on (5, 10, 20, 30, 60 min), 0c accessibility mode |
| profile settings (0x7A) | 53 / 54 `<profile>` | the same ids as the active settings 00-0d; profile 0 = basic settings, 1-4 = user profiles |
| registration (0x97) | 55 / 56 | 0 not registered, 1 private, 2 public |

Since v1.24.0 every id in this table has an entity (commands as buttons). The Sela answers status ids 00-0b;
which of the new ids it really uses shows in Home Assistant after the feature discovery.

The Sela answers session/profile id 0a (pre-flush) and device ids 08 (automatic flush) and 0a (light
sensor sensitivity) with a value, although it has neither a flush module nor a light sensor; an answer alone
does not prove a function. It does not answer counter ids 07-0a, 11, 13, 56 and 5a.

**Sela light modes** (app): the app shows four modes, the toilet stores three values. Off = 0,
on = 1 and automatic = 2, each with all four time windows (57/58 ids 0c-0f) set to ffff; time-controlled = 2
with time windows (app default 06:00-08:00 and 21:00-23:00). Value 3 is not used. The app writes the mode to
52 and 0b, then the four windows to 58.

All enums are 0-based (level 1 = 0). Light mode has three variants in the app: off/on/approach/in the dark,
off/on/approach and off/on/auto; the Sela's value 3 (time-controlled in the app) is not named there.
Descaling step (status 04): 0 idle, 1 preparing, 2 waiting for the user, 3 descaling, 4 flushing; on the Sela,
unlike the Mera, *flushing* means canceled and *descaling* is also used during the flushing step.

## User profiles

Default settings carry a profile byte: `53 <profile> <id>` reads, `54 <profile> <id> <LE16>` writes.
Profile 0 holds the default settings, 1-4 the user profiles (Sela: four, as in the specifications). A
profile that does not exist answers empty (`53 05 03` → no value). How a profile is activated (profile
keys of the remote control) is not known yet on the app protocol.

The app's user profile is kept in the app, not in the toilet: when connecting, the app writes its values
as session settings (`08`), also when nobody is seated; profiles 1-4 in the toilet stay unchanged
Profiles 2-4 hold 2/2/0/3/3/1 (factory values), profile 1 the values set on the remote.

A profile key of the remote control (short press) copies that profile into the session settings: after
profile 2 the session read 2/2/0/3/3/1, after profile 1 2/2/1/2/2/1. No message on the app
link, the toilet does it internally. The ESP does the same with `08` writes. A long press stores the
current session settings in that profile (confirmed by the user).

Counter `00` in list 59 is the number of showers today (see below).

## Requests of the Geberit Home app 2.14.2

From the decompiled `Geberit.ComLib.Bluetooth.dll` (`Legacy.AquaCleanProduct`): every request the app sends,
as node / context / procedure. Data point requests (07-0b, 0d, 51-59) take their procedure from the data point
table above; the others are fixed.

| Context | Proc | Function | Answer layout |
|---|---|---|---|
| 01 | 05, 06 | node ids | count + node ids (max. 128) |
| 01 | 0e | node firmware versions, request `<count> <node ids, 12 bytes>` | 61 bytes: count + 12 × (`<node> <char> <char> <build LE16>`); Sela: 1 = RS08 TS57, 15 = RS05 TS33 |
| 01 | 11 | node info, request `<count> <4 node ids>` | 53 bytes: count + 4 × (`<node>` + 12 characters) |
| 01 | 13 | firmware SAP number, request as 11 | as 11, e.g. `0f "000.000.00.0"` |
| 01 | 44 | **set the real-time clock** of a node: UTC seconds since 1970 (LE32) | — (not used by the ESP) |
| 01 | 57 / 58 | read / write time and trial data points (the user's local time 0b, light time slots) | `<id> <LE32>` |
| 01 | 81 | Bluetooth node version | 4 bytes: two characters + LE16, `"10" 0x0012` = 10.18 |
| 00 | 01 | enter bootloader | |
| 00 | 02 / 03 / 05 | save operation data / load default operation data | |
| 00 | 44 / 45 | read / reset error segment of a node (`<segment>`) | |
| 00 | 49 / 4a / … | statistics counters / histograms / timers (`<index>`) | |
| 00 | 80 | node identification | |
| 00 | 82 / 83 | read / write device identification | 82 bytes: article number 12, serial number 20, production date 10, description 40 (ASCII, padded) |
| 00 | 84, 86 / 85, 87 | read / write the initial operation date (node, device) | 10 ASCII characters `DD.MM.YYYY` |
| 00 | 88 / 89 | read / write device special functions | |
| 40 | … | bootloader and flashing (other product generation) | |

The app does not send context 01 procedure 45. The ESP uses it for the descaling statistics: the Sela answers
16 bytes, `<showers today> <days until descaling LE16> <grace days LE16> <showers until confirmation>
<last descaling LE32> <first request LE32> <number of descalings LE16>` (the counters 00-06 of list 59 in one
answer). In context 00 the same procedure resets an error segment, so it must not be sent there.

Profile setting ids and common setting ids: see the data point table above.

Sela answer to 45: `02 6700 0e00 01 00000000 00000000 0000` = 2 unposted showers, descaling
in 103 days, shower restricted 14 days after that, 1 shower until confirmation, never descaled. Counters
00-03 of list 59 are the same four values; 04-06 (all 0) are probably the last descaling, the last
prompt and the number of descalings.

Counter 00 = showers today. At midnight, if it is above 0, counter 01 (days until descaling) goes down by
one and 00 starts at 0 again: descaling is counted in days of use (assumed from consecutive-day observation).

Confirmed: counter 00 goes back to 0 and, when it was above 0, counter 01 goes down by one
once a night, **not at midnight**: around 01:45 local time (102 -> 101, showers 1 -> 0). The toilet
clock was in UTC that night (00:45 toilet time), set by the Android app; whether the rollover is tied to
the toilet clock (e.g. 00:45) is still open.

### Statistics 49 / 4a / 4b (context 01, read on the Sela)

* `49 <n>` → LE32 counter n: 0 uptime total (s), 1 in operation (s), 2 standby (s; 1 + 2 = 0), 3 shower total, 4/5 via control panel / remote control, 6 lady shower total, 7/8 panel / remote, 9 instant heater only, 10/11 oscillation shower / lady shower, 12 dryer, 13 odor extraction, 14 seat heating, 15 lid lifter, 16 descaling, 17 cleaning, 18 users, 19 system flush, 20-23 profile 1-4, 24/25 shower / lady shower via the app. On the Sela 3 = 4 + 5 + 24.
* `4a <n>` → 5 × LE32 + 1 byte: how often each level (1-5) was used. 0/1/2 shower position / pressure / temperature, 3/4/5 the same for the lady shower, 6 dryer temperature, 7 seat heating temperature. The sum is the counter of the function.
* `4b <n> <page>` → 25 × LE16 + 2 bytes: one entry per use in seconds, the oldest first, 25 per page; the number of entries is the counter (0 shower → 3, 1 lady shower → 6, 4 user detection → 18). Byte 50 > 0 = last page, byte 51 > 0 = last timer (the app stops there). 0 duration shower, 1 lady shower, 2 dryer, 3 seat heating, 4 user detection (sitting time), 5/6 delay before the shower / lady shower.
* The ESP reads the counters with a sensor every hour and 5 s after the user leaves, then the page of each timer with the newest entry (and the page before it for the average). When the page after the newest entry is not empty the timer is not shown.

### Counter list 59

Answer: `<count> (<id> <LE32>)*`. Sela:
`08 | 00 00000000 | 01 66000000 | 02 0e000000 | 03 01000000 | 04 0 | 05 0 | 06 0 | 0b <LE32>`

| id | Meaning |
|---|---|
| 00 | showers today (reset once a night, around 01:45 local) |
| 01 | days of use until descaling (= 45) |
| 02 | grace period after that, days (14) before the shower is restricted |
| 03 | showers until confirmation (1) |
| 04-06 | 0: probably last descaling, last prompt, number of descalings |
| 0b | the user's local time (DP 0x10053, TimestampLocal), seconds since 1970; counts up every second (the app asks list 59 every second while open). Kept as an offset to the RTC (13), so writing local time is right. The toilet keeps it across disconnects. The UTC value seen after ESP restarts was the ESP's own write: after a software restart the ESP32 keeps its UTC time but applies the time zone only once Home Assistant / SNTP has synced, so it wrote UTC as "local" time (fixed in v1.24.4). Earlier observations of a UTC toilet clock (ascribed to the Android app) may have the same cause |
| 07 | days until the odor extraction filter must be replaced (DP 0x1004F; not on the Sela) |
| 13 | RTC of the toilet, UTC (DP 0x1005B, set in the factory) |

### Other answers of the Sela

| proc | Answer |
|---|---|
| 05 | `01 0f …` node inventory; bytes around offset 75 change between reads (state, not decoded) |
| 13 | `02 01 … 0f "000.000.00.0"` (version of component 15?) |
| 55 | `00`: not registered |
| 86 | `DD.MM.YYYY`: commissioning date |

## App adding and app addresses

A long press of + on the remote control (passed through) makes the ESP advertise byte `ADV_APP` = 01 for
60 s. The window closes when an app connects during it, not because of an app that was already connected
(fixed in v1.13.3: a connected iPhone blocked an Android phone). Both iOS and Android connect with a
resolvable random address (e.g. `7F:A2:…`, `4E:D6:…`); recognizing the phone needs its IRK, which Home
Assistant's Private BLE Device resolves into `current_address`. The app does not bond ("Deleting unpaired
device" in the Bluedroid log after disconnect). A missed GATT client disconnect is caught by the GATT server
disconnect.

## IRK capture

For the capture the ESP stops the toilet advertisement and advertises with a fresh random static address
as "AquaClean IRK" (appearance 0x0340, service 0x180D heart rate) or "AquaClean Keyboard" (0x03C1, HID
0x1812). All characteristics need encryption, so the phone pairs (Just Works, IO capability none, bonding).
The phone's identity key is read from Bluedroid's bond list after `AUTH_CMPL` (`bond_key.pid_key.irk`, LSB
first; ESPHome does not pass `ESP_GAP_BLE_KEY_EVT` on). The random address can only be set while nothing
scans, so the scanner pauses for a moment; the services are created with the classic calls because ESPHome
delivers GATT events later, after the handle list of `create_attr_tab` is cleared. Stored and shown MSB first (32 hex digits), the form Home Assistant's Private BLE Device takes and
the AES key of `ah`: hash = AES-128(IRK, 13 zero bytes || prand)[13..15], address = prand || hash with the
top two bits of prand 01 (checked with the test vector of the Core specification: IRK
ec0234a357c8ad05341010a60a397d9b, prand 708194 -> hash 0dfbaa). After the key the ESP disconnects, removes
the bond and advertises as the toilet again.

## Link quality and Bluedroid

* Missed answers: in seven hours about 1 % of the status polls (proc 0D, every 500 ms) stayed unanswered,
  never two in a row, mostly in one hour; settings reads (07/53/57) now and then. Since v1.19.0 a request is
  asked once more before it counts as a timeout. Signal of the toilet at the ESP: -68 to -80 dBm.
* Lost security record: for hours Bluedroid logged `BTM_GetSecurityFlagsByTransport: BTM_GetSecurityFlags
  failed for device [toilet]` on every GATT request (2-4 a second) while the link kept working unencrypted:
  the device record of the toilet was gone while connected. The start was not in the log; most likely the
  bond with the toilet (paired with the ESP as remote control) was removed. Since v1.21.1 the ESP checks the
  bond every 30 s and connects again when it disappears.
* Toilet clock: after every start the toilet clock was 3601-3602 s behind local time (UTC), also after the
  ESP had set it. Not solved yet: either the toilet ignores the write or keeps UTC itself.
