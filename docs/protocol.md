# Geberit AquaClean — protocol overview

A high-level, functional overview of how the bridge talks to a Series 248 AquaClean (the Sela, and the
Mera / Tuma / Cama family). It deliberately contains **no reverse-engineering internals**: no wire frames,
data-point ids, command / status / error codes, enum value tables, GATT handles or CRC details. Those are
reverse-engineered from Geberit's own app and cryptographically verified against real captures, and they
live **only in the maintainer's private store, outside this public repository** — see
[`.github/SECURITY.md`](../.github/SECURITY.md) and [`.github/CONTRIBUTING.md`](../.github/CONTRIBUTING.md).

## How the bridge connects

A Geberit AquaClean accepts a single Bluetooth Low Energy connection at a time. The bridge takes that one
persistent, fully local connection and holds it, so the toilet's own remote control and one Geberit Home
app keep working alongside it — the bridge advertises as the toilet and relays for them. The link is
encrypted; the bridge pairs in the toilet's power-on pairing window, the same way the remote control does,
and re-establishes the encrypted link automatically if it drops.

Over that link the bridge:

- reads **status** — whether someone is seated, whether a shower, lady shower or dryer is running, the
  orientation light, and the error / service state;
- sends **functions** — start/stop the shower and lady shower, the dryer and odor extraction on models
  that have them, the orientation light, and the lid and flush where present;
- reads and writes **settings** — spray intensity and arm position, water temperature, seat and dryer
  heating, the orientation light's color, brightness and mode, water hardness and so on — both the active
  session values and the stored user profiles;
- reads **maintenance** information and runs maintenance procedures (descaling, arm cleaning, calibration);
- reads **device information** and **usage statistics** (model, firmware, serial number, counters).

Which functions a given model exposes is decided at runtime by feature discovery (see
[Models and feature discovery](../README.md#user-content-models-and-feature-discovery)); the bridge never assumes a
function the toilet does not report.

## User recognition

The bridge recognises a user's phone, watch or tracker by its Bluetooth Identity Resolving Key (IRK),
resolving the device's rotating random address locally. See [Users](../README.md#user-content-users) for how a profile
is linked to a device, and [`irk_capture.cpp`](../components/geberit_aquaclean/irk_capture.cpp) for how the
key is captured.

## Where the details live

Everything on the wire — the exact services and characteristics, the frame and message layout, the
data-point / command / status ids, the enum values, the CRC and the pairing handshake — is the authoritative
reverse-engineering work, kept **private and outside this repository** by the maintainer. It is deliberately
not published here; this overview is the public, function-level summary the component is built on. For the
sources of that work (what was analysed and where each piece is used), see [`SOURCES.md`](SOURCES.md).
