"""Write an ESPHome *host* config that imitates the bridge's API surface (Dutch names, sub-devices).

Not the bridge firmware: plain template entities with the same names, devices and kinds, so the Homey app's
transport, name matching, state handling and commands can be tested against a real ESPHome API server.
"""
import sys, yaml
sys.path.insert(0, sys.argv[1] + "/components")
import geberit_aquaclean as g

L = g.I18N["nl"]
ent = {e.key: e for e in g.ENTITIES}
dev = lambda grp: g.DEVICE_IDS[grp]
name = lambda k: g._entity_name(L, ent[k], True)

cfg = {
    "esphome": {"name": "aquaclean-test", "friendly_name": "AquaClean Test",
                "devices": [{"id": i, "name": L["devices"][gr], "relation": "via"} for gr, i in g.DEVICE_IDS.items()]},
    "host": {},
    "logger": {"level": "INFO"},
    "api": {"port": 16053, "encryption": {"key": "6LzTxOw9e1XwhTSpdR7Gz2YSaM0Sv9nBr8bM2pE3dZs="}},
    "globals": [{"id": "shower", "type": "bool", "initial_value": "false"}],
    "binary_sensor": [
        {"platform": "template", "name": name("shower_active"), "device_id": dev("session"),
         "lambda": "return id(shower);"},
        {"platform": "template", "name": name("connected"), "lambda": "return true;"},
    ],
    "button": [
        {"platform": "template", "name": name("shower_toggle"), "device_id": dev("session"),
         "on_press": [{"lambda": "id(shower) = !id(shower);"},
                      {"event.trigger": {"id": "remote", "event_type": "shower_long"}}]},
    ],
    "event": [{"platform": "template", "id": "remote", "name": name("remote_button"), "device_id": dev("session"),
               "event_types": g.EVENT_TYPES}],
    "number": [
        {"platform": "template", "name": name(k), "device_id": dev(grp), "optimistic": True,
         "min_value": 1, "max_value": 5, "step": 1, "initial_value": 3}
        for k, grp in (("spray_intensity", "session"), ("profile_1_spray_intensity", "profile_1"),
                       ("profile_4_spray_intensity", "profile_4"))
    ],
    "select": [{"platform": "template", "name": name("light_color"), "device_id": dev("toilet"), "optimistic": True,
                "options": L["options"]["light_color"]}],
    "text": [{"platform": "template", "name": name("profile_2_user_name"), "device_id": dev("profile_2"),
              "optimistic": True, "mode": "text", "initial_value": "Anna"}],
    "datetime": [{"platform": "template", "type": "time", "name": name("light_window_1_start"),
                  "device_id": dev("toilet"), "optimistic": True, "initial_value": "22:00:00"}],
    "switch": [{"platform": "template", "name": name("profile_4_lady_oscillating_spray"), "device_id": dev("profile_4"),
                "optimistic": True}],
    "sensor": [{"platform": "template", "name": name("days_until_descaling"), "device_id": dev("toilet"),
                "lambda": "return 42;", "update_interval": "2s"}],
    "text_sensor": [{"platform": "template", "name": name("model"), "device_id": dev("toilet"),
                     "lambda": 'return {"AquaClean Sela"};'}],
}
yaml.safe_dump(cfg, sys.stdout, allow_unicode=True, sort_keys=False)
