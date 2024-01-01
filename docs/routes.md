# Connection routes & install methods

Two independent choices decide how the bridge fits your setup. Neither excludes any system — pick what
suits you.

## Connection route (how it talks to Home Assistant)

- **Native API (ESPHome).** The ESPHome native API connects directly to Home Assistant, encrypted, with
  auto-discovery. Best when you run Home Assistant and want the tightest integration. A few non-HA systems
  can also use the native API (see **Homey** below).
- **MQTT.** The bridge publishes to an MQTT broker. Best when you don't use the ESPHome native API, or you
  connect to something other than Home Assistant, or you prefer a broker-based setup. The broker and time
  zone are entered on the device's own web page.

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

- **Native API (easiest).** Use a Homey ESPHome app (e.g. "ESPHome" / "ESPHome Controller") to talk to the
  bridge directly over the native API, locally, no cloud. Needs **Homey Pro (2023) or newer** (or Pro mini /
  self-hosted server), firmware 13+, the native API enabled (`api:` with an encryption key), and both on the
  same network. Entities map to Homey capabilities; anything without a match becomes a generic `esphome_*`
  capability. These are community apps, so maturity varies.
- **MQTT (platform-neutral, more manual).** Point the bridge's MQTT route at your broker and subscribe with
  Homey's **MQTT Client** app, mapping topics to Homey devices/flows by hand. Note: Homey's "Home Assistant
  Discovery" (in the MQTT Hub app) publishes Homey's *own* devices outward — it does **not** ingest the
  bridge's HA-style discovery topics, so there is no auto-discovery in this direction.

Rule of thumb: Homey Pro → native API via the ESPHome app; otherwise → MQTT Client with manual mapping.

## What to expect

- Series 248 (Sela, Mera Comfort classic family, …): fully supported.
- Series 250 (Alba): **not** supported by the main component; a separate experimental
  firmware lives on the `series-250-bridge` branch.
- When reporting an issue or a field test, say which **route** and **install method** you use — it changes
  how we read your logs.
