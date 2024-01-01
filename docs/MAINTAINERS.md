# Maintainer & build guide — `main` (Series 248 bridge)

The reference for how **this branch** is built, how the pieces fit together, how to keep it up to
date, and exactly how to reproduce every CI job **before** you push. Read this before a structural
change; it exists because the project has grown enough that "just try it" no longer scales.

> **This guide is for the `main` branch only.** The experimental `series-250-bridge` branch is a
> *separate firmware* (ESP-IDF 5.5 + NimBLE, built with `idf.py`, for the encrypted Series 250 Alba)
> and has **its own** `docs/MAINTAINERS.md` and its own changelog. The two branches share a name, a
> protocol lineage and the governance tooling — nothing in the build. **They are never merged into
> each other**, and each branch develops independently. If you are editing `.cpp/.h/.py` under
> `components/` you are in the right place; if you are editing `.c/.h` under `firmware/main/`, switch
> to the `series-250-bridge` branch and read its guide instead.

---

## 1. The Series 248 ESPHome component

### 1.1 What it is

An ESPHome *external component*. ESPHome turns a YAML config into a C++ firmware; our component
(`components/geberit_aquaclean/`) adds the AquaClean entities and the BLE man-in-the-middle logic.
There is no standalone binary — the firmware is produced by compiling a YAML config that pulls the
component in.

### 1.2 Repository map (`main`)

```
components/geberit_aquaclean/     the component itself
  __init__.py                     Python: config schema, the Ent entity registry, codegen (to_code)
  hub.{h,cpp}                     the toilet-facing client + relay + health/recovery (the core)
  remote_link.{h,cpp}             peripheral side: advertise as the toilet, remote + app links
  toilet_side.cpp                 the ESP presenting as the remote towards the toilet
  users.cpp                       user recognition (IRK), profiles, welcome light
  maintenance.cpp                 physical-maintenance sub-device
  homekit.cpp, homekit_qr.cpp     optional Apple HomeKit accessory
  entities.{h,cpp}, log_text.*    entity base classes, per-language log texts
  lang/<code>.yaml                29 translation files (names, options, devices, texts, …)
  homekit_sdk/, qrcodegen.*       VENDORED — never reformat, never hand-edit
packages/                         reusable YAML fragments (routes, boards, mqtt, web, api)
examples/atoms3-lite.yaml         a hand-written example config (the human-readable entry point)
firmware/aquaclean-<board>-<lang>.yaml   GENERATED ready-made configs (one per board × language)
static/index.html, boards.json   the web installer page (GENERATED boards.json)
static/series-250/                the Series 250 browser flasher hosted on this branch's Pages (see section 2)
tools/gen_firmware.py             GENERATES firmware/*.yaml, packages/mqtt_setup.yaml, static/boards.json, .github/workflows/build.yml
tools/check_lang_keys.py          verifies all 29 lang files have identical key structure
tools/overview/                   README overview infographic (overview.yaml + support/matrix.yaml → images/overview/)
tools/steps/                      README install-step cards (steps.json → images/steps/*.svg)
tools/badges/generate.py          README platform badges (logos in images/brands/ → images/badges/*.svg)
images/                           ALL images: overview, steps, badges, ui icons, illustrations, brands (see images/README.md)
docs/                             models.md, routes.md, protocol.md (private-safe), TEST_PLAN.md, SOURCES.md, this file
```

### 1.3 How it hangs together

- A **config** (`examples/atoms3-lite.yaml`, or a generated `firmware/*.yaml`) selects a **board**
  package and a **route** package and includes `packages/aquaclean.yaml` (the component + all
  entities).
- **Routes** are the two integration transports, chosen by one substitution `route: api|mqtt`:
  - `packages/route_api.yaml` = `api.yaml` + an API key → ESPHome native API (Home Assistant, Homey, ioBroker).
  - `packages/route_mqtt.yaml` = `mqtt.yaml` + `web.yaml` → MQTT (openHAB, Node-RED, Domoticz, …). The web
    installer adds `mqtt_setup.yaml` + `provisioning.yaml` so the broker is entered on the device's web page.
