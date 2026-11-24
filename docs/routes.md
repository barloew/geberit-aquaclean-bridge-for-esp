# Connection routes & install methods

Two independent choices decide how the bridge fits your setup. Neither excludes any system — pick what
suits you.

## Connection route (how it talks to your controller)

- **Native API (ESPHome).** The native API connects directly, encrypted, with auto-discovery — to Home
  Assistant (the ESPHome integration) and to other controllers that speak it, such as Homey (its ESPHome app)
  and ioBroker. The Noise key is set on the bridge's `/setup` page (web installer) or in YAML.
- **MQTT.** The bridge publishes to an MQTT broker — Home Assistant, openHAB, Node-RED, Domoticz and others.
  Best when you don't use the native API or prefer a broker-based setup. The broker and time zone are entered
  on the device's own web page.
- **Apple HomeKit (optional, alongside either route or stand-alone).** Every image can also be an Apple Home
  accessory, for the Home app and Siri — even stand-alone, without any other controller. It is compiled into
  every image and stays off until you turn on the bridge's **Apple Home** switch. See the README's
  [Apple Home](https://github.com/barloew/geberit-aquaclean-bridge-for-esp#apple-home-homekit-optional) section.

Both routes expose the same functions and entities. Choose one; you can switch later.

They differ only in how Home Assistant groups the entities into devices: the native API route splits them
into separate devices (toilet, active session, maintenance, user profiles 1–6), while the MQTT route
currently shows all entities under one device. A per-sub-device split over MQTT is **planned** — it will be
turned on once ESPHome releases its opt-in MQTT sub-device support (`sub_device_topics`, PR #20353; approved,
not yet merged). Until then, pick the native API route if the separate-devices layout matters to you.

## Install method (how the firmware gets onto the ESP)

- **ESPHome (own YAML build)** — full control, needs an ESPHome toolchain.
- **Web installer (prebuilt firmware)** — flash from the browser, then enter settings on the device page.
- **Home Assistant ESPHome add-on** — build and flash from within Home Assistant.

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

## What to expect

- Series 248 (Sela, Mera Comfort classic family, …): fully supported.
- Series 250 (Alba): **not** supported by the main component; a separate experimental
  firmware lives on the `series-250-bridge` branch.
- When reporting an issue or a field test, say which **route** and **install method** you use — it changes
  how we read your logs.
