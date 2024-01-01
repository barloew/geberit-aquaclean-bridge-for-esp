# Changelog — `series-250-bridge` (Alba, experimental)

This branch is **experimental and unversioned**: no release tags, no CI publishing. This is a running
log of what changed in the firmware and its docs. It is independent from `main`'s changelog. The
detailed Series 250 protocol-v2 spec stays private and is not described here.

## Unreleased

- **Self-monitoring & recovery ladder** (`main.c`), ported from the mature Series 248 bridge and adapted
  to NimBLE. Health = a completed Arendi secure link (this relay does not poll, so a finished handshake is
  the liveness signal). A 2 s `health_tick` escalates a stuck or absent link, least-disruptive first:
  - *Handshake watchdog* — connected but no `ACLI_DONE` within 20 s → terminate so the disconnect path
    rescans and starts a fresh handshake (crypto-session recovery).
  - *Rung 3 — BLE-stack reinit* — once per bad episode, `nimble_port_stop/deinit/init` + re-register, without
    a full reboot.
  - *Rung 4 — gated reboot* — `esp_restart`, bounded to **2 per episode** by a persisted NVS breadcrumb so a
    wedge can never loop; the reason is written to NVS first.
  - *Rung 5 — needs power-cycle* — sticky state when the toilet itself seems wedged; no more reboots.
  - A sustained healthy period (10 min) clears the episode. The ladder runs only after the first successful
    connection this boot, so a missing/unconfigured toilet never triggers a reboot loop.
  - Memory is sampled every 30 s (free heap / lowest-ever / largest free block).
  - **Known hardening caveats (UNVERIFIED):** the `health_tick` (esp_timer task) reads of `s_cli`/`s_toilet_conn`
    race with the host task's `acli_free`; the rung-3 reinit sequence is untested. See `firmware/README.md`.
- **Web portal** gained a live **Bridge health** line and a persisted **last-restart reason** (shown after a
  reboot, since the RAM log ring does not survive a restart), served by a new `GET /health` endpoint.
- Retracted the stale "WiFi can reduce BLE stability" comment in `main.c` to match the project position that
  WiFi and BLE coexist fine on this hardware.
- New `MAINTAINERS.md`: this branch's build (`idf.py`), how the pieces fit together, and how to reproduce the
  CI jobs locally.

## Earlier (branch bring-up)

- Dual-role relay firmware (ESP-IDF 5.5 + NimBLE): central to the toilet, peripheral advertising as the toilet,
  with an independent Arendi session on each side (`arendi_client` / `arendi_server` / `arendi_common`) and a
  Ble20 command layer (`alba_cmd`).
- Built-in **setup/log portal** (`web_portal.c`): WiFi SoftAP, pick-your-toilet BLE scanner/selector, live log in
  the browser, and an on-page **test harness** (Press now / Undo per command) that labels each captured command
  in the log. Toilet address stored in NVS; a single generic flashable image.
- Address-clone bring-up: clones the toilet's public BD_ADDR before the stack starts; a boot self-test logs the
  PUBLIC/RANDOM addresses every 5 s so the clone can be confirmed over the re-enumerating USB console.
- `README.md`, `VISION.md` (vision & roadmap incl. personalization via IRK and HomeKit) and `TEST_PLAN.md`
  (install/pairing order, on-page test sections, save-the-log-before-restart) established for field testing.
