# Security policy

This is a community hobby project; security reports are handled on a best-effort basis.

## Reporting a vulnerability

**Please do not open a public issue for a security problem.**

Use GitHub's private channel: the **Security** tab → **Report a vulnerability** (Privately report a
vulnerability). That opens a private advisory visible only to the maintainer, where details can be shared
safely.

When reporting, please:

- Describe the issue and how to reproduce it.
- **Do not include Series 250 (protocol v2) internals or the firmware's shared credential in any public
  place.** Keep those in the private advisory only — the protocol documentation is not public. (The Alba is
  currently one model that uses protocol v2; the protocol is not specific to it.)

## Supported versions

| Version | Supported |
| --- | --- |
| Latest release (main) | ✅ |
| Older releases | ❌ — upgrade first |
| `series-250-bridge` branch | ⚠️ experimental, best-effort |

## Scope

In scope: the ESPHome component and the Alba firmware in this repository. Out of scope: Geberit's own
devices, apps and cloud; third-party dependencies (report those upstream).
