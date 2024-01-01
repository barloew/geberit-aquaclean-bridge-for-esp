# Privacy: per-person usage data, and how to turn it off

Recognition is the real added value of this bridge: it knows **who** is approaching, loads their
profile, and — when they sit down — marks them the **active user**. Because the active user is known
at the same moment as the functions being used, the bridge can attribute usage to a person: who used
the toilet, which functions, how often.

That is powerful, and not everyone wants their household's toilet habits exposed in that much detail.
This is inherent to a lot of home automation — presence, room occupancy and device usage all expose
who-did-what — and the same judgement applies here. It is **opt-in**: nothing is attributed to a
person unless you link that person's device, and you decide which entities reach your home automation
at all. This page lists the entities involved and how to switch them off.

> None of this is forced on anyone. If you never link a device (never store an IRK), the bridge has
> no one to recognize: `recognized_user` and `active_user` stay empty and no usage is tied to a
> person. The statistics below still count device-wide totals, but without a name attached.

## The entities involved

Entity keys are the ones listed under `names` in
[`components/geberit_aquaclean/lang/en.yaml`](../components/geberit_aquaclean/lang/en.yaml). The
per-profile keys exist for profiles **1–6** (`profile_1_…` through `profile_6_…`).

**Who is present / using the toilet (identity):**

- `recognized_user` — name of the person last recognized approaching.
- `active_user` — name of the person currently seated and using the toilet.
- `profile_<N>_user_recognized` — per-profile "recognized" (presence) flag.
- `profile_<N>_user_active` — per-profile "active" (occupancy) flag.
- `profile_<N>_user_rssi` — per-profile device signal strength (a rough presence/proximity signal).

**Usage counted per person:**

- `stat_profile_1` … `stat_profile_4` — how often each profile was used.
- `stat_users` — number of user activations.

**Usage statistics (device-wide habits and patterns).** Not tied to a name on their own, but combined
with `active_user` they become per-person who-did-what:

- Counters: `stat_showers`, `stat_lady_showers`, `stat_oscillation`, `stat_lady_oscillation`,
  `stat_dryer`, `stat_odor`, `stat_seat_heating`, `stat_lid`, `stat_cleaning`, `stat_system_flush`.
- Timers: `stat_last_shower`, `stat_avg_shower`, `stat_last_lady`, `stat_avg_lady`,
  `stat_last_sitting`, `stat_avg_sitting`.
- Device totals (not personal): `stat_uptime`, `stat_run_time`.

## How to turn them off

There are three levels, from softest to hardest. All of them are set per entity in your YAML, by the
entity's key — the same mechanism as the README's *Renaming or hiding entities* section.

1. **Don't link anyone (feature-level opt-out).** Store no IRK for any profile. The bridge then
   recognizes no one, so `recognized_user` / `active_user` / the per-profile recognition flags never
   name a person. Nothing to configure — just don't link a device.

2. **`disabled_by_default: true` — created, offered to Home Assistant, but switched off there.** The
   entity still exists and is still exposed; Home Assistant lists it as disabled, and anyone with
   access can re-enable it. Use this when you want the entity available but not active by default.

3. **`internal: true` — built into the firmware but never exposed.** The entity is not registered
   with the native API, MQTT, Home Assistant or HomeKit at all, so it cannot reach your home
   automation. Use this to actually remove a statistic or an attribution entity from home automation.

### Example

```yaml
geberit_aquaclean:
  # Stop per-person attribution from reaching home automation entirely
  recognized_user:
    internal: true
  active_user:
    internal: true
  profile_1_user_recognized:
    internal: true
  profile_1_user_active:
    internal: true
  # Drop the per-profile usage counters
  stat_profile_1:
    internal: true
  # Keep a device-wide statistic, but disabled in Home Assistant until someone enables it
  stat_showers:
    disabled_by_default: true
```

Repeat the `profile_<N>_…` lines for each profile you want silent (1–6).

> **The IRK is a credential.** Linking a device stores its Identity Resolving Key. Anyone who has it
> can recognize and follow that device whatever address it uses, so treat it like a password. Set
> `log_obfuscate: true` to mask keys in the log, and delete the **Show IRK** notification once you've
> used it. See the README for the full IRK handling.
