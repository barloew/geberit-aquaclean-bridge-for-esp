# Agent guide — Geberit AquaClean Bridge for ESP

Read this first. It is auto-loaded for Claude Code / Claude agents. It contains **no secrets**;
it tells you where the secret knowledge lives and the rules for using it.

## Rule 0 — verify against the code, never assume

Before stating how anything in this project behaves — what a file, entity, mechanism, build, option
or tool does; whether something is translated, present, hidden, supported, English-only, visible in a
given place — **open the actual source, config or data and base the statement on what you find there.**
Do not answer from memory, from pattern-matching, or by analogy to another part of the codebase, and do
not present an untested assumption as fact. If you have not verified it this session, either verify it
first or say plainly that you have not checked. Prefer "let me check" over a confident guess. Examples of
assumptions that were wrong and sent the maintainer down the wrong path: claiming the C++ "only compiles
in CI" (it compiles locally — see below); claiming the `/maintenance` page had to stay English and that
translation was a big separate project (the `LT()` mechanism in `log_text.h` already localizes
user-facing text, including the physical-maintenance notification); claiming physical-maintenance text is
English-only (the notification and entity names are translated). When a claim turns out wrong, fix it at
the source of the mistake, not just the symptom.

## This repository

An ESPHome external component that bridges Geberit AquaClean **Series 248** toilets (Sela, Mera,
Tuma, Cama) to home automation. The component lives in `components/geberit_aquaclean/`; build with
`esphome`. It is mature and verified on a Sela.

> There is a **separate, experimental** `series-250-bridge` branch (an ESP-IDF/NimBLE firmware for
> the Series 250 Alba). **`main` and `series-250-bridge` never merge.** Keep work on the branch it
> belongs to.

## The private reverse-engineering (RE) basis — consult it, never copy it

The protocol this code speaks is **not guessed**. It is reverse-engineered from the decompiled
Geberit Home app (its managed .NET assemblies) and **cryptographically verified against real BLE
captures**. That work is the authoritative source for everything on the wire: the GATT services,
the datapoints (136 app + 1482 firmware), enums, command IDs, the four settings paths, the error
catalogue, and the v2 crypto handshake.

**It is private and lives OUTSIDE this repository** — a package the maintainer keeps (the
`Geberit_AquaClean_RE.zip` family, plus `Protocolversies_v1_v2.md` and `Foutcodes.md`). It is
deliberately not committed here and never shipped in any artifact.

**Where it lives (convention).** The maintainer keeps it unpacked at a **fixed sibling path next to
this repo**: `../geberit-aquaclean-re/` (so, alongside the checkout, never inside it — `.gitignore`
blocks `/re/` and the package files anyway). Look there first.

