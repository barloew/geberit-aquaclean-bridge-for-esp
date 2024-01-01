# Contributing

Thanks for helping improve the Geberit AquaClean Bridge for ESP.

**Working language: English.** Please write issues, pull requests and discussions in English.

## Reporting

- **Bug**, **feature** or **new model**: use the issue forms (New issue → pick a form). A bug needs a
  DEBUG-level ESPHome log and the Model/Article number.
- **Usage questions**: use [Discussions](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/discussions).
- **Field testing**: follow [docs/TEST_PLAN.md](../docs/TEST_PLAN.md) (Series 248) or the `series-250-bridge`
  branch's `TEST_PLAN.md` (Series 250) and post results in the **Field testing** Discussions category.
  When reporting anything, state your **connection route** and **install method** — see [docs/routes.md](../docs/routes.md).

## Pull requests

CI (`.github/workflows/validate.yml`) runs on every PR:

- `tools/check_lang_keys.py` — every file in `components/geberit_aquaclean/lang/` must have **exactly the
  same keys** as `en.yaml`. If you add or rename an entity, update all 29 language files and `en.yaml`.
- `ruff` on the Python component, `yamllint` on the translation files.
- `esphome config` for a matrix of boards × languages.
- `clang-format` on the C++ (style in `.clang-format`; run `clang-format -i` before pushing).

Please also:

- Keep entity keys and English labels **US English**.
- Add a `CHANGELOG.md` entry under the unreleased section.
- Don't add Series 250 / protocol v2 internals to the public docs.

## Releasing

Pushing a `v*` tag triggers `.github/workflows/build.yml`, which builds the firmware for every board and
language and publishes the web installer to GitHub Pages.
