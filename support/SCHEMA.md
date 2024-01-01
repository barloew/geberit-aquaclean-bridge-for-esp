# Support matrix — schema (Series 250 / Alba)

`support/matrix.yaml` is the single source of truth for what this branch supports and what has been tested.
It uses the **same schema as the main (Series 248) branch** — model entries, `surfaces`, `routes_status`,
`boards`, `validations`, `device_firmware_seen`, the `status_vocab`
(`validated · supported · unsupported · planned · failed`), the stable ticket keys (`<model>/<surface>`,
`route/<route>`, `board/<board>`, `device-fw/<version>`), and the hard rule that a `validated` status
requires a `validation` record carrying both `bridge_release` and `device_firmware`. The two branches never
merge, so this branch keeps its own copy.

Deltas for Series 250:

- **`meta.maturity: experimental`**, and a per-model `maturity` field. This firmware is unverified and has
  not been run end-to-end against a real Alba, so `validations` is empty and **nothing is `validated` yet**.
  `supported` here means the relay/protocol is implemented in code and the user/device are supported on
  issues — not that we have confirmed it on hardware.
- **Extra surfaces**: `event_storage` (the 250-only fault-log read channel — Phase 1 inventory/read,
  Phase 2 live push) and `composed_lady_shower` (a lady shower the Alba has no native button for, composed by
  the bridge from a settings preset).
- **Profiles are app-/bridge-side**, not toilet-stored slots (the Alba keeps no recallable profiles); the
  `profiles` surface tracks the bridge's own up-to-six profiles applied as a settings batch.
- **Routes** (`native_api`, `mqtt`, `homekit`) are the VISION aim and are `planned` — not implemented in
  this firmware yet.

The first real-Alba validation (from `TEST_PLAN.md`) will add a `validations` record with the Alba's reported
device firmware and the bridge release, and promote the covered surfaces from `supported` to `validated`.
