<!-- Thanks for contributing! Keep this short; the checklist is what matters. -->

## What does this change?

<!-- One or two sentences. Link the issue it closes: "Closes #123". -->

## AI assistance (required)

State whether AI wrote or helped write this PR. Keep exactly one `AI-Assistance:` line below and set its
value to `none`, `assisted`, or `generated` (name the tool/model in parentheses for the last two). A CI
check enforces this.

AI-Assistance: TODO <!-- none | assisted (e.g. Claude) | generated (e.g. Claude) -->

## Type

- [ ] Bug fix
- [ ] New feature / entity
- [ ] New model support
- [ ] Docs / translations only
- [ ] Build / CI / tooling

## How was it tested?

<!-- Which board + model, which ESPHome version, and did it compile / run on real hardware? -->

## Checklist

- [ ] `esphome config` passes for at least one firmware YAML.
- [ ] C++ is clang-formatted.
- [ ] If an entity was added or renamed: **all 29 language files** have the matching key (`tools/check_lang_keys.py` passes).
- [ ] `CHANGELOG.md` has an entry under the unreleased section.
- [ ] Entity keys and English labels are US English.
