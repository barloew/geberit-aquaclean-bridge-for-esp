# Sources

The code itself carries no source references; this document lists where the knowledge in it comes from and
where it is used. The protocol internals derived from these sources are kept in the maintainer's private
store, outside this repository; [`protocol.md`](protocol.md) is the public, function-level overview.

## Third-party work

| Source | Used for | Where |
|---|---|---|
| **irk-capture** by Derek Seaman and Kyle Teal, MIT license, <https://github.com/DerekSeaman/irk-capture> | The idea of IRK capture: advertise briefly as a heart rate sensor (iPhone, Apple Watch) or as a keyboard (Android) with characteristics that need encryption, so the phone pairs and hands over its Identity Resolving Key. Rewritten for Bluedroid; no code copied. | `irk_capture.cpp` |
| **Geberit Home app 2.14.2** (`com.geberit.home`), static analysis of the decompiled .NET assemblies (`Geberit.ComLib.Bluetooth`, `AcDataPointDefinitionFactory`), supplied by the user | The data-point table behind the toilet's status readouts, commands, settings, profiles and counters, the enum meanings, and the UTC/local timestamp handling | `__init__.py` (entities), `hub.cpp` |
| **Geberit Home app 2.14.2**, own reading of the decompiled `Geberit.ComLib.Bluetooth.dll` (`Legacy.AquaCleanProduct`, `AcDeviceIdentification`, `AcNodeInfo`) | Every fixed request the app sends and the shape of each answer: node and device information, firmware versions, the firmware SAP number, and the initial-operation date | `hub.cpp` (device data) |
| **Geberit data point list** (the Mera's functions, value ranges unvalidated) | Candidate functions of other models | Mera / Tuma Comfort entities in `__init__.py` |
| **Geberit AquaClean model overview** | Article number prefixes per model and the functions of each model | `MODELS` and feature tables in `__init__.py`, feature discovery in `hub.cpp` |
| **Lucide** icons (`lucide-static`), ISC license, <https://lucide.dev> | The icons on the installation step cards, embedded in the SVGs; also usable in the overview (`lucide:` icons) | `images/steps/*.svg`, `tools/steps/generate.py`, `tools/overview/generate.py` |
| **Inter** typeface by The Inter Project Authors, SIL OFL 1.1, <https://github.com/rsms/inter> | Text measurement and PNG rendering of the overview infographic (bundled as a Latin subset; not embedded in the images) | `tools/overview/fonts/`, `tools/overview/generate.py` |
| **tzdata** (IANA time zone database, public domain), via ESPHome and aioesphomeapi | The POSIX rules of the time zones of the web installer firmware (both routes), parsed at generation time | `packages/timezone.yaml`, `tools/gen_firmware.py` |
| **Bluetooth Core Specification**, Vol 3 Part H 2.2.2 | Resolving private random addresses: `ah(k, r) = e(k, 0^104 ‖ prand) mod 2^24`; checked with the specification's test vector (IRK `ec0234a357c8ad05341010a60a397d9b`, prand `708194`, hash `0dfbaa`) | `users.cpp` |
| **Geberit AquaClean Sela user manuals** | Terms per language: Dutch 968.684.00.0 (09), English 968.681.00.0 (09), German 968.337.00.0 (07), French 968.682.00.0 (09), Spanish 968.689.00.0 (08), Italian short instructions 968.706.00.0 / 971.175.00.0 (menu names missing there translated in the same style) | `lang/*.yaml` |

## Own recordings and observations (AquaClean Sela)

| What | Used in |
|---|---|
| GATT tables of the toilet and the remote control | `hub.cpp`, `remote_link.cpp` |
| Sniffle recordings of the toilet's advertisement and scan response | `remote_link.cpp` (advertising as the toilet) |
| Decrypted recording of the toilet pairing with the remote control | `toilet_side.cpp` (GATT server layout of the remote control) |
| App traffic: the requests after connecting, the acknowledgement of large answers, the device-identification answer, and the frame checksum | `hub.cpp` |
| Test: a stored setting changes the light only after it switches on again; the active layer changes it at once | `hub.cpp` (`write_setting`) |
| Counters: showers today and days until descaling, rollover once a night | `hub.cpp` (counter list) |
| The app writes its user profile as session settings when connecting; profile keys of the remote control copy a toilet profile into the session; the commissioning date is read on connect | `hub.cpp`, `__init__.py` |
| Bluedroid loses the bond with the toilet while connected and logs `BTM_GetSecurityFlags failed` for hours | `toilet_side.cpp` (bond watchdog) |
| After a software restart the ESP32 applies the time zone only after the first time sync; before v1.24.4 the ESP therefore wrote UTC to the toilet clock | `hub.h`, `hub.cpp` (clock) |
| The Sela answers some setting ids of functions it does not have (pre-flush, automatic flush, light sensor sensitivity) | `__init__.py` (flush entities need the flush module) |

## Translations

* The terms of the Polish, Czech, Hungarian, Danish, Swedish, Norwegian, Finnish, Portuguese, Turkish, Estonian, Croatian, Latvian, Lithuanian, Romanian, Russian, Slovak, Slovenian, Serbian, Ukrainian, Chinese and Korean texts come from the translation files Geberit ships in the Geberit Home app 2.14.2 (`Home.Core.dll`, embedded resources `Loca.<language>.json`: shower, lady shower, spray intensity, spray arm position, orientation light, descaling, water hardness, dryer, seat heating, odor extraction, remote control, …). Where the app's wording is clearly wrong (Polish "Perlator" for the lady shower) the regular term is used ("Natrysk damski").
* The app has no Arabic; the Arabic texts are our own translation, with "الشطف" for flushing as in Geberit's Arabic installation manuals.
* Hebrew (not in the app): the terms of geberit.co.il (Geberit AquaClean pages) and Geberit Israel's AquaClean brochure: אסלה שוטפת, שטיפה לנשים, זרוע שטיפה, מייבש אוויר חם, שאיבת ריחות, חימום למושב האסלה, תאורת התמצאות, שלט רחוק, מכסה האסלה, הדחה, הסרת אבנית.

- QR codes (HomeKit setup code): `components/geberit_aquaclean/qrcodegen.c/.h`, QR Code generator library by Project Nayuki (MIT), https://www.nayuki.io/page/qr-code-generator-library
