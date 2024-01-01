# Agent guide — Series 250 bridge (experimental)

Read this first. It is auto-loaded for Claude Code / Claude agents. It contains **no secrets**.

## This branch

A **separate, experimental** ESP-IDF 5.5 / NimBLE firmware for the Geberit AquaClean **Alba**
(Series 250, encrypted "protocol v2"). Firmware in `firmware/main/`; build with `idf.py`. It is
**not** verified end-to-end on real hardware yet, and it **never merges into `main`** (the Series
248 ESPHome component is a different stack).

## The private reverse-engineering (RE) basis — consult it, never copy it

This firmware's crypto and codec come from reverse-engineering the decompiled Geberit Home app and
**cryptographically verifying the v2 handshake against real captures** — not from third-party code.
That RE work (the `Geberit_AquaClean_RE.zip` family, `Protocolversies_v1_v2.md`, `Foutcodes.md`) is
the authoritative source and is **private, kept outside this repository**.

**Where it lives (convention).** The maintainer keeps it unpacked at a **fixed sibling path next to
this checkout**: `../geberit-aquaclean-re/` (alongside the repo, never inside it; `.gitignore`
blocks `/re/` and the package files anyway). Look there first.

**Before changing anything in `arendi_*`, the datapoint table, the command set, or the error
handling, consult the RE package first** (start at `00_LEESMIJ.md` →
`documenten/Protocolversies_v1_v2.md` for the handshake, `documenten/Geberit_AquaClean_BLE_referentie.md`,
`documenten/Foutcodes.md`, and `data_csv/`). Look for it at `../geberit-aquaclean-re/` or attached
to your session; if it is **neither**, ask the maintainer rather than guessing or copying from
jens62/torp93 (secondary cross-checks only). The full canonical spec and the key facts are
summarised in `MAINTAINERS.md` §0.

## Secrecy guardrails (non-negotiable)

Same rules as `main` (see that branch's `CLAUDE.md`), plus this branch's specifics:

1. **Never commit the credential/PSK value or the detailed RE prose spec** — the
   `Geberit_AquaClean_RE.zip` family, `Protocolversies_v1_v2.md`, `Foutcodes.md`, `errorcodes_*.csv`,
   captures (`*.pcap`) — to this repo or any published artifact (tarball, issue,
   discussion, PR, review). The credential lives **only** in the git-ignored `aquaclean_bridge_id.c`
   (copy it from `aquaclean_bridge_id.example.c`); `.gitignore` blocks the rest — don't defeat it.
   The one deliberate exception is the credential compiled into the distributed Alba firmware: the web
   installer publishes a `.bin` (built in CI from the `ALBA_BRIDGE_ID_C` secret) that necessarily
   contains it, because the firmware needs it to authenticate. The **value** is still never published as
   text — not in source (beyond the git-ignored `aquaclean_bridge_id.c`), docs, logs, issues, PRs, review
   or chat.
   The **firmware source is public** on this experimental branch: it *is* the implementation, so
   `arendi_*` (framing/handshake) and the generated `alba_dp_table.h` (DpIds/enums) are part of the
   published code. `alba_dp_table.h` is **generated** from the private material — regenerate it, never
   hand-edit it, and never commit its generator or the private source it reads.
2. **User-facing pages name functions, not internals.**
3. **The credential is not in the committed source** — it lives only in the git-ignored
   `aquaclean_bridge_id.c` and in the binaries you distribute; never echo its value into logs, docs,
   comments, or chat.
4. **Terminology:** this is a **bridge**, never a "proxy"; keep "anal" out of the docs (use
   "shower").

Enforced by `MAINTAINERS.md` §0 and `.coderabbit.yaml` (reviewer is told never to request or
reproduce the internals).

## Integration routes — keep examples balanced (do not over-center Home Assistant)

The target integration is the same as the main bridge: **two equal first-class routes — the
ESPHome native API and MQTT — plus optional Apple HomeKit (which can run stand-alone).** Home
Assistant is one consumer among several, never the default or the assumed platform. In docs,
examples and use cases, lead with the **route**, not one hub, and rotate concrete examples across
the ecosystem — **openHAB, Node-RED, Domoticz, Homey, ioBroker and Apple Home** — rather than
reaching for Home Assistant every time. A reader on any of those should see themselves in the docs
as clearly as an HA user. (See the main branch `CLAUDE.md` for the full rule.)

## House rules

- The maintainer runs all `git`/push and compiles on a LilyGO T-Dongle-S3 themselves.
- An honest FAIL with a good log is the most useful field-test result; see `TEST_PLAN.md`.