- **Boards** (`board-atoms3-lite.yaml`, `board-atoms3r.yaml`, `board-xiao-esp32s3.yaml`) set the
  ESP32-S3 target, pins and status LED.
- **Entities are not listed in the YAML.** The component's `_expand_entities()` injects every entity
  from the `Ent(...)` registry in `__init__.py` with defaults before validation. That is why adding
  an entity does **not** require touching any user's YAML or the generated firmware configs — see 1.5.

### 1.4 Build & run locally (`main`)

ESPHome 2026.10.2 is the pinned version (matches CI — see 3.1). Use a venv.

```bash
pip install esphome==2026.10.2
esphome config   examples/atoms3-lite.yaml     # validate only (fast; what CI runs)
esphome compile  examples/atoms3-lite.yaml     # full C++ build
esphome run      examples/atoms3-lite.yaml     # compile + flash + logs
```

To build exactly what ships, compile a generated file, e.g. `firmware/aquaclean-mqtt-atoms3-lite-nl.yaml`
(MQTT route) or `firmware/aquaclean-api-atoms3-lite-nl.yaml` (native-API route, runtime key via `/setup`).

### 1.5 The generation pipeline — edit the generator, not its output

`tools/gen_firmware.py` is the source of truth for several files. **These are generated; never edit
them by hand** (a header comment says so on each):

- `firmware/aquaclean-<board>-<lang>.yaml` — the ready-made MQTT configs (board × language).
- `packages/mqtt_setup.yaml` — the on-device MQTT setup form (50 time zones from tzdata).
- `static/boards.json` — the board list for the installer page.
- `.github/workflows/build.yml` — the CI build matrix.

Regenerate after changing the board set, the language set, or the generator itself:

```bash
python3 tools/gen_firmware.py      # rewrites the generated files above
git diff --stat                    # review what changed, commit generator + output together
```

You do **not** need to regenerate for an ordinary component or translation change (new entity, new
lang value, C++ logic): entities auto-inject (1.3) and the generated configs don't enumerate them.
Regenerate only when board/language coverage or the generator's templates change.

### 1.6 Adding or changing an entity (end to end)

1. Add an `Ent("key", "<domain>", <group>, {...})` line in `__init__.py` (`ENTITIES`). Domains map to
   schemas in `_schema_for` and to C++ setters in `to_code` (`set_<key>_sensor`, etc.). For a new
   gauge/measurement sensor use the `gauge` domain; for an enum text sensor use `status_enum` with a
   synthetic `setting=(index,)` and an `options.<key>` list.
2. Add the matching setter + member in `hub.h` (or the relevant class) and publish from C++.
3. Add the entity **name** under `names:` and, for an enum, the **states** under `options:` in **all 29**
   `lang/*.yaml` files — English + the languages you can do properly, English placeholder for the rest.
   The key and (for `options`) the list length must be identical across all 29 or `check_lang_keys.py`
   fails. Diagnostics get `CONF_ENTITY_CATEGORY: DIAG` (disabled by default).
4. Run the local checks in 3.1 before pushing.

### 1.7 Vendored code

`components/geberit_aquaclean/homekit_sdk/**` and `qrcodegen.*` are third-party. They are excluded from
clang-format and ruff. Do not reformat or hand-patch them; update by re-vendoring upstream.

---

## 2. The Series 250 flasher that this branch hosts

The only Series 250 thing that lives on `main` is the **browser flasher** under `static/series-250/`
(`index.html`, `manifest.json`, `README.md`), published by this branch's Pages build (3.2). It is a
hosting slot, not the firmware: the `firmware.bin` it serves is built on the `series-250-bridge`
branch (`idf.py build` + a merged binary) and committed into `static/series-250/` here by the owner.
`static/series-250/README.md` has the exact build-and-commit steps. Keeping the flasher page current
is a `main` task; everything about the firmware itself belongs to the other branch and its guide.

---

## 3. CI/CD — every job, and how to reproduce it locally

Reproduce each job locally before pushing. The versions below are pinned in CI; keep local = CI.

### 3.1 `main` — `.github/workflows/validate.yml` (push to `main`, every PR, manual)

