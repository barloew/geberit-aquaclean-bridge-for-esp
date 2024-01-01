# Support matrix — schema

`support/matrix.yaml` is the **single source of truth** for what the bridge supports and what has been
tested, per model, feature, route and board. Human docs (README, `docs/models.md`, the overview image) and
release notes are **derived from or checked against** this file; nothing else authors support claims.

This document is the contract for both maintainers and the CI/CD agents that read the file.

## Top-level keys

| Key | Meaning |
|---|---|
| `meta` | Branch (248 = main) and the `status_vocab`. |
| `surfaces` | Canonical feature keys → human label. A key is half of a ticket id (`<model>/<surface>`). |
| `routes` | Integration route keys (native_api, mqtt, homekit) → label. |
| `models` | One entry per model, keyed by slug. |
| `routes_status` | Per-route status (bridge-level, not per model). |
| `boards` | Target ESP32-S3 boards and their build/test status. |
| `validations` | Evidence records. Every `validated` status points at one by id. |
| `device_firmware_seen` | Ledger of AquaClean **device** firmware versions observed in the field. |

## Status vocabulary (`meta.status_vocab`)

| Status | Meaning | Needs a validation record? |
|---|---|---|
| `validated` | Reproduced on real hardware. | **Yes** — with `bridge_release` **and** `device_firmware`. |
| `supported` | The code supports it (feature discovery / public SAP cross-check) and the user + device are supported on issues; not hardware-tested by us. | No |
| `unsupported` | The model/route lacks this surface. | No |
| `planned` | On the roadmap, not implemented. | No |
| `failed` | Known broken / regressed. | Link an `issue`. |

Boards use `validated` / `builds` (compiles, not hardware-tested).

## Model entry

```yaml
<slug>:
  name: "AquaClean <Model>"
  series: 248                       # 248 (main) or 250 (Alba branch)
  sap_prefix: ["146.xx", ...]       # public Geberit SAP article prefixes (device-reported via 0x10000)
  status: validated|supported|unsupported
  validation: <id>                  # required iff status: validated
  surfaces:                         # optional; omit when status covers the whole model
    <surface_key>: validated|supported|unsupported|...
  note: "free text"                 # e.g. 'feature set resolved at runtime by feature discovery'
```

A model marked `supported` whole need not enumerate surfaces — feature discovery resolves them on the device
at runtime, and the matrix should not assert a feature set it has not seen. Enumerate `surfaces` only where
there is concrete per-feature information (as for the Sela).

## Validation record (`validations[]`)

```yaml
- id: <slug>-<short-label>          # dateless (e.g. sela-hw)
  model: <slug>
  status: validated
  bridge_release: "vX.Y.Z"          # REQUIRED — our release at validation time
  device_firmware: "RS08 TS57"      # REQUIRED — what the toilet reported (main control node)
  by: "maintainer|<tester>"
  evidence: "hardware + capture/log reference by id"   # never RE internals; no dates
```

`bridge_release` + `device_firmware` are mandatory: a validation that does not name both the version of our
code and the version of the device firmware it ran against is not proof. A validation is **scoped to that
device firmware** — it says nothing about another firmware version.

## Device-firmware ledger (`device_firmware_seen`)

```yaml
"RS08 TS57":
  model: <slug>
  status: known-good|untested|known-broken
  bridge_release: "vX.Y.Z"
  issue: "<ref>"                    # when known-broken
```

The AquaClean device firmware is a break vector: a Geberit OTA can change protocol or behaviour. Any firmware
version **not** in this ledger is treated as `untested` (may break) until a validation covers it. A field
report of a new device firmware adds an `untested` row and should open a `device-fw:<version>` ticket.

## Build checks (`build_checks`) — device-independent

Structural, CI-verified facts that are **not** hardware validations: they carry no `device_firmware`, are
not tied to a model, and must never be rendered as "tested on hardware". Example: the 29 language files are
verified for identical key structure by `tools/check_lang_keys.py` (status `structural`), but translation
quality is a separate, non-hardware matter. When a generated caption says "29 languages", it is sourced from
here — a presence/structural fact — not from `validations`. Statuses here are `structural` (CI-verified) or
`unverified`.

## Ticket keys (for the issue tracker)

Stable, derivable identifiers so a failure maps to exactly one ticket:

- feature: `<model>/<surface>` — e.g. `sela/last_error`, `mera-comfort/profiles`
- route: `route/<route>` — e.g. `route/mqtt`
- board: `board/<board>` — e.g. `board/xiao-esp32s3`
- device firmware: `device-fw/<version>` — e.g. `device-fw/RS08 TS57`

Suggested labels: `model:<slug>`, `feature:<surface>`, `route:<route>`, `status:<status>`,
`device-fw:<version>`, `release:<vX.Y.Z>`.

## What CI / agents must enforce

1. **Validate** `matrix.yaml` against this schema (keys, status vocabulary, required fields).
2. **No `validated` without evidence** — reject a `validated` status whose `validation` record lacks
   `bridge_release` or `device_firmware`, or points at a missing id.
3. **Drift guard** — no generated doc, badge or image caption may claim `tested`/`validated` for a
   `model × surface`/route whose matrix status is not `validated`. Re-generate derived docs and fail if the
   working tree is stale (Phase 2 adds the generator).
4. **Release gating** — release notes state the device firmware(s) and bridge release each claim was validated
   against, taken from `validations`. A `known-broken` device firmware is a release risk flag.
5. **Field intake** — a new device firmware or a `failed` surface opens/updates the matching ticket by its key.

## Scope

Series 250 (Alba) lives on the `series-250-bridge` branch and keeps its own `support/matrix.yaml` in the same
schema (its surfaces add e.g. `event_storage`; profiles are app-/bridge-side). The two branches never merge.
