# How this bridge compares to the other AquaClean integrations

*An objective look at how the Geberit AquaClean Bridge for ESP (Series 248 main bridge +
the experimental Series 250 bridge) relates to the two best-known alternative projects, and
why its architecture is a different shape.*

This is written for two readers: someone choosing between the projects, and someone who
already runs one of the others and is wondering whether to try this one. It is not a
takedown. The other projects did real pioneering protocol work and deserve credit for it.
But this project's protocol knowledge is its own — independently reverse-engineered and
verified against real device captures, not ported from their code — so this is a comparison
of peers, not of a project standing on the others' shoulders.

## The three projects at a glance

| | **This project** (barloew) | **jens62/geberit-aquaclean** | **torp93/homey-geberit-aquaclean** |
|---|---|---|---|
| Lineage | Independent firmware; protocol independently reverse-engineered (not ported) | Builds on Thomas Bingel's C# library — Python port plus its own RE (incl. Series 250) | Node/Homey app; protocol work builds on jens62 |
| Where the logic runs | **On the ESP itself** (firmware) | On a host (Raspberry Pi / HA server / Python process) | On the Homey Pro |
| Role on the BLE link | **Man-in-the-middle bridge**: clones the toilet's identity, is central to the toilet and peripheral to the clients | **Client**: connects to the toilet like the app does | **Client**: connects to the toilet like the app does |
| What the ESP does | Runs the whole bridge | Optional dumb `bluetooth_proxy` (BLE-to-IP relay) | `bluetooth_proxy` (BLE-to-IP relay), required |
| Connection style | **Permanent** single link to the toilet, re-served to many clients | On-demand (connect ~1–2 s, release cycles) or persistent (with uncontrolled remote/app displacements) | On-demand by choice (connect ~2 s, release ~every 30 s idle); holds and contends during active use |
| Integration routes | **Two first-class routes: ESPHome native API *and* MQTT** | MQTT, plus HACS and a REST API / CLI | ESPHome native API (to Homey) |
| Home-automation reach | Home Assistant, Homey, ioBroker (native API); openHAB, Node-RED, Domoticz and anything MQTT (MQTT route) | Home Assistant, openHAB, anything MQTT | Homey only |
| Apple HomeKit / Siri | **Yes — shipping** (optional); bridge is an Apple Home accessory, works even stand-alone | No | No |
| Personalized experience | **Yes** — recognizes *who* approaches, auto-loads their profile, up to 6 users | No (status/control only) | No — knows someone is seated (seat sensor), not who |
| Maintenance & diagnostics | Full status + **gated physical-maintenance procedures** + broad diagnostics | Status, counters, per-setting read/write, annotated error text | Status, counters, settings, filter-counter reset |
| Deployment focus | **End-user**: browser installer flashes the *complete bridge* in 29 languages, auto-discovery, guided setup, visual feedback | Developer/host (config.ini, HACS, REST/CLI) | Homey pairing flow; browser installer for its ESP proxy |
| Series 248 (Mera etc.) | Sela verified on hardware; Mera, Tuma, Cama supported via feature discovery; real-time | Supported (mature) | Supported (mature, Mera Comfort verified) |
| Series 250 (Alba, encrypted) | Experimental branch, **not yet field-verified** | Read support via HACS, with a known remote-control conflict | Not supported |
| Cloud / account needed | No | No | No |

All three are local-only, need no Geberit account, and are MIT-licensed community work.

## The one architectural difference that drives everything

Every Geberit AquaClean accepts **one Bluetooth central at a time**, and its own remote is
deactivated whenever another central is connected — Geberit states this in the manuals
themselves. That single fact is the hinge the three designs turn on.

**The other two projects are *clients*.** They connect to the toilet the same way the phone
app does, read or write, and disconnect. Because only one central is allowed at a time, a
client that stays connected locks everything else out — so both projects connect only
briefly and release the link (jens62 calls it on-demand mode; torp93 reconnects roughly
every 30 s when idle). This is a sound, pragmatic design, and on the Mera family it works
well: you get polled status and full control, and the remote is only blocked for the second
or two the client is actually connected.

Its limits follow directly from the shape:

- Status is **polled**, not real-time. Between polls the toilet is unobserved; an event is
  seen on the next poll, not when it happens.
- The physical remote and the app are **briefly locked out** on every poll, because the
  client is the one central during that window.
- Truly *simultaneous* use of remote + app + automation isn't possible — the single
  connection slot is time-shared, not shared.
- A direct client depends on the host's or proxy's radio being in range and healthy; much
  of both projects' engineering is spent keeping that one fragile link alive (circuit
  breakers, recovery loops, proxy auto-restart).

