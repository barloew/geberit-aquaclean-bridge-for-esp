# Maintainer & build guide — `series-250-bridge` (Alba, experimental)

The reference for how **this branch** is built, how the pieces fit together, how to keep it up to
date, and exactly how to reproduce every CI job **before** you push. This branch develops
independently from `main`.

> **This guide is for the `series-250-bridge` branch only.** `main` is a *different firmware* — the
> mature Series 248 bridge, an ESPHome external component (C++ + Python) built with `esphome` — and
> has its own `docs/MAINTAINERS.md` and its own `CHANGELOG.md`. The two branches share a name, a
> protocol lineage and the governance tooling — nothing in the build. **They are never merged into
> each other.** This firmware is a hand-written **ESP-IDF 5.5 + NimBLE** app in plain C, built with
> `idf.py`, and is **experimental and unverified** (written against ESP-IDF 5.x, not yet proven on
> hardware in CI history).

---

## 0. Status & the private-protocol rule

- **Experimental / unverified.** Expect to adjust API and Kconfig names on the first real hardware
  build (LilyGO T-Dongle-S3, ESP32-S3). Nothing here is "released"; field-test feedback drives change.
- **What stays private.** The **credential value** and the **detailed RE prose spec** —
  `Protocolversies_v1_v2.md`, `Foutcodes.md`, `errorcodes_*.csv`, captures (`*.pcap`) and the
  `Geberit_AquaClean_RE.zip` package — are **never** committed or published as text; `.gitignore` blocks them,
  and the credential's value lives only in the git-ignored `aquaclean_bridge_id.c`. The one exception: the
  credential is compiled into the distributed Alba firmware binary (the web installer's `.bin`, built in CI
  from the `ALBA_BRIDGE_ID_C` secret) — it ships there by necessity, but never as text in source, docs, logs
  or review. The **firmware source itself is public on
  this experimental branch**: it *is* the implementation, so `arendi_*` (framing/handshake) and the generated
  `alba_dp_table.h` (DpIds/enums) are part of the published code. New captured-protocol findings go into the
  private spec and through `SECURITY.md`, never a public issue/PR.
- **Canonical protocol spec (private).** The full v2 handshake is specified in the author's private
  `Protocolversies_v1_v2.md` (version negotiation → EP nonces → X25519 + CMAC key exchange → AES-CTR channel,
  the keyset slot model, and the post-handshake `CMD_UNLOCK`). It is **kept out of this repository**. The
  `firmware/main/arendi_*.c` sources implement exactly that spec; when you change the handshake, update the
  private spec, not a repo file. Key facts confirmed against real captures: the physical remote and the Geberit
  app share **one** client credential (so relaying the remote is not gated on an unknown key), `keyset_id`
  (0x00/0x01) is a slot selector rather than a separate key, and the type-plate code is the separate
  `CMD_UNLOCK` step — not the CMAC key.

---

## 1. The firmware

### 1.1 What it is

A dual-role BLE relay on ESP-IDF + NimBLE: **central** to the real toilet (holds the one permanent
secure link) and **peripheral** advertising as the toilet so the remote control and the Geberit app
connect to the ESP instead. Each side is an independent Arendi session; decrypted application frames
are relayed between them, with a hook for local (Home Assistant) injection.

### 1.2 Repository map

```
firmware/
  CMakeLists.txt                  IDF project root (project "esp32-aquaclean")
  main/
    CMakeLists.txt                SRCS + REQUIRES (bt nvs_flash mbedtls esp_hw_support esp_wifi esp_netif esp_event esp_http_server)
    main.c                        app_main, GAP events, relay hooks, advertising/scan, HEALTH & RECOVERY ladder
    arendi_common.{c,h}           Arendi framing + crypto shared by both roles
    arendi_client.{c,h}           central role: SABM -> VERSION -> EP -> KE -> ENCRYPTED handshake, then Ble20
    arendi_server.{c,h}           peripheral role: server side of the same handshake
    alba_cmd.{c,h}                Ble20 command describe/helpers
    alba_dp_table.h               datapoint table — GENERATED, do not hand-edit
    web_portal.{c,h}              SoftAP + HTTP: pick-your-toilet, live log, on-page test harness, /health
  sdkconfig.defaults              NimBLE dual-role; mbedTLS X25519/CMAC/HKDF; 8 MB flash; SW coexistence; USB-JTAG console
  partitions.csv                  nvs / phy_init / factory (0x300000)
  README.md                       firmware reference incl. the health ladder and its UNVERIFIED caveats
README.md, VISION.md, TEST_PLAN.md   install/overview, roadmap, and the field-test plan (on-page test sections)
```