| Job | What it does | Reproduce locally |
|---|---|---|
| `lang-keys` | all 29 lang files match `en.yaml` | `pip install pyyaml && python3 tools/check_lang_keys.py` |
| `yamllint` | lint the translation YAML | `pipx install yamllint && yamllint -c .yamllint components/geberit_aquaclean/lang` |
| `ruff` | lint the Python component + tools | `uvx ruff@0.15.16 check components/geberit_aquaclean tools` |
| `clang-format` | C++ format (own sources; skips `qrcodegen.*` and `homekit_sdk/`) | `clang-format --dry-run --Werror $(find components/geberit_aquaclean \( -name '*.cpp' -o -name '*.h' \) ! -name 'qrcodegen.*')` |
| `overview` | the overview infographic matches its source and the matrix | `pip install pyyaml fonttools && python3 tools/overview/generate.py --check` |
| `esphome-config` | `esphome config` for boards {atoms3-lite, atoms3r, xiao-esp32s3} × langs {en, nl, de} | `esphome config firmware/aquaclean-<board>-<lang>.yaml` |

Pinned: Python 3.12, ESPHome **2026.10.2**, ruff **0.15.16**. `clang-format` config is `.clang-format`
(`SortIncludes: false`); `yamllint` config is `.yamllint`.

### 3.2 `main` — `.github/workflows/build.yml` (on tag `v*`, or manual) — **generated by `gen_firmware.py`**

- `build`: matrix **board {atoms3-lite, atoms3r, xiao-esp32s3} × 29 languages** → `esphome/build-action@v7`
  with ESPHome `2026.10.2`, `complete-manifest: true`, building each `firmware/aquaclean-<board>-<lang>.yaml`;
  uploads per-combo artifacts. This builds the **MQTT-route** ready-made firmware.
- `pages`: assembles `site/` = `static/index.html` + `static/boards.json` + `static/series-250/` + all
  firmware artifacts under `site/firmware/`, then `actions/deploy-pages`. Needs repo **Settings → Pages →
  Source: GitHub Actions**, and the workflow's `pages: write` / `id-token: write` permissions (already set).
- Trigger a release: push a tag (see 5). Locally you can't run Pages, but you can dry-run a single build with
  `esphome compile firmware/aquaclean-mqtt-atoms3-lite-en.yaml` (or `-api-` for the native-API route).
  **If you change the matrix, change
  `gen_firmware.py` and regenerate — never hand-edit `build.yml`.**

### 3.3 Shared governance workflows

These exist on `main` (and most are mirrored on the other branch, which documents its own copies):

- **Claude** — `claude.yml` (PR review/comment `@claude`) + `claude-triage.yml` (issues and issue comments,
  repo-wide). Issue triage is handled repo-wide from `main`; the `series-250-bridge` branch deliberately runs
  only a PR-scoped `claude.yml` so it isn't duplicated. Both use repo secret `ANTHROPIC_API_KEY` and the Claude
  GitHub App.
- **`pr-ai-disclosure.yml`**: every PR description must contain `AI-Assistance: none|assisted|generated`
  (see `pull_request_template.md`) or the check fails.
- **`labeler.yml`**: path-based PR labels from `.github/labeler.yml`. **`labels.yml`**: creates the label set
  from `.github/labels.yml`.
- **`.coderabbit.yaml`**: CodeRabbit review config (`language: en`).
- **Dependabot** (`.github/dependabot.yml`): keeps GitHub Actions versions current.
- **`CODEOWNERS`, `CONTRIBUTING.md`, `SECURITY.md`** under `.github/`: review ownership, contribution rules
  (English working language; AI disclosure), and the private security-report channel. Security or captured-protocol
  findings go through `SECURITY.md`, never a public issue/PR.

---

## 4. Dependencies — inventory & keeping current

Everything this branch depends on, where its version lives, and how it is tracked. Only the first two rows are
automated; the rest are **manual** — Dependabot cannot see version literals in workflow strings, and it never
touches vendored source.

