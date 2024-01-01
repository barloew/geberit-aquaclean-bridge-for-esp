# AquaClean Series 250 bridge (experimental)

This branch is a **separate, experimental** dual-role BLE bridge for the **Geberit AquaClean Alba** — a
**Series 250** device, which uses a different protocol. It is **not** part of the main
[Geberit AquaClean Bridge](https://github.com/barloew/geberit-aquaclean-bridge-for-esp) ESPHome component, and the
two cannot share one firmware:

- The main bridge is an ESPHome component for Geberit AquaClean **Series 248** shower toilets (Sela, Mera, Tuma,
  Cama).
- This is a **standalone ESP-IDF firmware**. Its components are stack-independent, so they could later be ported
  onto the main bridge; this branch is the proving ground.

> **Status:** builds clean for ESP32-S3; the BT public-address clone is confirmed on hardware and the command codec
> is unit-tested. **Not yet run end-to-end against a real Alba.**

## What it does

This experimental BLE bridge runs two roles at once:

- **Central** → connects to the real Alba toilet and holds a persistent secure session.
- **Peripheral** → clones the toilet's address and advertises as the toilet, so the **clients that would normally
  connect to the toilet connect to the ESP32 instead**: the Alba remote, the Geberit app, and — once wired up to the
  main bridge — home-automation and HomeKit clients. It terminates each client's session locally, all at the same
  time.

## Install

> Experimental — flash at your own risk; see the warranty note in [`VISION.md`](VISION.md).

**Hardware.** An ESP32-S3 board with native USB and **≥ 8 MB flash**; the proving hardware is a **LilyGO
T-Dongle-S3**, and an **M5Stack AtomS3 Lite** works just as well (both are ESP32-S3, and the firmware uses no
board-specific pins). The chip must be an ESP32-S3 — the address-clone step depends on it. A board that needs
an external antenna must have one fitted.

**Language.** The firmware, its web page and diagnostics are **English-only**.

**Placement.** The Bluetooth link to the toilet is what matters, so put the board **close to the toilet with
clear line of sight** — ideally within a metre or two, comfortably stronger than about **−80 dBm** on the
toilet's signal in the live log. A person sitting on the toilet attenuates the signal by roughly 13 dB *exactly
when the bridge is in use*, so a marginal link fails in confusing ways (it connects, then GATT times out). When
in doubt, move the board closer.

1. **Flash the firmware — from your browser, nothing to install.** Open the **install page**
   <https://barloew.github.io/geberit-aquaclean-bridge-for-esp/series-250/> in **Chrome or Edge on a
   computer**, plug the board into USB, and click **Install**. One generic image works for everyone; you set
   your toilet address afterwards (step 2).
   *Developers* can instead build and flash with ESP-IDF 5.5: `cd firmware && idf.py set-target esp32s3 &&
   idf.py build && idf.py flash` (see [`firmware/README.md`](firmware/README.md)).
2. **Connect to the setup page.** On boot the firmware brings up its own WiFi network **`AquaClean-Setup`**
   (password `aquaclean250`). Join it with a phone or laptop and open **http://192.168.4.1/**.
3. **Register your toilet.** Under **Pick your toilet**, the page lists nearby Bluetooth devices with their
   signal strength; click **use** next to your AquaClean (see *Which one is the toilet?* below). The address is
   stored on the board and **the board reboots** to apply the clone. (A manual address box is there as a
   fallback.)
4. **Reconnect and confirm the clone.** After the reboot, re-join `AquaClean-Setup` and reopen the page. In the
   **live log**, the `PUBLIC` line must now equal your toilet's address. If it still shows the board's factory
   address, the clone didn't take — report that line; nothing else works until it's right.
5. **Let the bridge pair with the toilet — last, after the reboot.** The bridge plays the remote's part: it
   scans for the toilet and starts the pairing/handshake itself (it needs no interaction with the remote or
   "pairing mode" of its own). **Order matters** — register the address and let the board reboot *first* (so it
   is cloning the right address) before you make the toilet pairable. Getting a real Alba to pair is the main
   thing this field test is here to prove, so treat this as the current best guess and report what actually
   happens:
   - Keep the **remote off / away** and make sure the **Geberit Home app is closed**, so neither can claim the
     toilet first (the toilet accepts one connection at a time — whoever connects first takes the slot).
   - Put the toilet into its pairable state — its precondition is **"device switched off"** — so power-cycle the
     toilet (off, wait 30 seconds, then on).
   - Watch the **live log**: the bridge should connect and finish its handshake, ending in **connected to the
     toilet**. If it doesn't, copy what the log shows — that's the key result.

Once the log shows the bridge connected to the toilet, setup is done — continue with
[`TEST_PLAN.md`](TEST_PLAN.md) and report results in the *Field testing* category in Discussions.

### Which one is the toilet?

Each row shows a name, an address and a signal strength (RSSI, e.g. `-45 dBm`; closer to 0 = stronger). Your
toilet is the one right next to you:

- Its address usually starts with Geberit's prefix **`e4:85:01`**.
- Stand next to the toilet and set the remote a few metres aside; the toilet is the strongest `e4:85:01` device
  that stays strong. (It's also the device the Geberit app connects to.)
- Not in the list? Switch the remote off and close the Geberit app (so neither is holding the connection) and
  power-cycle the toilet so it advertises, then pick it. (The real pairing is step 5, after the reboot.)

> The WiFi portal is a **test convenience**, not part of the shipped bridge — the finished product integrates
> over WiFi (native API / MQTT) like the main bridge. WiFi and BLE run together fine on this hardware: the main
> bridge already runs its BLE dual-role alongside WiFi, stably. The portal uses an access point rather than
> joining your WiFi, but that coexists with BLE just the same. If you ever want to take WiFi out of the picture,
> press **Stop WiFi** or build with `ENABLE_WEB_PORTAL 0`.

See [`firmware/README.md`](firmware/README.md) for the full build, configuration and self-test details.

## Layout

- `firmware/` — the ESP-IDF project (see its own `README.md` to build, flash and run the boot self-test).

See [`firmware/README.md`](firmware/README.md) for configuration, flashing and the capture/confirm procedure.

## Vision, roadmap & testing

- [`VISION.md`](VISION.md) — what this branch is trying to achieve, where it stands, and what we ask testers for.
- [`TEST_PLAN.md`](TEST_PLAN.md) — the step-by-step field test; report results in the *Field testing* category in
  [Discussions](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/discussions).
