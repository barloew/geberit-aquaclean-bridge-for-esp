# Connection routes & install methods

Two independent choices decide how the bridge fits your setup. Neither excludes any system — pick what
suits you.

## Connection route (how it talks to your controller)

- **Native API (ESPHome).** The native API connects directly, encrypted, with auto-discovery — to Home
  Assistant (the ESPHome integration) and to other controllers that speak it, such as Homey (see [Homey](#homey))
  and ioBroker (see [ioBroker](#iobroker)). The Noise key is set on the bridge's `/setup` page (web installer) or in YAML.
- **MQTT.** The bridge publishes to an MQTT broker — Home Assistant, openHAB, Node-RED, Domoticz and others.
  Best when you don't use the native API or prefer a broker-based setup. The broker and time zone are entered
  on the device's own web page.
- **Apple HomeKit (optional, alongside either route or stand-alone).** Every image can also be an Apple Home
  accessory, for the Home app and Siri — even stand-alone, without any other controller. It is compiled into
  every image and stays off until you turn on the bridge's **Apple Home** switch. See the README's
  [Apple Home](https://github.com/barloew/geberit-aquaclean-bridge-for-esp#user-content-open-apple-home-homekit-optional) section.

Both routes expose the same functions and entities. Choose one; you can switch later.

They differ only in how Home Assistant groups the entities into devices: the native API route splits them
into separate devices (toilet, active session, maintenance, user profiles 1–6), while the MQTT route
currently shows all entities under one device. A per-sub-device split over MQTT is **planned** — it will be
turned on once ESPHome releases its opt-in MQTT sub-device support (`sub_device_topics`, PR #20353; approved,
not yet merged). Until then, pick the native API route if the separate-devices layout matters to you.

## Install method (how the firmware gets onto the ESP)

- **ESPHome (own YAML build)** — full control, needs an ESPHome toolchain. **Start from
  [`examples/atoms3-lite.yaml`](../examples/atoms3-lite.yaml)** (pick your board, and `route: api` or
  `route: mqtt`). On the MQTT route the broker, user, password and topic prefix are **substitutions** in that
  file — set them there. Do **not** copy a `firmware/*.yaml` file for an own build: those are the web-installer
  images and include `packages/mqtt_setup.yaml`, which takes the broker from the ESP's web page and ignores the
  YAML `mqtt_broker` substitution, so the broker would look unset. `mqtt_setup` belongs to the web installer
  only.
- **Web installer (prebuilt firmware)** — flash from the browser, then enter the broker and other settings on
  the device's web page (`packages/mqtt_setup.yaml`).
- **Home Assistant ESPHome add-on** — build and flash from within Home Assistant; it is an own-YAML build, so
  follow the ESPHome bullet above (start from `examples/atoms3-lite.yaml`, broker in substitutions). You can
  pull the packages straight from GitHub (`github://barloew/geberit-aquaclean-bridge-for-esp/...@<ref>`), so
  no local checkout is needed.

The install method is a matter of preference and of the system you connect to; it does not change what the
bridge can do.

## Homey

The bridge needs no Homey-specific firmware — you integrate it from the Homey side via one of the two
routes:

- **Native API with the AquaClean Bridge app (most complete, Homey Pro).** A dedicated Homey app
  ([`homey/`](../homey)) that talks to the bridge over the native API — locally, encrypted, no cloud — and
  gives every standard entity its own flow cards (triggers, conditions and actions; user-profile settings
  have a profile picker), in all 13 Homey languages. Not in the Homey App Store yet and not yet validated on
  hardware; feedback is welcome. Install it yourself, see [Install the Homey app](#install-the-homey-app).
- **Native API with a generic ESPHome app.** Homey's ESPHome apps also talk to the bridge over the native
  API. The current **ESPHome Controller** app needs **Homey OS 13+** on a Homey Pro (2023) or newer, Pro
  mini, or self-hosted server; for an older Homey Pro (2016–2019) the older **ESPHome** app works too (Homey
  OS 5+, less actively maintained). Entities map to Homey capabilities; anything without a match becomes a
  generic `esphome_*` capability. These are community apps, so maturity varies.
- **MQTT (platform-neutral, more manual).** Point the bridge's MQTT route at your broker and subscribe with
  Homey's **MQTT Client** app, mapping topics to Homey devices/flows by hand. Note: Homey's "Home Assistant
  Discovery" (in the MQTT Hub app) publishes Homey's *own* devices outward — it does **not** ingest the
  bridge's HA-style discovery topics, so there is no auto-discovery in this direction.

Rule of thumb: Homey Pro → native API via the AquaClean Bridge app (or an ESPHome app); otherwise → MQTT
Client with manual mapping. Connect one Homey app to the bridge, not several: every native-API client costs
the ESP memory.

### Install the Homey app

Homey Pro only (2016–2019, 2023 or newer, Pro mini, self-hosted server); Homey (cloud) cannot install apps
from outside the App Store. You need a computer on the same network as the Homey.

1. **Bridge on the native API route.** Install the native-API firmware and save an API encryption key on the
   bridge's `/setup` page — or, in your own YAML, set `api:` with an `encryption: key:`. Keep the key at hand.
2. **Download the app.** Get `aquaclean-homey-app-<version>.zip` from the
   [Homey app releases](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/releases?q=homey&expanded=true)
   and unzip it.
3. **Install the Homey CLI.** Install [Node.js](https://nodejs.org) (LTS), then in a terminal:
   `npm install -g homey` and `homey login` (sign in with your Homey account).
4. **Install the app.** In the unzipped folder run `homey app install` and pick your Homey. The app then
   stays on the Homey, also when the computer is off.
5. **Add the bridge.** In the Homey app: *Devices* → *+* → *AquaClean Bridge*. A bridge found on the network
   is pre-selected (otherwise enter its host name or IP address); paste the encryption key and tap
   *Connect*. The flow cards are then under the AquaClean Bridge device.

An app installed this way gets **no automatic updates**: for a new version, download the new zip and run
`homey app install` again — your device and flows stay. If the install stops with "Homey seems to be
offline", make sure the computer and the Homey are on the same network.

## ioBroker

The bridge needs no ioBroker-specific firmware — you integrate it from the ioBroker side via one of the two
routes:

- **Native API with the AquaClean Bridge adapter (most complete).** A dedicated ioBroker adapter,
  [ioBroker.aquaclean-bridge](https://github.com/barloew/ioBroker.aquaclean-bridge) (category *household*;
  source in [`iobroker/`](../iobroker)), that talks to the bridge over the native API — locally, encrypted, no
  cloud — and turns every standard entity into a state with a fixed, language-independent id, the right role,
  unit and range, and names in all 11 ioBroker languages. Selections are numbers with labels, the remote
  control buttons arrive as a state, written values are confirmed by the bridge, and the instance settings find
  bridges on the network. Published on npm as `iobroker.aquaclean-bridge` and on its way into ioBroker's
  *latest* repository; not yet validated on hardware, feedback is welcome. See
  [Install the ioBroker adapter](#install-the-iobroker-adapter).
- **Native API with the generic ESPHome adapter.** ioBroker's ESPHome adapter also talks to the native API. It
  doesn't know the bridge, so state ids, roles and names follow what the firmware sends, in the firmware's
  language.
- **MQTT (platform-neutral).** Point the bridge's MQTT route at your broker and use ioBroker's **MQTT** adapter
  (as broker or client). The states follow the bridge's topics, named after the entities in the firmware
  language; there is no discovery-based device model as in the adapter above.

Rule of thumb: native API via the AquaClean Bridge adapter; MQTT if you already run everything through a
broker. Connect one native-API client to the bridge, not several (not the AquaClean Bridge adapter and the
ESPHome adapter at the same time): every client costs the ESP memory.

### Install the ioBroker adapter

Needs Node.js 22 or newer and js-controller 7.0.7 or newer.

1. **Bridge on the native API route.** Install the native-API firmware and save an API encryption key on the
   bridge's `/setup` page — or, in your own YAML, set `api:` with an `encryption: key:`. Keep the key at hand.
2. **Install the adapter.** In ioBroker Admin: *Adapters* → search for *AquaClean Bridge* → install. The
   adapter list shows it once it is in ioBroker's *latest* repository (switch Admin to the *latest/beta*
   repository under *System settings* to see it before it reaches *stable*). Until then, use the GitHub icon
   (*Install from custom URL*) → `https://github.com/barloew/ioBroker.aquaclean-bridge`.
3. **Configure the instance.** Pick the bridge from the list (or enter its host name or IP address), paste the
   encryption key and save. The instance turns green once it is connected; the states are under
   `aquaclean-bridge.0`.

The adapter's [README](https://github.com/barloew/ioBroker.aquaclean-bridge#readme) describes every state, with
examples for scripts and the remote control buttons ([German](https://github.com/barloew/ioBroker.aquaclean-bridge/blob/main/docs/de/README.md)).
Report problems with the adapter here, in this repository's [issues](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/issues/new/choose)
(pick *ioBroker* as home automation system), and ask questions in
[Discussions](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/discussions) — not in the adapter
repository, which only holds the published copy. One instance per bridge; for a second toilet, add a second
instance.

## What to expect

- Series 248 (Sela, Mera Comfort classic family, …): fully supported.
- Series 250 (Alba): **not** supported by the main component; a separate experimental
  firmware lives on the `series-250-bridge` branch.
- When reporting an issue or a field test, say which **route** and **install method** you use — it changes
  how we read your logs.
