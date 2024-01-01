# Vision & roadmap — Series 250 bridge (Alba)

> **Experimental.** This branch is a proving ground, not a product. Read it as "where we want to go and
> what we need help proving", not as a promise of working features.

## Why this branch exists

The main [Geberit AquaClean Bridge](https://github.com/barloew/geberit-aquaclean-bridge-for-esp) gives
**Series 248** models (Sela, Mera, Tuma, Cama) full local control — no cloud, no Geberit account. **Series
250** (the AquaClean Alba) runs on Geberit's newer platform with an encrypted
protocol (protocol v2) that the main bridge does not speak. This branch exists to find out whether the same
kind of local, self-contained bridge is possible for Series 250, and to build the proof step by step.

The end goal is the same promise as the main bridge: **your device, controlled locally, by you.**

## What we want to achieve (functional vision)

In rough priority order:

1. **Reliable local link** to a Series 250 device that stays up, recovers on its own, and does not depend on
   the Geberit cloud or app.
2. **Coexistence** — the original remote and the Geberit app keep working with the bridge in place, so adding
   the bridge never takes features away.
3. **A personalized experience the Alba itself can't give you — the real point of this bridge.** The Alba
   toilet keeps **no profiles of its own** and its remote has no profile keys. Profiles live in the **Geberit
   Home app**: you create one there and, when you apply it, the app writes its settings to the toilet **in one
   batch**. The bridge works the same way as a client — it holds its **own profiles (up to six)** and applies
   one by writing the settings — so it can do the picking automatically: recognize who is approaching from
   their phone's IRK and apply that user's profile, and expose per-user personalization to your home
   automation. Local, hands-free, no app. (No toilet-stored profile slots are needed — neither the app nor the
   bridge relies on them.)
4. **More control of the functions the Alba actually offers.** It is a basic shower toilet: the shower
   (start/stop), the shower jet strength (5 levels), the oscillating movement, the shower-arm position
   (5 levels), water temperature (5 levels) and water heating, and descaling. It has no dryer, odor extraction
   or orientation light — those are larger models, not the Alba.

   But "more control" is the point: a function the Alba lacks as a button can still be **composed from the
   settings it does have**. A **lady shower** is really just a preset — a chosen combination of shower-arm
   position, jet strength, water temperature and oscillation. The bridge already applies exactly those settings
   as a batch per profile, so it can offer a lady shower the Alba has no dedicated button for: as a **preference
   (per profile)** and/or a **session setting**, the bridge sets the wanted combination and starts the shower.
   We can surface this composed lady shower **four ways**:
   - **automatically** — chained on a given profile, e.g. a normal shower followed by a lady shower (or the
     reverse); the user stops the second wash with the remote (press the shower button again),
   - via **HomeKit / Siri**,
   - via a dedicated **remote button-combination**, and
   - via **home automation**.
5. **Status feedback, including faults.** The bridge reports what the device is doing back to your home
   automation — and, crucially, it makes **faults readable**. The toilet's error register becomes a plain,
   module-aware label, **code-first** so it is exactly what you hand Geberit support: e.g. *Error code 071B
   (Hot water)* — the hex is the lookup key, the module name tells you where. On the Alba (Series 250) the
   bridge can additionally read the device's **Event Storage fault log** (the history of faults with their
   times), so a problem surfaces as text in your home automation instead of a blinking light on the unit.
6. **Integration on par with the main bridge** — the same **two equal first-class routes, the native API and
   MQTT**, plus optional **HomeKit** (stand-alone). Any consumer can use it with no model-specific cloud in
   between — openHAB, Node-RED, Homey, Domoticz, ioBroker, Apple Home, Home Assistant and the rest — none of
   them the default.
7. **A clean path back into one bridge.** The crypto and codec here are stack-independent, so if this proves
   out the aim is to fold Series 250 into the **main bridge** — one codebase instead of two firmwares forever.
   That the main bridge happens to be built on ESPHome is just its build platform: it already serves the same
   two equal routes (native API and MQTT) plus HomeKit, so convergence changes nothing about route choice and
   never makes any one hub or route the default.

## Where it stands

See the status line in [`README.md`](README.md). In short: it builds, the address clone is confirmed on
hardware and the command codec is unit-tested, but it has **not** been run end-to-end against a real Alba.
That last gap is exactly why we need field testers. To make testing easy for non-technical owners, the firmware
includes a **built-in WiFi setup/log page** (enter the toilet address and watch the log in a browser — no cable
or external tools). It is a test convenience, not part of the shipped bridge (which integrates over WiFi like
the main bridge); see [`TEST_PLAN.md`](TEST_PLAN.md).

## What we ask users to test — and why

We are actively looking for **Alba owners** to run the bridge on their own hardware and
report back. The device is experimental, so an honest **FAIL with a good log is the most useful result** — it
tells us where the real-world behaviour diverges from what we built blind.

- **Follow the standard plan:** [`TEST_PLAN.md`](TEST_PLAN.md) walks the steps (flash, pair, relay, functions,
  coexistence, stability) and says what to record.
- **Report in the project's Field testing area** (the *Field testing* category in
  [Discussions](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/discussions)), in English.
- **The feedback that moves this forward most:**
  - Does pairing complete, and how long/retries? (step 2)
  - Which remote/app actions show up relayed, **by name**? (step 3) — this is the key unknown.
  - Does the remote **and** the app keep working with the bridge in place? (step 5)
  - Does the link stay up over hours, and recover after a reboot? (step 6)
- **Hardware we'd love coverage on:** different Alba article numbers.

### Please keep protocol details out of public posts

This is a public branch. When you report, describe **behaviour** (what worked, what failed, log lines) — you do
not need to include protocol internals. Anything sensitive (a suspected security issue, or captured secrets)
goes through the private channel in [`SECURITY.md`](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/blob/main/.github/SECURITY.md),
never a public issue or discussion.

## Honesty & safety

Using unofficial software with your device may affect the manufacturer warranty and the service terms of your
installer or service provider. This branch is experimental and offered as-is. The safety notes from the main
bridge apply here too — do not rely on it for unsupervised operation.

## Roadmap

1. **Prove the link (now).** End-to-end against a real Alba: pairing, a decrypted frame relayed both ways,
   key-stream continuity over a long session. Field-tester logs drive this.
2. **Name the functions.** Confirm each Alba function by name, in both directions, across a few Alba units:
   shower (start/stop), jet strength, oscillation, shower-arm position, water temperature, water heating and
   descaling — plus the app's **profile push** (applying a profile = a batch of these settings written at once).
3. **Expose to home automation & personalize.** Wire the relayed state/commands to the native API, MQTT and
   HomeKit, matching the main bridge's entity model, and add IRK-based automatic profile selection — the
   feature the Alba's own remote lacks.
4. **Fold back in.** Port the proven codec onto the main bridge's Bluedroid dual-role, so Series 250 becomes
   part of the one component instead of a separate firmware — if and when the proof holds.

Each phase gates the next; nothing moves to "supported" until real hardware confirms it.
