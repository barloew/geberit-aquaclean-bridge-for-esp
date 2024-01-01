# Field test plan — Series 248 (ESPHome component)

A standard plan so results from different testers and hardware are comparable. Run the steps that apply
to your model (skip a function your model does not have — mark it **N-A**), and post your results in the
**Field testing** Discussions category. Mark each step **PASS / FAIL / N-A** and attach a DEBUG log.

## 0. Environment (note in your report)

- Model + article number, ESP board, bridge version.
- **Connection route**: Native API (ESPHome) or MQTT.
- **Install method**: own ESPHome build, web installer, or HA add-on.
- **Controller**: Home Assistant, Homey, or other. See [routes.md](routes.md) for routes, controllers and install methods.

## 1. Pairing & discovery

1. Bridge powers up and finds the toilet (`Connected to the toilet`).
2. Bridge finds the remote control and advertises as the toilet (link section in the log).
3. Model and article number are detected and match your unit.
4. The *Found functions* list matches what your model can do.

## 2. Connection route

**Native API:** the device appears in your controller (Home Assistant, or Homey via its ESPHome app),
entities populate, state changes within a few seconds of acting on the toilet/remote.

**MQTT:** the broker + time zone are entered on the device web page; the device connects to the broker;
entities/topics appear in your controller or MQTT client; state changes propagate both ways.

## 3. Core control (skip what your model lacks)

For each: change it from HA **and** from the remote, and confirm both sides reflect the new state.

- Shower on/off, shower strength, shower-arm position.
- Lady shower (strength, position).
- Water temperature; oscillating shower.
- Dryer — confirm it only starts **while seated**, from every source.
- Odor extraction (+ run-on behavior).
- Orientation light: on/off, color, brightness, light-sensor sensitivity (if present).
- Seat heating (if present).

## 4. Profiles & user recognition

1. Profiles 1–3 are readable on the bridge and match the toilet.
2. Change a default (e.g. default water temperature) and confirm it sticks.
3. User recognition: a known phone/IRK recognizes the user; up to 6 users.
4. Seated + recognized → shown as the **Active user**.

## 5. HomeKit (only if you use it)

1. Pairing with the shown setup code succeeds.
2. Function switches behave as expected (available only while seated, where that applies).

## 6. Statistics & stability

1. Statistics populate (shower counts, durations, visits, descaling counters).
2. Run for ~24 h: note timeouts (target: 0) and any disconnects.
3. Reboot the ESP and the toilet; confirm it reconnects and rediscovers cleanly.

## Reporting

Start a post in the **Field testing** Discussions category (it opens a form), select route + install method,
and paste your per-step results and log.