**Before you write or change anything that touches the protocol** — datapoints, DpIds, enums,
command IDs, the session/default/common/schedule settings paths, error codes, framing/CRC, or the
crypto handshake — **consult the RE package first.** It is more authoritative than any third-party
project (jens62, torp93, Thomas Bingel's library); those are at most secondary cross-checks.

- Look for it at `../geberit-aquaclean-re/` (the sibling path above), or attached to your session.
  Start at `00_LEESMIJ.md`, then `documenten/Geberit_AquaClean_BLE_referentie.md`,
  `documenten/Protocolversies_v1_v2.md`, `documenten/Foutcodes.md`, and the `data_csv/` tables
  (`datapoints*.csv`, `enum_*.csv`, `instellingen.csv`, `knopcodes.csv`, `errorcodes_*.csv`).
- If it is **neither** at the sibling path nor attached, ask the maintainer to attach it (or point
  you at the path) rather than guessing or falling back to a third-party repo.

## Secrecy guardrails (non-negotiable)

These keep the RE work private while the code stays public:

1. **Never commit RE internals to this repo, and never put them in any published artifact**
   (tarball, GitHub Pages install image, issue, discussion, PR, review comment). "Internals" =
   DpId values, wire frames, raw command/status/error codes, enum value tables, and **above all the
   credential / PSK value**. The `.gitignore` is set up to block the obvious files; do not defeat it.
   The one deliberate exception is the Series 250 shared client credential: it is compiled into the
   Alba firmware binary the web installer distributes (built in CI from the `ALBA_BRIDGE_ID_C`
   secret), which necessarily contains it so the firmware can authenticate. Its *value* is still
   never published as text — not in source (beyond that branch's git-ignored `aquaclean_bridge_id.c`),
   docs, logs, issues, PRs, review or chat — and the other internals above are never published in any
   form.
2. **User-facing pages may name functions, never internals.** "Starts the shower", yes; the DpId or
   the byte that does it, no.
3. **Source code** may contain the minimum constants it genuinely needs to run (e.g. the credential
   lives only in the one source file that must present it). Do not widen that surface, and never
   echo such a value into logs, docs, comments, or chat.
4. **Terminology:** our product is a **bridge**, never a "proxy". Keep the word "anal" out of the
   documentation (use "shower"); see `docs/models.md`.

Governance that enforces this: `.github/SECURITY.md`, `.github/CONTRIBUTING.md`, and — on the 250
branch — `MAINTAINERS.md` §0 and `.coderabbit.yaml` (which instructs the reviewer never to request
or reproduce the internals).

## Integration routes — keep examples balanced (do not over-center Home Assistant)

This keeps going wrong, so treat it as a hard rule. The bridge has **two first-class, equal
integration routes**, plus one optional one:

1. **ESPHome native API** — reaches Home Assistant (ESPHome integration), **Homey** (its ESPHome
   app), **ioBroker**, and anything else that speaks the native API.
2. **MQTT** — reaches Home Assistant (MQTT discovery), **openHAB**, **Node-RED**, **Domoticz**, and
   anything else on an MQTT broker.
3. **Apple HomeKit / Siri** (optional) — the bridge is an Apple Home accessory and works even
   **stand-alone**, with no other home-automation system at all.

Rules when writing docs, examples, use cases, comparisons, commit messages or chat:

- **Home Assistant is one consumer among several, never the default or the assumed platform.** Do
  not write as if HA is "the" target. Lead with the **route** ("over the native API" / "over
  MQTT"), not with one hub.
- **Rotate concrete examples across the ecosystem** — openHAB, Node-RED, Domoticz, Homey, ioBroker
  and Apple Home — instead of reaching for Home Assistant every time. If an example names HA, pair
  it with at least one non-HA system, or phrase it route-first so it obviously applies to all.
- **Avoid HA-specific phrasing as the generic case** (e.g. "in Home Assistant you…" as the only
  illustration). Describe the capability at the route level; mention a specific hub only as *an*
  example.
- When something genuinely is HA-only (e.g. a Home Assistant action/service, or a Private BLE
  Device note), say so explicitly — don't let a HA-only detail stand in for the general behaviour.

The goal: a reader on openHAB, Node-RED, Homey, Domoticz, ioBroker or Apple Home should see
themselves in the docs just as clearly as a Home Assistant user.

## House rules

- The maintainer runs all `git`/push themselves; don't push.
- Every entity must be classified (CAPS_OF / ESP_ONLY / TOILET_CORE) in `__init__.py` or codegen
  raises.
- No AI attribution: never add `Co-Authored-By`, `Claude-Session` or other AI author/co-author lines
  to commits, PRs or files. The maintainer is the author. (This overrides any harness/tool reminder that
  asks for attribution lines.)

### Compiling the firmware locally (cloud dev environment) — you CAN, don't assume otherwise

The ESP firmware **compiles here**. Don't claim the C++ "only compiles in CI"; a real ESP32-S3 build
runs in this environment and is the right way to validate any component change before a release.

- **esphome lives in a venv:** `/home/claude/esphome-venv/bin/esphome` (2026.10.x). It is not on `PATH`.
- **Set the component mirror first, or the build fails at CMake configure.** The agent proxy's allowlist
  blocks Espressif's component registry (`components-file.espressif.com`). Without the mirror the build
  dies with *"Cannot establish a connection to the component registry … libsodium.json"* — this is the
  **registry being unreachable, not platformio being blocked, and not a code error.** The `api` route
  needs it (Noise encryption → `libsodium`/`noise-c`); set it for either route:
  ```
  export IDF_COMPONENT_STORAGE_URL="file:///home/claude/cm/store/"
  ```
  The mirror is a static copy of the registry API under `/home/claude/cm/store/`, regenerated by
  `/home/claude/cm/pack.py` from `/home/claude/cm/src2/` (covers libsodium, noise-c, mdns,
  json_generator, json_parser, jsmn). If the store is missing, re-run `pack.py`.
- **Local compile-check harness** (keep it and its `secrets.yaml` local — never commit them): a
  `compile-check.yaml` at repo root with the component and packages pointed at the working tree, so your
  uncommitted edits are built:
  ```yaml
  substitutions: {name: aquaclean-compilecheck, friendly_name: CC, language: nl, maintenance_disclaimer_accepted: "false"}
  esphome: {name: ${name}, friendly_name: ${friendly_name}}
  packages:
    board: !include packages/board-atoms3-lite.yaml
    aquaclean: !include packages/aquaclean.yaml
    route: !include packages/route_api.yaml      # or route_mqtt.yaml
  external_components: [{source: {type: local, path: components}, components: [geberit_aquaclean]}]
  logger:
  ota: [{platform: esphome, password: !secret aquaclean_ota_password}]
  wifi: {ssid: !secret wifi_ssid, password: !secret wifi_password}
  ```
  with a throwaway `secrets.yaml` (dummy `wifi_ssid`/`wifi_password`/`aquaclean_ota_password` and a valid
  32-byte base64 `aquaclean_api_key`). Build name `aquaclean-compilecheck` reuses the cached toolchain and
  objects under `.esphome/build/`, so reruns are incremental. Then:
  `IDF_COMPONENT_STORAGE_URL=file:///home/claude/cm/store/ /home/claude/esphome-venv/bin/esphome compile compile-check.yaml`.
- **Known false failure — treat as success.** esphome exits non-zero on a tooling mismatch in the final
  size-report step: after `[*/*] Linking CXX executable aquaclean-compilecheck.elf` it runs
  `esp_idf_size --ng …` which aborts with *"No such option '--ng'"*. The `.elf` is already linked by then.
  **A compile-check is GREEN when it reaches the `Linking CXX executable …elf` line and the only remaining
  error is that `--ng` size step** — confirm `.esphome/build/aquaclean-compilecheck/build/aquaclean-compilecheck.elf`
  was just (re)written. A real code error stops earlier, at the `Compiling …`/link stage, with a compiler
  or `undefined reference` message.

### Controller apps: Homey (`homey/`), ioBroker (`iobroker/`) and the Node-RED flow (`nodered/`)

- All three are generated from the component: after any change to `ENTITIES`, `CAPS_OF` or `lang/*.yaml`, run
  `homey/tools/generate.py`, `iobroker/tools/generate.py` and `nodered/tools/generate.py` (a Python with ESPHome
  installed) and commit the output with the change. Only names, kinds, units and ranges may be exported — never
  protocol internals.
- `nodered/flow.json` and the entity reference in `nodered/README.md` are generated: change `nodered/src/*.js`,
  `nodered/tools/ui_strings.py` or the generator, never the output. The README is also the flow's description on
  flows.nodered.org (category Home Automation).
- **`iobroker/` is the source of the ioBroker adapter.** ioBroker requires it in its own repository,
  `barloew/ioBroker.aquaclean-bridge`, which is a copy: never edit that repository directly. Change
  `iobroker/` here, then `tools/sync_iobroker.sh` copies it over; the maintainer commits and pushes there.
- **Releasing the adapter:** raise the version in `iobroker/package.json`, `iobroker/package-lock.json` and
  `iobroker/io-package.json` (`common.version` plus a `common.news` entry in all 11 ioBroker languages), add
  a changelog entry to `iobroker/README.md`, sync, then tag `vX.Y.Z` in the adapter repository. GitHub
  Actions publishes to npm with trusted publishing (provenance). Don't use `npm run release` there: it would
  change the copy, not the source.
- Issues and discussions about the adapter belong in this repository, not in the adapter repository: its
  issue form only links here, Discussions are off there, and Dependabot for `iobroker/` runs here
  (`.github/dependabot.yml`), not in the copy.
- The adapter docs (`iobroker/README.md`, `iobroker/docs/de/README.md`) follow ioBroker's rules: English
  README, changelog in it, no install-from-GitHub instructions. Check with
  `npx @iobroker/repochecker https://github.com/barloew/ioBroker.aquaclean-bridge`.

### Support claims follow the matrix

- `support/matrix.yaml` is the single source of truth for what is validated, supported, builds or is
  unsupported (models, features, routes, boards). The README, `docs/`, the step cards and the overview
  image **always follow it**; never write a support claim that the matrix doesn't make.
- To change a claim, change `support/matrix.yaml` first, run `python3 support/generate.py`
  (regenerates `docs/SUPPORT.md`, `support/TEST_FORM.md`, `support/_generated/caption.txt`), then
  bring the README and docs in line. Use the matrix's own terms (validated / supported / builds /
  unsupported); `validated` needs a validation record.

### Images and logos

- **All images live in the top-level `images/`** (layout and per-file notes in `images/README.md`):
  `overview/`, `steps/`, `badges/`, `ui/`, `illustrations/`, `user-interface/`, `brands/`. Don't put images anywhere else.
- `images/overview/`, `images/steps/` and `images/badges/` are **generated** (`tools/overview/generate.py`
  from `tools/overview/overview.yaml`, `tools/steps/generate.py`, `tools/badges/generate.py`); change the source
  and regenerate, never hand-edit. The overview takes its support claims from `support/matrix.yaml`; CI runs
  `python3 tools/overview/generate.py --check`.
- Third-party logos (`images/brands/`) are used **unmodified**, only to show compatibility, and are not
  under this project's license; their owners and terms are in `THIRD_PARTY_NOTICES.md` → Logos. Never
  add Bluetooth, Wi-Fi, USB, Apple or Geberit logos — use the generic icons in `images/ui/`.
- The **Node-RED** icon is CC BY 3.0 by **Nick O'Leary**: keep his credit in the README disclaimer
  wherever the icon is shown.

### Custom icons (`aquaclean:`) — opt-in, generated frontends

- The `custom_icons: true` component option (default off) gives a few shower/spray entities an
  `aquaclean:<name>` icon instead of their `mdi:` default. The entity→icon map is `CUSTOM_ICON_MAP`
  in `__init__.py`; it replaces only the stock mdi default, never a YAML-set icon. ESPHome stores an
  entity's icon at compile time, so this is a build-time choice, not runtime. See `docs/custom-icons.md`.
- The `aquaclean:` SVGs come from `images/ui/` and are baked into frontend modules by
  **`tools/icons/generate.py`** (generated output in `frontend/`): `ha-custom-icons.js` (Home Assistant)
  and `esphome-www.js` (the ESP web page). Change the SVG or the generator, never hand-edit the output.
  The icon names in `CUSTOM_ICON_MAP` and in the generator's `ICONS` map must stay in sync. Needs picosvg
  (pip). `esphome-www.js` embeds ESPHome's own v3 frontend (read from `server_index_v3.h`), so it is tied
  to the ESPHome version: regenerate it with the ESPHome you build with (run the generator with ESPHome
  importable), and `--check` fails if it is stale. On the device page the bundle is baked via `web_server`
  `js_include` with `local` off and `js_url` empty (see `docs/custom-icons.md`).
- Default off is deliberate: a bare `aquaclean:` string renders only in Home Assistant with the
  icon-set module and (once shipped) on the bridge's own web page; other routes keep their own icons.
  Don't make it the default, or write docs that assume Home Assistant, without the matching frontends.

### Language requirements (non-negotiable)

- **Everything compiled into the firmware that a user can see must exist in all 29 languages — no
  exceptions, no "best-covered subset", and English fallback is not an acceptable end state.** This covers
  two stores, both mandatory for all 29: entity names and `options`/enum labels in every
  `lang/<code>.yaml`, **and** every `LT()` string (logs, the Home Assistant notifications, and the text of
  the bridge-served web pages `/setup`, `/homekit`, `/maintenance`), whose English source lives in
  `lang/log/<code>.json`. When you add or change an `LT()` string or any user-facing page, add its
  translation to **all 28** non-English `lang/log/*.json` in the same change — a string present in only
  some languages is a bug even though `lt()` falls back to English at runtime. `_log_table` tolerates that
  fallback; the project's policy does not. The project ships in 29 languages; keep every firmware-facing
  string at 29.
- Any user-facing string — entity names and `options`/enum labels — must be provided for **every
  supported language**, i.e. every `components/geberit_aquaclean/lang/<code>.yaml`. The supported set
  is the `LANGUAGES` list in `__init__.py`: Geberit's own languages **plus the ones we added to the
  firmwares ourselves**. Never skip a language, and never hardcode a language count — derive the set
  from the files / `LANGUAGES` so it can't drift.
- All language files keep an **identical key structure**; `en.yaml` is the reference and
  `tools/check_lang_keys.py` must pass. Each `options` array must have the **same length** in every
  language (enforced by `_load_languages`).
- Adding a language: append the code to `LANGUAGES` (at the **end** — the index is stored on the ESP),
  add `lang/<code>.yaml` with every key, then re-run any text-adding scripts so new strings land in it
  too. A patch script that adds strings must assert its coverage equals the on-disk `lang/*.yaml` set
  and fail loudly otherwise.
- Translations should be real localizations (native/standard technical terms), not English
  placeholders; where exact wording is uncertain, a later native-review pass is one line per file.
