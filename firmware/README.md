# esp32-aquaclean — AquaClean Alba BLE bridge (ESP-IDF)

Experimental ESP-IDF/NimBLE firmware that sits between a Geberit AquaClean **Alba** (Series 250) and its remote
control, using two BLE roles at once:

- **Central** → connects to the real toilet and holds a persistent secure link.
- **Peripheral** → clones the toilet's address and advertises as the toilet, so the remote connects to the ESP32
  instead, and terminates the remote's secure link locally.

Decrypted application frames are relayed between the two links, with a hook for local home-automation injection.
Each link keeps its own ephemeral keys; the bridge decrypts on one side and re-encrypts on the other.

```
remote control  <--BLE-->  [ ESP32 peripheral | relay | ESP32 central ]  <--BLE-->  toilet
```

> **Status:** builds clean for ESP32-S3; the BT public-address clone is confirmed on hardware and the command
> codec is unit-tested. **Not yet run end-to-end against a real Alba.**

> **Protocol spec.** The full v2 handshake (version negotiation, EP nonces, the X25519 + CMAC key exchange, the
> AES-CTR channel, the keyset slot model and the post-handshake `CMD_UNLOCK`) is specified in the author's
> private `Protocolversies_v1_v2.md`, which is **kept out of this repository** along with the credential value.
> The `arendi_*` sources below implement exactly that spec; this README stays at the level of file roles and
> behaviour, not the protocol internals.

## Files

- `main/arendi_common.[ch]` — framing and crypto.
- `main/arendi_client.[ch]` — central role.
- `main/arendi_server.[ch]` — peripheral role (presents the toilet to the remote).
- `main/alba_cmd.[ch]` + `main/alba_dp_table.h` — command/address codec and the datapoint table.
- `main/web_portal.[ch]` — built-in WiFi SoftAP + web page (device picker, live log, manual address box,
  WiFi-off button) and the scanned-device table.
- `main/main.c` — NimBLE glue, address clone, MTU chunking, the relay, and the boot self-test.

## Configure

- **Toilet address:** set it at runtime on the web portal (stored in NVS), or change the compile-time default
  `TOILET_MAC_STD` / `TOILET_ADDR` in `main/main.c` as a fallback.
- **Shared client credential:** in `aquaclean_bridge_id.c` — this file is **git-ignored and not in the
  repository**. Copy `main/aquaclean_bridge_id.example.c` to `main/aquaclean_bridge_id.c` and fill in the real
  16-byte value before building; without it the handshake will not authenticate.
- **Portal on/off:** `ENABLE_WEB_PORTAL` in `main/main.c` (set to `0` to build pure-BLE with no WiFi).

## Build and flash

Requires **ESP-IDF 5.5**, an **ESP32-S3 with ≥ 8 MB flash** (the portal pulls in WiFi + an HTTP server; see
`sdkconfig.defaults` and `partitions.csv`).

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash
```

A merged image can instead be flashed from a Chromium browser (offset `0x0`). The USB serial console
(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) still shows the log as a fallback.

## Setup / log portal (`web_portal.c`)

On boot the firmware starts a WiFi SoftAP **`AquaClean-Setup`** (password `aquaclean250`) and serves a page at
`http://192.168.4.1/`:

- `GET /` — the page: a "pick your toilet" device list, a manual address box, the live log, and a "Stop WiFi"
  button.
- `GET /scan` — JSON of nearby BLE devices (`a` address, `r` RSSI, `n` name) the central has discovered. The
  table is fed from the GAP discovery callback in `main.c` via `web_portal_report_device()`.
- `GET /log?since=N` — new log bytes since offset `N` (the page polls this; the `X-Next` header is the next
  offset). The log is captured with an `esp_log_set_vprintf` hook into a ring buffer; serial output is kept.
- `POST /config` — body is the toilet address (`aa:bb:...`), from the picker or the manual box; stored in NVS,
  then the device reboots to apply the clone.
- `POST /radio` — stops WiFi so the 2.4 GHz radio is free for an uncontended BLE measurement.
- `GET /health` — JSON `{"h": <state>, "r": <last restart reason>}`. The page shows the live health state at
  the top and, after a self-reboot, why the board last restarted (persisted in NVS, since the RAM log ring is
  cleared by a restart).

## Health & recovery ladder (`main.c`)