**This project is a *bridge* (man-in-the-middle).** The ESP clones the toilet's Bluetooth
identity and holds the single permanent connection to the real toilet itself. The remote,
the app, home automation and HomeKit then connect to *the ESP*, which presents itself as the
toilet and relays. Nobody else ever contends for the toilet's one connection slot, because
only the bridge is ever connected to the toilet.

What that buys the user — and all of this is shipping today on the Series 248 bridge, not a
roadmap:

- **Real-time** state — the bridge is always connected, so it sees every change as it
  happens and pushes it out, no polling lag.
- **Everything connected at once.** Home automation, Apple HomeKit, the physical remote and
  one Geberit Home app all work simultaneously. While the app is open it takes priority, and
  commands from Home Assistant, MQTT and HomeKit keep working alongside it. This is
  coexistence delivered, not time-sharing one slot.
- **Two first-class integration routes, so the reach is wide.** The bridge speaks the
  **ESPHome native API** (Home Assistant's ESPHome integration, and anything else that
  speaks it — Homey via its ESPHome app, ioBroker) **and MQTT** (Home Assistant via MQTT
  discovery, openHAB, Node-RED, Domoticz, and anything else on an MQTT broker). You pick the
  route that fits your setup; both expose the same devices and entities.
- **Apple HomeKit and Siri, optionally and stand-alone.** The bridge itself becomes an Apple
  Home accessory. It can run with Home Assistant or MQTT, or completely on its own with no
  other home-automation system at all — control and voice straight from Apple Home.
- **Placement, and a radio dedicated to the bridge's own job.** The radio that matters sits
  in the ESP right at the toilet, not in a Pi or Homey across the room. Our firmware does use
  the radio to scan as well — to capture a user's IRK and for the presence monitoring behind
  recognition — so it is not literally doing only the one connection. The distinction is what
  that scanning *is*: a purpose-built, firmware-scheduled part of the bridge's own function,
  not ESPHome's general-purpose `bluetooth_proxy` service forwarding every nearby Bluetooth
  device to a home-automation hub and offering connection slots to arbitrary peripherals. The
  instability the other projects document is specifically in that generic proxy component's
  advertisement-subscription lifecycle after a disconnect — a moving part our integrated,
  single-purpose stack simply doesn't have.
- A small dedicated appliance: no always-on PC, Pi or BlueZ host to maintain.

Interestingly, the jens62 project independently concluded that this relay/bridge shape is
the *correct* answer to the coexistence problem and wrote up a detailed design for an
"AquaClean Hub" that does essentially what this firmware does — one permanent connection to
the toilet, impersonating it to many clients. In their project that design is documented but
**not yet implemented**; here it is the thing that already runs. On Series 248 this is not a
theory: the main bridge runs the man-in-the-middle relay in production today.

## The part no other project offers: a personal experience, not just control

The two alternatives are, by design, **status-and-control bridges**: they read the toilet's
state and send it commands. That is useful, and they do it well. But the toilet still behaves
the same for everyone who uses it — you still pick your user profile by hand. Where they do
report "a user", it is the toilet's own seat sensor saying *someone* is seated, not *who*.

This project turns the toilet into something **personal**, and that is the real added value
the architecture unlocks:

- **It recognizes who is approaching and loads their profile automatically** — before you
  sit down. No pressing a user key on the remote. Press shower on the remote, in Home
  Assistant, over MQTT or in Apple Home and it runs with *your* settings.
- **Up to six users** — twice the three the Geberit remote supports.
- **Recognition is by the person's own device** — phone, watch, wearable or tracker — and it
  keeps working even with randomized/rotating Bluetooth addresses, because the identity
  (the IRK) is resolved **on the ESP32 itself**, so address changes are picked up instantly.
  The key can be captured by the bridge directly or reused from one Home Assistant already
  knows.
- **A personal welcome light** — the toilet's own orientation light greets you in the colour
  you chose for your profile, which doubles as confirmation that your profile is loaded.
- **It knows who is actually using the toilet.** When a recognized person sits down they
  become the **active user** — an explicit state the bridge exposes alongside recognized and
  seated. Because the active user is known at the same time as the functions being used, you
  can attribute usage to a person: who used the toilet, which functions, how often. No other
  project can do this, because none of them knows *who* is present in the first place.

None of this is possible in a poll-and-release client model, and neither alternative attempts
it. It is a direct consequence of the bridge being permanently present at the toilet,
resolving identity locally, and sitting in the path of every client at once.

> **A note on privacy.** Per-person usage attribution is powerful, and not everyone wants to
> know their housemates' toilet habits in that much detail. This is inherent to a lot of home
> automation — presence, room occupancy and device usage all expose who-did-what — and the
> same judgement applies here: it is opt-in by whether you link a person's device and whether
> you act on the data. Worth stating plainly rather than hiding, but it is a capability, not
> something forced on anyone.

## Maintenance and diagnostics, done safely