| Dependency | Kind | Pinned / recorded in | Tracking | Notes |
|---|---|---|---|---|
| GitHub Actions | CI actions (`uses:`) | the workflow files | **Dependabot** (weekly, grouped) | auto-PRs |
| `tools/steps` npm | build-time (README step-card SVGs) | `tools/steps/package.json` | **Dependabot** (monthly) | auto-PRs |
| Inter fonts (OFL-1.1, Latin subset) | build-time (overview text measurement and PNG) | `tools/overview/fonts/` | manual | re-subset only when the overview needs new characters |
| ESPHome `2026.10.2` | framework / compiler | `build.yml` (via `gen_firmware.py`) **and** `validate.yml` | manual | keep both in sync |
| ruff `0.15.16` | Python linter | `validate.yml` (`uvx ruff@`) | manual | |
| Python `3.12` | CI runtime | `validate.yml` setup-python | manual | |
| `homekit_sdk/` | **vendored C (patched fork)** | `homekit_sdk/README.md` — rednblkx fork, branch `esphome`, commit `1d1ea30`, with local patches | manual re-vendor | excluded from lint/format |
| `qrcodegen.{c,h}` | **vendored C** (Nayuki, MIT) | provenance header in `qrcodegen.h` | manual re-vendor | excluded from lint/format |
| `esp_hap_core` managed deps (`json_generator`, `json_parser`, `mdns`) | ESP Component Registry (IDF component manager) | version constraints in `homekit_sdk/esp_hap_core/idf_component.yml` | manual | fetched at build time |
| libsodium, mbedtls | provided by ESPHome | — | moves with ESPHome | our build adds the libsodium ESPHome already uses (see `homekit_sdk/README.md`) |

### How to update

- **Automated (Dependabot).** Review the PR, let CI (3.1) run, merge if green. Nothing else.
- **Toolchain pins** (ESPHome / ruff / Python). Change the literal in the workflow(s) — for ESPHome change
  `gen_firmware.py` and regenerate `build.yml`, and change `validate.yml` — update this table, then run the 3.1
  checks locally (for ESPHome: `esphome config` on all three boards). Keep local == CI.
- **Vendored source** (`homekit_sdk/`, `qrcodegen.*`). These are patched/own-copied, so never blind-pull:
  1. check upstream for security/bug fixes against the recorded commit;
  2. re-vendor from upstream at the new commit into a scratch copy;
  3. re-apply the local patches listed in the provenance (for `homekit_sdk/` they are in its README);
  4. diff against the current tree, build, and test (for HomeKit: pairing + the QR output);
  5. update the recorded commit/tag and the "Local changes" note in the provenance.
- **ESP Component Registry deps** (`idf_component.yml`): bump the version constraint there and rebuild; the IDF
  component manager fetches the new version at build time.

---

## 5. Release process

1. Move the `## vX.Y.0 (not released yet)` section in `CHANGELOG.md` to a dated release, add a new unreleased
   section on top.
2. Run the 3.1 checks locally; they must be green.
3. Tag and push: `git tag vX.Y.0 && git push origin vX.Y.0`. `build.yml` builds all board×language firmware and
   publishes the web installer to GitHub Pages.
4. Verify the Pages deployment and the installer page.

---

## 6. Guardrails that keep the project from sprawling

- **Generated files are not edited by hand.** `firmware/*.yaml`, `packages/mqtt_setup.yaml`, `static/boards.json`
  and `.github/workflows/build.yml` are outputs of `gen_firmware.py`. Change the generator and regenerate.
- **The 29-language invariant.** Every entity name/enum exists in all 29 lang files with identical keys and list
  lengths. `check_lang_keys.py` is the gate; run it before every push that touches entities or `lang/`.
- **Two branches stay apart.** No cross-merges. A fix usually applies to exactly one framework.
- **Protocol internals stay private.** The Series 250 "protocol v2" / Arendi internals (DpIds, frames, keyset/trust)
  are never committed to a public file or published; only function names appear in user-facing pages.
- **AI authorship is disclosed** on every PR, and the English working language holds across issues/PRs/discussions.
- **Vendored code is left alone** (`homekit_sdk/`, `qrcodegen.*`): excluded from formatting/lint, updated only by
  re-vendoring.