### 1.3 How it hangs together

- `app_main()` (`main.c`): clones the toilet's public BD_ADDR onto the BT interface, optionally starts the
  web portal, then brings up NimBLE. `on_sync()` sets the central's random address, starts scanning and
  advertising, and starts two `esp_timer`s: the address self-test (5 s) and the **health tick** (2 s).
- **Relay:** `gap_event()` routes GATT — the central link decodes toilet notifications through `arendi_client`
  (`on_toilet_app` → forward to the remote), the peripheral link decodes remote writes through `arendi_server`
  (`on_remote_app` → forward to the toilet).
- **Web portal** (`web_portal.c`): a WiFi SoftAP + HTTP server for setup and logging with no serial cable; it
  also persists the recovery breadcrumb and the last-restart reason in NVS and serves `/health`.
- **Health & recovery ladder** (`main.c`, ported from `main`'s 248 bridge): liveness from decoded toilet
  frames, a handshake watchdog (re-handshake), a reconnect-flap/stuck detector, a `nimble_port_deinit/init`
  stack reinit (rung 3), a bounded gated reboot (rung 4), and a sticky "needs power-cycle" (rung 5). Full
  description and its **known hardening caveats** (the esp_timer-vs-host-task race on `s_cli`; the unverified
  reinit sequence) are in `firmware/README.md`.

### 1.4 Build & run locally

ESP-IDF v5.5, target esp32s3 (matches CI — see 3). Developed on a LilyGO T-Dongle-S3.

```bash
cd firmware
idf.py set-target esp32s3
idf.py build                       # exactly what CI runs
idf.py -p <port> flash monitor     # flash + serial over the native USB-JTAG console
```

Setup/log without a cable: join WiFi `AquaClean-Setup` (password `aquaclean250`) → `http://192.168.4.1/`.
The page shows live health at the top, the pick-your-toilet list, the on-page test harness and the live log.

---

## 2. The browser flasher (hosted on `main`)

There is **no GitHub Pages workflow on this branch.** The browser flasher page lives on `main` under
`static/series-250/`. To publish a testable flasher: `idf.py build` here, produce a single merged
`firmware.bin` (bootloader + partition table + app), and commit it into `static/series-250/` on `main`;
that folder's `README.md` on `main` has the exact steps. Everything else about the flasher page is a
`main` task.

---

## 3. CI/CD — every job, and how to reproduce it locally

### 3.1 `.github/workflows/validate.yml` (push to `series-250-bridge`, every PR, manual)

| Job | What it does | Reproduce locally |
|---|---|---|
| `clang-format` | C format on own sources; skips generated `alba_dp_table.h` | `clang-format --dry-run --Werror $(find firmware/main \( -name '*.c' -o -name '*.h' \) ! -name 'alba_dp_table.h')` |
| `build` | `espressif/esp-idf-ci-action@v1`, ESP-IDF **v5.5**, target **esp32s3**, `path: firmware` | `cd firmware && idf.py set-target esp32s3 && idf.py build` |

`clang-format` config is `.clang-format`. A green CI build is **not** a green hardware run — this firmware
is unverified, so always re-test on the board after a change that compiles.

### 3.2 Shared governance workflows

- **Claude** — `claude.yml` is **PR-only** here (mention `@claude` in a PR review or review comment). New
  issues and `@claude` in issue comments are handled **repo-wide from `main`**, so they are intentionally not
  duplicated on this branch. Uses the repo secret `ANTHROPIC_API_KEY` and the Claude GitHub App.
- **`pr-ai-disclosure.yml`**: every PR description must contain `AI-Assistance: none|assisted|generated`
  (see the PR template) or the check fails.
- **`labeler.yml`** (+ `.github/labeler.yml`): path-based PR labels. The label *set* is created repo-wide from
  `main`'s `labels.yml`.
- **CodeRabbit** (`.coderabbit.yaml`): this branch has **its own** config — CodeRabbit reads it from the PR's
  base branch, so it does not inherit `main`'s. It excludes the generated `alba_dp_table.h`, adds NimBLE/crypto
  review instructions, and instructs the reviewer never to reproduce or request the private protocol internals.
  Keep it in step with the C firmware; `main`'s CodeRabbit config is separate and tuned for the ESPHome component.

Dependabot, `CODEOWNERS`, `CONTRIBUTING.md` and `SECURITY.md` are configured repo-wide (on `main`).

---

## 4. Dependencies — inventory & keeping current

This firmware has **no vendored third-party C** (the Arendi/Ble20 code is our own); its dependencies are the
toolchain and what ESP-IDF ships. Everything here is **manual** — see the Dependabot caveat below.

| Dependency | Kind | Pinned / recorded in | Tracking | Notes |
|---|---|---|---|---|
| ESP-IDF `v5.5` | toolchain + BLE/crypto/HTTP stacks | `validate.yml` (`esp_idf_version`), local `idf.py` | manual | re-test on hardware after a bump |
| IDF built-in components | `bt`/NimBLE, `mbedtls`, `esp_wifi`, `nvs_flash`, `esp_http_server`, `esp_netif`, `esp_event` | `firmware/main/CMakeLists.txt` `REQUIRES` | move with ESP-IDF | no pins of their own |
| GitHub Actions | CI actions (`uses:`) | this branch's workflow files | **manual on this branch** | see caveat |
| `alba_dp_table.h` | generated datapoint table (not external) | its generator/source | regenerate | excluded from clang-format |

> **Dependabot caveat.** The repo's Dependabot config lives on `main` and, for GitHub Actions, updates the
> **default branch only**. So the Action versions in *this* branch's workflows are **not** auto-bumped — bump
> them by hand (or add a `target-branch: series-250-bridge` entry to `main`'s `.github/dependabot.yml`).

### How to update

- **ESP-IDF:** change `esp_idf_version` in `validate.yml`, rebuild, and **re-test on hardware** — treat a
  toolchain bump as a full re-verification, since the firmware is unverified to begin with. If the stack APIs
  shift (NimBLE, `esp_http_server`, coexistence Kconfig), expect to touch `main.c` / `web_portal.c` /
  `sdkconfig.defaults`.
- **GitHub Actions:** bump the `uses:` tags by hand (Dependabot does not reach this branch — see caveat), or add
  the `target-branch` entry on `main`.
- **IDF built-in components:** nothing to pin; they come with the ESP-IDF version above.

---

## 5. "Release" (there isn't one)

Experimental — no version tags, no changelog versions, no build pipeline. The changelog here is a dated
log of what changed. To get a testable build into people's hands: build the merged binary, commit it to
`static/series-250/` on `main` (section 2), and update `README.md` / `TEST_PLAN.md` / `firmware/README.md`
as needed. Field-test reports (posted in the *Field testing* discussion category) drive the next change.

---

## 6. Guardrails

- **Unverified firmware.** CI only proves it *compiles*. Every functional change needs a hardware re-test. The
  authentication credential is known (the remote shares the app's credential; see `arendi_common.c`), so the open
  field-test unknowns are the **end-to-end pairing/bonding behaviour** (does presenting that credential bond
  bridge↔toilet and remote↔bridge without the permanent displacement state other projects saw) and multi-day
  stability (see `VISION.md` / `TEST_PLAN.md`).
- **Protocol internals stay private** (section 0). Only function names in user-facing pages.
- **`alba_dp_table.h` is generated** — change its source/generator, not the file.
- **Branches stay apart.** No cross-merges with `main`; a fix belongs to exactly one framework.
- **Health ladder hardening.** `health_tick` runs in the esp_timer task while GAP events run in the host task,
  so `s_cli`/`s_toilet_conn` reads race with `acli_free` on disconnect — add a lock (or move the check onto the
  host task) before relying on it in production. The rung-3 `nimble_port_stop/deinit/init` sequence is unverified;
  confirm the teardown order on your ESP-IDF. Both are noted in `firmware/README.md`.
- **AI authorship is disclosed** on every PR; English is the working language across issues/PRs/discussions.