Beyond status and control, the bridge surfaces the whole maintenance picture as entities, for
every route: next descaling, number of descalings, showers today, days until the filter
change, the error code and a toilet-fault alarm — all usable in automations ("descaling due →
notify", "fault → alert"). Only the entities your model actually has appear, through feature
discovery.

It then adds something the others do not: a separate **Physical maintenance** device —
start, continue or cancel descaling, unlock the shower, arm cleaning, spray- and
lid-calibration, filter-replaced, and draining on the models that have it. These act on the
toilet from your home automation system, but they are **gated behind an opt-in unlock at the
toilet with the remote**, and hidden again afterward, so a stray automation can never start a
descaling cycle or move an arm while someone is there. Alongside sit broad diagnostics for
troubleshooting: model, article and serial number, firmware version, signal strength,
reconnection count and last error.

The others expose status, counters and settings well; none exposes the physical-maintenance
*procedures* to home automation, and none gates them for safety the way this does.

## Deployment built for the person, not the developer

The architecture also lets this project treat **setup and administration as a product**.
jens62 leans on developer-style configuration (a `config.ini`, a HACS custom repository and
coordinator, REST/CLI); torp93 has made real strides here — it now offers a one-click browser
installer for its ESP *proxy*, with hand-flashing as the advanced path, after which you pair
in Homey. The difference is what gets flashed, and how far the guided flow reaches:

- **Flash the complete bridge from the browser** — not a relay you pair up afterward, but the
  whole bridge, ready-made and in your own language (29 of them), over USB in Chrome or Edge,
  with no ESPHome, Python or command line, for **either** route. The native-API route adds a
  single step: generate an encryption key on the bridge's own `/setup` page. ESPHome YAML
  stays available for extras like a web-page password or MQTT over TLS.
- **It finds things for you** — the toilet and the remote are discovered automatically; there
  is no hunting for a Bluetooth address.
- **Guided, visual setup** — a step-by-step flow, a two-minute "capture IRK" pairing step per
  user, adjustable recognition range, and light-blink feedback that tells you what happened
  without reading a log.

That focus on the owner's and administrator's experience is not a cosmetic layer bolted on
top; it is feasible precisely because the bridge is self-contained on the device, so setup,
identity and feedback can all live in one place the user actually touches.

## So which should someone use?

- **You have a Mera-family (Series 248) toilet and want the simplest path into Home
  Assistant or Homey, right now, from proven software** — all three are reasonable.
  torp93 is the most polished Homey-native option; jens62 is the most feature-complete for
  HA/openHAB and has the broadest tested model list. This project is the one that gives you
  real-time updates, two integration routes (native API *and* MQTT), Apple HomeKit, and a
  flash-the-whole-bridge-from-the-browser setup — and it already runs its bridge architecture
  in production on 248.

- **You want a personal, appliance-like experience** — the toilet recognizes who is
  approaching and loads their profile automatically (up to six people), the remote, the
  phone app, home automation and HomeKit all work at once with live state, and you flash it
  once from the browser and forget it — that is exactly what this project is built for, and
  the others are not shaped to do it.

- **You have a Series 250 Alba and want encrypted-protocol support** — be realistic: this
  is the frontier for *everyone*. jens62 can read Alba data over HACS but with a documented
  remote-control conflict; torp93 doesn't target Alba at all; and this project's Series 250
  bridge is promising in design but **not yet field-verified**. If you have an Alba, the most
  valuable thing you can do is help test.

## The honest part: where the others are ahead

Objectivity cuts both ways.

- **Maturity.** jens62 and torp93 are shipping software with real users and release
  histories. This project's Series 250 bridge is experimental and, at time of writing, not
  yet verified on real hardware. Our mature, proven piece is the Series 248 bridge.
- **Depth and annotation of protocol coverage.** The jens62 lineage has been at this the
  longest: the broadest tested-model history, firmware-update handling, and error codes
  annotated with Geberit's own repair text. The practical maintenance surface — descaling and
  filter status, per-setting read/write, error reporting — this project matches (and gates its
  physical procedures for safety); where the jens62 lineage still leads is that longer history
  and the depth of low-level annotation.
- **The hard Alba problem is unsolved on every side.** Making the *physical remote* coexist
  on the encrypted Alba is an open problem for all three projects, this one included. Our
  architecture is designed to handle it; that is not the same as having proven it.

None of that changes the architectural point — it sharpens it. The others are more mature;
this project is shaped differently, in a way that removes the coexistence and latency limits
rather than mitigating them, and it already demonstrates that shape working on Series 248.

## Credit where due

Both other projects, and Thomas Bingel's C# library that jens62 builds on with its own
reverse-engineering, did real protocol work and built the community around these toilets. This project reverse-engineered
the protocol independently rather than building on that code, so the credit here is
collegial, not a debt — they are good projects solving a slightly different problem in a
well-chosen way for their context.
