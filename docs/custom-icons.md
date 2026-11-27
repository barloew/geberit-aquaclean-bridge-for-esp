# Custom icons (`aquaclean:`)

A few shower and spray-strength entities can show the bridge's own SVG icons instead of the stock
`mdi:` ones. This is **opt-in and off by default**, because the stock `mdi:` icons work everywhere
with nothing to install, while the custom icons only render where the `aquaclean:` prefix is known.

The entities covered, and the icon each uses:

| Entities | Icon |
|---|---|
| `shower_active`, `shower_toggle` | `aquaclean:shower` |
| `lady_shower_active`, `lady_shower_toggle` | `aquaclean:lady_shower` |
| `spray_intensity`, `default_spray_intensity` | `aquaclean:spray_intensity` |
| `lady_spray_intensity`, `default_lady_spray_intensity` | `aquaclean:lady_spray_intensity` |
| `seat_heating`, `default_seat_heating` | `aquaclean:seat_heating` |

The mapping is by icon: *every* entity that carries one of these stock `mdi:` icons gets the
matching SVG — the active session, the basic profile ("Standaardinstellingen", the `default_*`
entities), each user profile's clone (e.g. `profile_3_seat_heating`), and the shower-count
statistics — so a function's icon is the same everywhere it appears.

The SVGs live in [`images/ui/`](../images/ui/); `tools/icons/generate.py` bakes them into the
frontend module(s) that resolve the prefix.

## Turning it on (self-build)

Set `custom_icons` in the component config and rebuild:

```yaml
geberit_aquaclean:
  custom_icons: true   # default: false (stock mdi icons)
```

With it off, those entities keep their `mdi:` icons — the normal, universal behaviour. The choice is
made at build time (ESPHome stores an entity's icon as a compile-time value), so a web-installer
image carries whichever setting it was built with.

An icon you set yourself in YAML on one of these entities is left untouched; only the stock `mdi:`
default is replaced.

## Showing the icons on each platform

An `aquaclean:…` string is only a label. The entity sends it over the native API and over MQTT just
like an `mdi:` string, but each consumer has to know how to draw it. What the string reaches, and how
to make it render, differs per platform:

| Platform | Route | How to show the SVGs |
|---|---|---|
| Home Assistant | native API or MQTT | Install the `aquaclean` icon-set module (`frontend/ha-custom-icons.js`), then add it under `frontend.extra_module_url` (below). |
| ESPHome web page | — | Bake the device bundle (`frontend/esphome-www.js`) into `web_server` via `js_include` (below). Without it the web page shows a blank icon for these entities when `custom_icons` is on. |
| Homey | native API or MQTT | Own Homey app (SDK v3) that carries the SVG assets. |
| openHAB | MQTT | SVG files in `conf/icons/classic/` plus an item icon mapping. |
| Node-RED | MQTT | A custom SVG / `ui-template` in Dashboard 2.0 (`@flowfuse/node-red-dashboard`). |
| ioBroker | native API | Own SVG upload in VIS / VIS-2 or iQontrol. |
| Domoticz | MQTT | Manual per-device icon ZIP upload. |

None of the non-Home-Assistant systems read the `aquaclean:` string as a rich icon — they each have
their own icon mechanism — so enabling `custom_icons` does not take anything away there: they keep
whatever icon you configure in that system. The visible effect of `custom_icons` is in Home Assistant
(with the module installed) and on the bridge's own web page (with the device bundle baked in).

### Home Assistant — installing the icon-set

Manual install:

1. Copy `frontend/ha-custom-icons.js` to `config/www/geberit-aquaclean-icons/ha-custom-icons.js` on
   the Home Assistant host. The folder name is free, but it must match the `extra_module_url` path
   below exactly (a mismatch serves a 404).
2. Add it as a frontend module:

   ```yaml
   frontend:
     extra_module_url:
       - /local/geberit-aquaclean-icons/ha-custom-icons.js
   ```

3. Restart Home Assistant and reload the browser (clear the cache if an icon does not appear).

The module registers `window.customIconsets['aquaclean']`, so any entity whose icon is
`aquaclean:<name>` — from this bridge over the native API or MQTT discovery — is drawn from the baked
SVG. Home Assistant draws it in the current theme colour (a monochrome icon).

Without the module, Home Assistant shows a blank icon for those entities — which is why the firmware
side is opt-in.

### ESPHome web page — the device bundle

The ESP's own web page (`web_server` version 3) draws entity icons with [Iconify](https://iconify.design):
the icon string goes straight into an `<iconify-icon>`, which resolves `mdi:` from the Iconify API
(so stock `mdi:` icons there need internet) but doesn't know `aquaclean:`. `tools/icons/generate.py`
produces `frontend/esphome-www.js`: ESPHome's own v3 frontend plus a `window.IconifyPreload` that
registers the aquaclean icons. Baked into the firmware, the device page renders them **offline**.

Because `web_server: local: true` serves ESPHome's embedded page as-is (it never references a
`js_include`), enabling this means serving the page through our bundle instead. In your `web_server`
block:

```yaml
web_server:
  # ... your existing version/port/sorting_groups ...
  local: false            # our js_include replaces the embedded local page
  js_url: ""              # no external frontend fetch
  js_include: frontend/esphome-www.js   # path relative to your YAML; adjust if needed
```

Flash cost is roughly neutral: `local: false` drops ESPHome's embedded page, and the bundle it
replaces it with is about the same size (~34 KB compressed).

`frontend/esphome-www.js` embeds ESPHome's own frontend, read from its `server_index_v3.h`, so it is
tied to the ESPHome version. Regenerate it with the ESPHome you build with
(`python3 tools/icons/generate.py`, run with that ESPHome importable); `--check` fails if it is stale.
This is opt-in — leave `web_server` as it is (and the icons stay `mdi:`-only on the device page) if
you don't need it.