Ported from the mature Series 248 bridge, adapted to NimBLE. A `health_tick` timer (every 2 s) watches the
toilet link and escalates a stuck or absent link, least-disruptive first. "Healthy" here means a **completed
Arendi secure link** — this relay does not poll, so a finished handshake is the liveness signal.

- **Handshake watchdog** — connected but the Arendi handshake does not reach `ACLI_DONE` within 20 s →
  terminate the link so the disconnect path rescans and starts a fresh handshake (crypto-session recovery).
- **Reconnect-flap / stuck detection** — ≥ 5 toilet-link reconnects in 10 min, or 5 min continuously unhealthy.
- **Rung 3 — BLE-stack reinit** — once per bad episode, `nimble_port_stop/deinit/init` + re-register, without a
  full reboot.
- **Rung 4 — gated reboot** — `esp_restart`, bounded to **2 per episode** by a persisted NVS breadcrumb so a
  wedge can never loop reboots. The reason is written to NVS first and shown on the page after the reboot.
- **Rung 5 — needs power-cycle** — sticky state (shown on the page and logged); the toilet itself seems wedged
  and only a power-cycle clears it. No more reboots.
- A sustained healthy period (10 min) clears the episode and the breadcrumb. The ladder only runs after the
  first successful connection this boot, so a missing/unconfigured toilet never triggers a reboot loop.

Memory is sampled every 30 s (free heap, lowest-ever free heap, largest free block) and logged.

> **Known hardening items (UNVERIFIED):** `health_tick` runs in the `esp_timer` task while GAP events run in the
> host task, so reads of `s_cli` / `s_toilet_conn` race with the disconnect handler's `acli_free` — add a lock
> (or move the check onto the host task) before relying on it in production. The rung-3 `nimble_port_stop/
> deinit/init` sequence is written against ESP-IDF 5.x and not compiled here; verify the teardown order on your
> build.

**Coexistence:** WiFi and BLE share the radio via `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` — a standard, stable
configuration (the main bridge runs its BLE dual-role alongside WiFi the same way). The portal is a **test
convenience**, not part of the shipped bridge; `POST /radio` (Stop WiFi) or `ENABLE_WEB_PORTAL 0` takes WiFi
out of the picture if you want to isolate it. See `../TEST_PLAN.md`.

> This firmware is **unverified** — written against ESP-IDF 5.x but not compiled here. Expect to adjust
> API/Kconfig names (WiFi, HTTP server, coexistence) when you first build on hardware.

## Boot self-test (no Alba needed)

At boot, and every 5 s, the firmware logs its own addresses so you can confirm the public-address clone took:

```
PUBLIC (should equal your toilet's address) addr (type 0): …
RANDOM (central role) addr (type 1): …
```

If `PUBLIC` shows the ESP's factory address instead, the clone did not take on your target and the remote will not
recognise the bridge.

## Capture / confirm mode (needs an Alba)

The relay logs every decrypted frame by name, which is the confirmation step. Near a real Alba you should see lines
such as `remote->toilet: Write <shower command> (0x0004)`. Confirm that the handshake completes, that frames relay both
ways, and that the stream stays in sync over time.

## Open items

- **Pairing with the toilet.** The `+` key is on the remote, not the toilet, so the bridge's central side must
  initiate the bond itself (it plays the remote). The **credential is known**, not open: sniffer captures show
  the physical remote authenticates with the **same shared client credential as the app** (kept in the
git-ignored `aquaclean_bridge_id.c`, not in this repository);
  the `keyset_id` (0x00 app / 0x01 remote) is a key-*slot selector*, not a separate key, and the type-plate
  pairing code is a separate post-handshake `CMD_UNLOCK` step, not the CMAC key. Open questions for real hardware
  are therefore *not* "which key", but: what puts the toilet into its pairable state (the manual's precondition is
  "device switched off", i.e. a power cycle), and the end-to-end behaviour — whether presenting that credential
  lets the bridge bond to the toilet (as the remote) and the remote bond to the bridge without the permanent
  displacement state other projects saw. Pair the bridge with the remote switched off, then pair the remote to
  the bridge separately.
- End-to-end against a real Alba, and the AES-CTR key-stream continuity.
- A home-automation control surface — Home Assistant, Homey, ... (the codec can already encode named commands).
