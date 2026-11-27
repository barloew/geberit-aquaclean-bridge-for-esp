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
| ESPHome web page | — | Needs the bridge's own web-server bundle that resolves the prefix *(planned; not yet shipped — until then the web page shows a blank icon for these entities when `custom_icons` is on)*. |
| Homey | native API or MQTT | Own Homey app (SDK v3) that carries the SVG assets. |
| openHAB | MQTT | SVG files in `conf/icons/classic/` plus an item icon mapping. |
| Node-RED | MQTT | A custom SVG / `ui-template` in Dashboard 2.0 (`@flowfuse/node-red-dashboard`). |
| ioBroker | native API | Own SVG upload in VIS / VIS-2 or iQontrol. |
| Domoticz | MQTT | Manual per-device icon ZIP upload. |

None of the non-Home-Assistant systems read the `aquaclean:` string as a rich icon — they each have
their own icon mechanism — so enabling `custom_icons` does not take anything away there: they keep
whatever icon you configure in that system. The visible effect of `custom_icons` is in Home Assistant
(with the module installed) and, once it ships, on the bridge's own web page.

### Home Assistant — installing the icon-set

Manual install:

1. Copy `frontend/ha-custom-icons.js` to `config/www/geberit-aquaclean/ha-custom-icons.js` on the
   Home Assistant host.
2. Add it as a frontend module:

   ```yaml
   frontend:
     extra_module_url:
       - /local/geberit-aquaclean/ha-custom-icons.js
   ```

3. Restart Home Assistant and reload the browser (clear the cache if an icon does not appear).

The module registers `window.customIconsets['aquaclean']`, so any entity whose icon is
`aquaclean:<name>` — from this bridge over the native API or MQTT discovery — is drawn from the baked
SVG. Home Assistant draws it in the current theme colour (a monochrome icon).

Without the module, Home Assistant shows a blank icon for those entities — which is why the firmware
side is opt-in.
