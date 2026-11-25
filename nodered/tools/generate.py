#!/usr/bin/env python3
"""Generate the Node-RED flow and its entity reference from the bridge component.

Run with a Python that has ESPHome installed (the component imports it), from the repository root:

    python3 nodered/tools/generate.py

Reads components/geberit_aquaclean/__init__.py (ENTITIES), lang/*.yaml, the web-installer firmware files (names of
the setup entities) and iobroker/tools/descriptions.py (what every entity does), and writes:

  nodered/flow.json   the flow to import into Node-RED / to publish on flows.nodered.org
  nodered/README.md   the entity reference between <!-- entities:begin --> and <!-- entities:end -->

Over MQTT the bridge builds every topic from the entity name in its firmware language:
<prefix>/<component>/<object id>/state|command. The flow recognizes an entity by a hash of "<component>/<object id>"
in all bridge languages, so msg.topic is the same key whatever language the firmware speaks.

Only names, kinds, units and value ranges are exported: nothing about the toilet protocol.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NR = HERE.parent
ROOT = NR.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "iobroker" / "tools"))
from descriptions import D as _D  # noqa: E402
from ui_strings import EXTRA_BUTTONS, EXTRA_LANGUAGES, EXTRA_OPTIONS, UI, UI_KEYS  # noqa: E402

# descriptions shared with the ioBroker adapter; a few say something adapter-specific
D = dict(_D)
D["user_ha_device"] = ("Home Assistant device tracker linked to this profile (Home Assistant only; elsewhere use an "
                       "IRK instead).", "")

# PLATFORM (internal domain of the component -> ESPHome/MQTT component) is imported from the component in build().
UNITS = {"stat_hours": "h", "stat_seconds": "s", "days": "d", "gauge": "B", "rssi": "dBm", "user_rssi": "dBm",
         "threshold": "dBm"}
WRITABLE = {"switch", "light", "button", "number", "select", "time", "text"}
OPTION_KEY = {"user_welcome": "welcome_light"}

# Entities of the web-installer firmware outside the component: key, component, substitution with the name
# (None: the name is the same in every language)
SETUP = [
    ("restart", "button", None, "Restart"),
    ("mqtt_broker", "text", "mqtt_setup_broker", None),
    ("mqtt_port", "number", "mqtt_setup_port", None),
    ("mqtt_user", "text", "mqtt_setup_user", None),
    ("mqtt_password", "text", "mqtt_setup_password", None),
    ("mqtt_topic_prefix", "text", "mqtt_setup_topic_prefix", None),
    ("mqtt_connect", "button", "mqtt_setup_connect", None),
    ("time_zone", "select", "tz_label", None),
]
SETUP_DOC = {
    "restart": ("Restart", "Restarts the bridge."),
    "mqtt_broker": ("MQTT broker", "Web-installer firmware: host name or address of the MQTT broker."),
    "mqtt_port": ("MQTT port", "Web-installer firmware: port of the MQTT broker (1–65535)."),
    "mqtt_user": ("MQTT user", "Web-installer firmware: user name at the broker."),
    "mqtt_password": ("MQTT password", "Web-installer firmware: password at the broker; it is never shown again."),
    "mqtt_topic_prefix": ("MQTT topic prefix", "Web-installer firmware: base of every topic of the bridge."),
    "mqtt_connect": ("Connect to MQTT broker", "Web-installer firmware: stores the MQTT settings and restarts."),
    "time_zone": ("Time zone", "Web-installer firmware: time zone of the bridge's clock (the option text)."),
}


def load_component():
    sys.path.insert(0, str(ROOT / "components"))
    import geberit_aquaclean as g  # noqa: PLC0415
    return g


def object_id(name: str) -> str:
    """ESPHome's object id: per UTF-8 byte, ' ' -> '_', A-Z -> a-z, anything but [a-z0-9_-] -> '_'."""
    out = []
    for b in name.encode("utf-8"):
        c = chr(b)
        if c == " ":
            c = "_"
        elif "A" <= c <= "Z":
            c = c.lower()
        if not (c in "-_" or "0" <= c <= "9" or "a" <= c <= "z"):
            c = "_"
        out.append(c)
    return "".join(out)


def fnv36(s: str) -> str:
    h = 0x811C9DC5
    for ch in s.encode("ascii"):
        h ^= ch
        h = (h * 0x01000193) & 0xFFFFFFFF
    n, digits = h, "0123456789abcdefghijklmnopqrstuvwxyz"
    out = ""
    while True:
        n, r = divmod(n, 36)
        out = digits[r] + out
        if n == 0:
            return out


def group_of(e) -> str:
    if e.group is None:
        return "bridge"
    return "profile" if e.group.startswith("profile_") else e.group


def leaf_of(e) -> str:
    return re.sub(r"^profile_\d_", "", e.key) if group_of(e) == "profile" else e.key


def base_key(e) -> str:
    """Key in descriptions.D: profile N folded."""
    if group_of(e) == "profile":
        b = leaf_of(e)
        return f"{b}@profile" if f"{b}@profile" in D else b
    return re.sub(r"profile_\d", "profile_N", e.key)


def setup_names(code: str) -> dict[str, str]:
    """Names of the setup entities in the web-installer firmware of one language."""
    files = sorted((ROOT / "firmware").glob(f"aquaclean-mqtt-*-{code}.yaml"))
    if not files:
        raise SystemExit(f"no MQTT firmware file for language {code}")
    subs = dict(re.findall(r'^  (\w+): "?(.*?)"?\s*$', files[0].read_text(encoding="utf-8"), flags=re.M))
    out = {}
    for key, _comp, sub, fixed in SETUP:
        out[key] = fixed if sub is None else subs[sub]
    return out


def mqtt_lang(g, code):
    # MQTT builds topics from the name at runtime: scripts without Latin letters keep the English names
    return g.I18N["en"] if code in g.ASCII_ID_LANGUAGES else g.I18N[code]


def build(g):
    std = [e for e in g.ENTITIES if not e.discovery and not e.disabled]
    missing = sorted({base_key(e) for e in std} - set(D))
    if missing:
        raise SystemExit(f"no description for: {', '.join(missing)}")

    # internal domain -> ESPHome/MQTT component, from the component so the topics stay in step with the firmware
    PLATFORM = g.PLATFORM

    # ------------------------------------------------------------ owners of every MQTT topic, per language
    # The bridge prefixes the profile groups' names over MQTT (see _entity_name(..., mqtt=True) in the component),
    # so every entity has its own topic: a profile entity is its own key (profile_<n>_<leaf>), writable like any
    # other. We still check across all bridge languages that no two entities ever share one <component>/<object id>.
    topics: dict[str, str] = {}          # "<component>/<object id>" -> owner key
    owner_of: dict[str, str] = {}        # entity key -> owner key
    for code in g.LANGUAGES:
        tr = mqtt_lang(g, code)
        by_topic: dict[str, list] = {}
        for e in g.ENTITIES:
            t = f"{PLATFORM[e.domain]}/{object_id(g._entity_name(tr, e, True, mqtt=True))}"
            by_topic.setdefault(t, []).append(e)
        for key, comp, _sub, _fixed in SETUP:
            by_topic.setdefault(f"{comp}/{object_id(setup_names(code)[key])}", []).append(key)
        for t, members in by_topic.items():
            ents = [m for m in members if not isinstance(m, str)]
            setups = [m for m in members if isinstance(m, str)]
            if len(members) != 1:
                keys = [m if isinstance(m, str) else m.key for m in members]
                raise SystemExit(f"{code}: topic {t} shared by {keys} (the bridge should give every entity a "
                                 "unique name over MQTT)")
            owner = setups[0] if setups else ents[0].key
            for m in ents:
                if owner_of.setdefault(m.key, owner) != owner:
                    raise SystemExit(f"{m.key}: owner {owner_of[m.key]} in one language, {owner} in {code}")
            if topics.setdefault(t, owner) != owner:
                raise SystemExit(f"topic {t} means {topics[t]} in one language and {owner} in {code}")

    # ------------------------------------------------------------ entity records (by owner key)
    recs: dict[str, dict] = {}
    rep: dict[str, object] = {}      # owner key -> representative entity
    for e in g.ENTITIES:
        owner = owner_of[e.key]
        if owner in recs:
            continue
        p = PLATFORM[e.domain]
        r = {"p": p, "g": group_of(e)}
        if p in WRITABLE and e.domain not in ("user_irk",):
            r["w"] = 1
        if p == "number":
            if e.domain == "threshold":
                r["min"], r["max"] = -100, -30
            else:
                _kind, _sid, maximum, offset = e.setting[:4]
                r["min"], r["max"] = offset, maximum + offset
        unit = UNITS.get(e.domain) or e.defaults.get("unit_of_measurement")
        if unit:
            r["u"] = unit
        if e.domain == "timestamp" or e.defaults.get("device_class") == "timestamp":
            r["ts"] = "s"
        if e.domain == "uptime":
            r["ts"] = "iso"
        if p == "select" or (e.domain == "status_enum" and e.key != "last_error_text"):
            groups = [OPTION_KEY.get(e.domain, e.key)]
            groups += [ok for k, _cap, ok in g.OPTION_VARIANTS if k == e.key]
            r["o"] = groups
        if e.discovery or e.disabled:
            r["adv"] = 1
        recs[owner] = r
        rep[owner] = e
    for key, comp, _sub, _fixed in SETUP:
        r = {"p": comp, "g": "bridge", "setup": 1}
        if comp in WRITABLE:
            r["w"] = 1
        if key == "mqtt_port":
            r["min"], r["max"] = 1, 65535
        recs[key] = r

    keys = list(recs)
    index = {k: i for i, k in enumerate(keys)}
    hashed: dict[str, int] = {}
    for t, owner in topics.items():
        h = fnv36(t)
        if hashed.get(h, index[owner]) != index[owner]:
            raise SystemExit(f"hash collision on {t}")
        hashed[h] = index[owner]

    # ------------------------------------------------------------ labels
    groups = sorted({grp for r in recs.values() for grp in r.get("o", [])})
    options = {}
    for grp in groups:
        options[grp] = {c: g.I18N[c]["options"][grp] for c in g.LANGUAGES}
        for c, extra in EXTRA_OPTIONS.items():
            assert len(extra[grp]) == len(options[grp]["en"]), (c, grp)
            options[grp][c] = extra[grp]
    buttons = {c: {"names": g.I18N[c]["buttons"], "states": g.I18N[c]["button_states"]} for c in g.LANGUAGES}
    for c, (names, states) in EXTRA_BUTTONS.items():
        assert set(names) == set(g.I18N["en"]["buttons"]), c
        buttons[c] = {"names": names, "states": states}
    ui_langs = list(g.LANGUAGES) + list(EXTRA_LANGUAGES)
    missing_ui = [c for c in ui_langs if c not in UI]
    if missing_ui:
        raise SystemExit(f"ui_strings.UI lacks {missing_ui}")
    for c in ui_langs:
        assert len(UI[c]) == len(UI_KEYS), c
        for i, s in enumerate(UI[c]):
            want = set(re.findall(r"{\w+}", UI["en"][i]))
            assert set(re.findall(r"{\w+}", s)) == want, (c, UI_KEYS[i])

    tables = {
        "languages": g.LANGUAGES,
        "fallback": EXTRA_LANGUAGES,
        "keys": keys,
        "e": recs,
        "topics": hashed,
        "options": options,
        "buttons": buttons,
        "uiKeys": list(UI_KEYS),
        "ui": {c: list(UI[c]) for c in ui_langs},
    }
    return std, recs, rep, tables, ui_langs


# ---------------------------------------------------------------------------------------------- flow


def js(name: str) -> str:
    return (NR / "src" / name).read_text(encoding="utf-8")


class Flow:
    def __init__(self):
        self.nodes = []
        self._n = 0

    def id(self, tag: str) -> str:
        # stable ids, so a re-import replaces the same nodes
        self._n += 1
        return f"aqc{self._n:03d}{re.sub(r'[^a-z0-9]', '', tag.lower())[:9]}"

    def add(self, node: dict) -> dict:
        self.nodes.append(node)
        return node


def build_flow(tables) -> list:
    f = Flow()
    tab = f.id("tab")
    broker = f.id("broker")
    common = js("common.js")
    head = "// GENERATED by nodered/tools/generate.py from the AquaClean bridge. Edit the settings in the inject node.\n"

    def node(kind, name, x, y, g=None, **kw):
        n = {"id": f.id(name), "type": kind, "z": tab, "name": name, "x": x, "y": y, "wires": kw.pop("wires", [])}
        if g:
            n["g"] = g
        n.update(kw)
        return f.add(n)

    def width(n):
        if n["type"] in ("link in", "link out"):
            return 30
        return max(100, 7 * len(n.get("name", "")) + 60)

    def group(name, nodes, style, info=""):
        gid = f.id("grp" + name)
        for n in nodes:
            n["g"] = gid
        x0 = min(n["x"] - width(n) / 2 for n in nodes) - 15
        x1 = max(n["x"] + width(n) / 2 for n in nodes) + 15
        y0 = min(n["y"] for n in nodes) - 35
        y1 = max(n["y"] for n in nodes) + 30
        f.nodes.append({"id": gid, "type": "group", "z": tab, "name": name, "style": dict(style, label=True),
                        "nodes": [n["id"] for n in nodes], "x": round(x0), "y": round(y0),
                        "w": round(x1 - x0), "h": round(y1 - y0), "info": info})
        return gid

    blue = {"fill": "#e3f3f3", "stroke": "#5b9bd5"}
    grey = {"fill": "#f2f2f2", "stroke": "#999999"}
    green = {"fill": "#e6f4e6", "stroke": "#5ba35b"}
    yellow = {"fill": "#fff8e0", "stroke": "#d4a72c"}

    f.add({"id": tab, "type": "tab", "label": "AquaClean Bridge", "disabled": False,
           "info": "Geberit AquaClean Bridge over MQTT. See the comment nodes and the README on flows.nodered.org."})
    f.add({"id": broker, "type": "mqtt-broker", "name": "MQTT broker (edit me)", "broker": "localhost",
           "port": "1883", "clientid": "", "autoConnect": True, "usetls": False, "protocolVersion": "4",
           "keepalive": "60", "cleansession": True, "autoUnsubscribe": True, "birthTopic": "", "birthQos": "0",
           "birthPayload": "", "birthMsg": {}, "closeTopic": "", "closeQos": "0", "closePayload": "",
           "closeMsg": {}, "willTopic": "", "willQos": "0", "willPayload": "", "willMsg": {},
           "userProps": "", "sessionExpiry": ""})

    # --- settings + tables
    c_read = node("comment", "1. Set your broker and settings, then Deploy", 260, 60,
                  info="Double-click **MQTT broker (edit me)** (in any MQTT node) and enter your broker.\n\n"
                       "Then open **Settings** and adjust if needed:\n\n"
                       "- `topicPrefix`: the bridge's *MQTT topic prefix* (default `aquaclean`)\n"
                       "- `discoveryPrefix`: the discovery prefix (default `homeassistant`)\n"
                       "- `language`: `auto` (the bridge's language) or a code such as `en`, `nl`, `de`, `ja`, "
                       "`zh-TW`, `pt-BR`\n"
                       "- `onlyChanges`: send a state only when it changed\n"
                       "- `contextName`: name of the global context object with every state\n")
    tables_js = (head + "const tables = " + json.dumps(tables, ensure_ascii=False, separators=(",", ":"))
                 + ";\nglobal.set(\"aquaclean_tables\", tables);\n")
    n_tables = node("function", "Entity tables (generated, do not edit)", 260, 180, func="return null;",
                    outputs=0, timeout=0, noerr=0, initialize=tables_js, finalize="", libs=[])
    n_mqtt_in = node("mqtt in", "AquaClean bridge topics", 610, 120, topic="", qos="0", datatype="utf8",
                     broker=broker, nl=False, rap=True, rh=0, inputs=1)
    n_apply = node("function", "Apply settings", 390, 120, func=head + js("settings.js"), outputs=1, timeout=0,
                   noerr=0, initialize="", finalize="", libs=[], wires=[[n_mqtt_in["id"]]])
    n_settings = node("inject", "Settings", 190, 120, props=[{"p": "payload"}], repeat="", crontab="", once=True,
                      onceDelay="0.5", topic="", payloadType="json",
                      payload=json.dumps({"topicPrefix": "aquaclean", "discoveryPrefix": "homeassistant",
                                          "language": "auto", "onlyChanges": True,
                                          "contextName": "aquaclean"}, indent=2),
                      wires=[[n_apply["id"]]])

    # --- decode
    lo_state = node("link out", "AquaClean states", 1080, 80, mode="link", links=[])
    lo_btn = node("link out", "AquaClean remote buttons", 1080, 120, mode="link", links=[])
    lo_alarm = node("link out", "AquaClean connection & alarms", 1080, 160, mode="link", links=[])
    dbg_state = node("debug", "every state", 1090, 220, active=False, tosidebar=True, console=False,
                     tostatus=False, complete="true", targetType="full", statusVal="", statusType="auto")
    n_decode = node("function", "Decode", 900, 120, func=head + common + "\n" + js("decode.js"), outputs=3,
                    timeout=0, noerr=0, initialize="", finalize="", libs=[],
                    outputLabels=["every state", "remote control buttons", "connection and alarms"],
                    wires=[[lo_state["id"], dbg_state["id"]], [lo_btn["id"]], [lo_alarm["id"]]])
    n_mqtt_in["wires"] = [[n_decode["id"]]]

    # --- encode
    li_cmd = node("link in", "AquaClean command", 700, 360, links=[])
    n_mqtt_out = node("mqtt out", "to the bridge", 1060, 360, topic="", qos="", retain="", respTopic="",
                      contentType="", userProps="", correl="", expiry="", broker=broker)
    n_encode = node("function", "Encode", 880, 360, func=head + common + "\n" + js("encode.js"), outputs=1,
                    timeout=0, noerr=0, initialize="", finalize="", libs=[], wires=[[n_mqtt_out["id"]]])
    li_cmd["wires"] = [[n_encode["id"]]]
    c_cmd = node("comment", "Send commands here: msg.topic = key, msg.payload = value", 880, 310,
                 info="Wire a **link out** node to **AquaClean command** (or use a link call) from any tab.\n\n"
                      "- `msg.topic`: the entity key, e.g. `shower_toggle`, `orientation_light`, `light_color`\n"
                      "- `msg.payload`: buttons anything; switches and the light `true`, `false` or `\"toggle\"`; "
                      "numbers a number; selections the option number or its text in any language; "
                      "times `\"HH:MM\"`; texts a text\n\n"
                      "Errors are raised with `node.error`, so a **catch** node can handle them.")

    # --- errors
    n_catch_dbg = node("debug", "command errors", 390, 330, active=True, tosidebar=True, console=False,
                       tostatus=True, complete="error.message", targetType="msg", statusVal="error.message",
                       statusType="msg")
    n_catch = node("catch", "Encode errors", 190, 330, scope=[n_encode["id"]], uncaught=False,
                   wires=[[n_catch_dbg["id"]]])

    # --- examples: commands
    ex_cmd = []
    y = 480
    for label, topic, payload, ptype in (
            ("Shower: start or stop", "shower_toggle", "true", "bool"),
            ("Orientation light: toggle", "orientation_light", "toggle", "str"),
            ("Light color: White (option 6)", "light_color", "6", "num"),
            ("Light color: \"Blau\" (German text works too)", "light_color", "Blau", "str"),
            ("Default water temperature: 3", "default_water_temperature", "3", "num"),
            ("Stop shower and dryer", "stop_all", "true", "bool")):
        lo = node("link out", "", 470, y, mode="link", links=[li_cmd["id"]])
        inj = node("inject", label, 260, y, props=[{"p": "payload"}, {"p": "topic", "vt": "str"}], repeat="",
                   crontab="", once=False, onceDelay=0.1, topic=topic, payload=payload, payloadType=ptype,
                   wires=[[lo["id"]]])
        li_cmd["links"].append(lo["id"])
        ex_cmd += [inj, lo]
        y += 40

    # --- examples: states
    li_s = node("link in", "states", 650, 480, links=[lo_state["id"]])
    lo_state["links"].append(li_s["id"])
    d_seated = node("debug", "someone sat down", 1180, 460, active=True, tosidebar=True, console=False,
                    tostatus=True, complete="aquaclean.name", targetType="msg", statusVal="payload",
                    statusType="auto")
    d_desc = node("debug", "descaling due within 7 days", 1180, 520, active=True, tosidebar=True,
                  console=False, tostatus=True, complete="payload", targetType="msg", statusVal="payload",
                  statusType="auto")
    sw_desc = node("switch", "≤ 7 days?", 970, 520, property="payload", propertyType="msg",
                   rules=[{"t": "lte", "v": "7", "vt": "num"}], checkall="true", repair=False, outputs=1,
                   wires=[[d_desc["id"]]])
    sw_seated = node("switch", "seated = true?", 970, 460, property="payload", propertyType="msg",
                     rules=[{"t": "true"}], checkall="true", repair=False, outputs=1, wires=[[d_seated["id"]]])
    sw_topic = node("switch", "by msg.topic", 800, 480, property="topic", propertyType="msg",
                    rules=[{"t": "eq", "v": "seated", "vt": "str"},
                           {"t": "eq", "v": "days_until_descaling", "vt": "str"}],
                    checkall="false", repair=False, outputs=2, wires=[[sw_seated["id"]], [sw_desc["id"]]])
    li_s["wires"] = [[sw_topic["id"]]]

    li_b = node("link in", "remote buttons", 650, 600, links=[lo_btn["id"]])
    lo_btn["links"].append(li_b["id"])
    d_btn = node("debug", "your action here", 1180, 600, active=True, tosidebar=True, console=False,
                 tostatus=True, complete="aquaclean.label", targetType="msg", statusVal="aquaclean.label",
                 statusType="msg")
    sw_btn = node("switch", "arrow up, long press?", 900, 600, property="payload", propertyType="msg",
                  rules=[{"t": "eq", "v": "arrow_up_long", "vt": "str"}], checkall="true", repair=False,
                  outputs=1, wires=[[d_btn["id"]]])
    li_b["wires"] = [[sw_btn["id"]]]

    li_a = node("link in", "connection & alarms", 650, 660, links=[lo_alarm["id"]])
    lo_alarm["links"].append(li_a["id"])
    d_alarm = node("debug", "connection & alarms", 900, 660, active=True, tosidebar=True, console=False,
                   tostatus=True, complete="true", targetType="full", statusVal="payload", statusType="auto")
    li_a["wires"] = [[d_alarm["id"]]]

    fn_ctx = node("function", "read any state from global context", 900, 720,
                  func="// Every state is in global context, from any tab:\n"
                       "const aq = global.get(\"aquaclean\");\n"
                       "msg.payload = {\n"
                       "    online: aq.online,\n"
                       "    seated: aq.state.seated,\n"
                       "    waterTemperature: aq.state.water_temperature,\n"
                       "    lightColor: aq.label.light_color   // the option text in your language\n"
                       "};\nreturn msg;",
                  outputs=1, timeout=0, noerr=0, initialize="", finalize="", libs=[], wires=[])
    inj_ctx = node("inject", "read now", 650, 720, props=[{"p": "payload"}], repeat="", crontab="", once=False,
                   onceDelay=0.1, topic="", payload="", payloadType="date", wires=[[fn_ctx["id"]]])
    d_ctx = node("debug", "global context", 1180, 720, active=True, tosidebar=True, console=False,
                 tostatus=False, complete="payload", targetType="msg", statusVal="", statusType="auto")
    fn_ctx["wires"] = [[d_ctx["id"]]]

    group("Settings and tables", [c_read, n_settings, n_apply, n_tables, n_mqtt_in], grey)
    group("From the bridge", [n_decode, lo_state, lo_btn, lo_alarm, dbg_state], blue,
          "Output 1: every state. Output 2: remote control buttons. Output 3: online, connected, alarms.")
    group("To the bridge", [c_cmd, li_cmd, n_encode, n_mqtt_out], green)
    group("Errors", [n_catch, n_catch_dbg], grey)
    group("Examples: commands", ex_cmd, yellow)
    group("Examples: states", [li_s, sw_topic, sw_seated, sw_desc, d_seated, d_desc, li_b, sw_btn, d_btn, li_a,
                               d_alarm, inj_ctx, fn_ctx, d_ctx], yellow)
    return f.nodes


# ---------------------------------------------------------------------------------------------- docs

GROUP_TITLES = {
    "bridge": "### `bridge` — the bridge (the ESP)",
    "toilet": "### `toilet` — AquaClean Toilet",
    "session": "### `session` — Active session",
    "profile": "### `profile` — User profiles 1–6",
    "maintenance": "### `maintenance` — Physical maintenance (only while unlocked)",
}
COLS = "| `msg.topic` | Name | `msg.payload` | Direction | Values | What it does |\n|---|---|---|---|---|---|"


def payload_type(r) -> str:
    p = r["p"]
    if p in ("binary_sensor", "switch", "light"):
        return "boolean"
    if p in ("sensor", "number") or r.get("o") or r.get("ts"):
        return "number"
    if p == "button":
        return "–"
    return "string"


def direction(r) -> str:
    if r["p"] == "button":
        return "command"
    if r["p"] == "event":
        return "event"
    return "state + command" if r.get("w") else "state"


def values(r, en) -> str:
    p = r["p"]
    if r.get("o"):
        opts = en["options"][r["o"][0]]
        return ", ".join(f"{i} = {o}" for i, o in enumerate(opts) if o)
    if p == "event":
        return "button id, e.g. `shower`, `shower_long`"
    if p in ("binary_sensor", "switch"):
        return "true / false"
    if p == "light":
        return "true / false (command also `\"toggle\"`)"
    if p == "button":
        return "any payload"
    if p == "time":
        return "`\"HH:MM\"`"
    if r.get("ts"):
        return "time in ms (JavaScript `Date`)"
    if p in ("sensor", "number"):
        s = f"{r['min']}–{r['max']}" if "min" in r else ""
        return f"{s} {r.get('u', '')}".strip() or "number"
    return "text"


def write_docs(g, std, recs, rep) -> None:
    en = g.I18N["en"]
    path = NR / "README.md"
    text = path.read_text(encoding="utf-8")
    out = []
    groups = {k: [] for k in GROUP_TITLES}
    seen = set()
    for e in std:
        owner = next(k for k, r in rep.items() if r is e) if any(r is e for r in rep.values()) else None
        if owner is None:
            continue
        if owner in seen:
            continue
        seen.add(owner)
        groups[recs[owner]["g"]].append((owner, e))
    for gk, rows in groups.items():
        out += [GROUP_TITLES[gk], ""]
        if gk == "profile":
            out += ["Profiles 1–3 are stored in the toilet (profile keys 1–3 on the remote control), profiles 4–6 "
                    "only on the bridge. `N` in `profile_N_…` is the profile number 1–6: each profile has its own "
                    "keys (e.g. `profile_2_spray_intensity`), which you can read and write.", ""]
        if gk == "session":
            out += ["`N` in `profile_N_activate` / `profile_N_store` is 1–6.", ""]
        out.append(COLS)
        for owner, e in rows:
            r = recs[owner]
            name = g._entity_name(en, e, True)
            shown = re.sub(r"profile_\d", "profile_N", owner)
            if e.name_args and "n" in e.name_args:
                name = name.replace(str(e.name_args["n"]), "N")
            out.append(f"| `{shown}` | {name} | {payload_type(r)} | {direction(r)} | {values(r, en)} | "
                       f"{D[base_key(e)][0]} |")
        if gk == "bridge":
            out += ["", "Setup entities of the web-installer firmware (normally set on the bridge's own web page):",
                    "", COLS]
            for key, comp, _sub, _fixed in SETUP:
                r = recs[key]
                name, desc = SETUP_DOC[key]
                out.append(f"| `{key}` | {name} | {payload_type(r)} | {direction(r)} | {values(r, en)} | {desc} |")
        out.append("")
    # fold the per-profile session buttons and statistics into one line each
    folded, seen_rows = [], set()
    for line in out:
        m = re.match(r"\| `(profile_N_\w+|stat_profile_N)`", line)
        if m and m.group(1) in seen_rows:
            continue
        if m:
            seen_rows.add(m.group(1))
        folded.append(line)
    adv = sorted({g._entity_name(en, e, True) for e in g.ENTITIES if e.discovery or e.disabled})
    folded.append("#### Advanced entities\n\nDiscovery and diagnostic tools for other models, disabled by default on "
                  "the bridge. When you enable one on the bridge, it arrives with its own key too: "
                  + ", ".join(adv) + ".")
    block = "\n".join(folded)
    new, n = re.subn(r"(<!-- entities:begin -->).*?(<!-- entities:end -->)",
                     lambda m: f"{m.group(1)}\n{block}\n{m.group(2)}", text, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: entity markers not found")
    path.write_text(new, encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-docs", action="store_true")
    args = ap.parse_args()
    g = load_component()
    std, recs, rep, tables, ui_langs = build(g)
    nodes = build_flow(tables)
    (NR / "flow.json").write_text(json.dumps(nodes, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    if not args.no_docs:
        write_docs(g, std, recs, rep)
    profiles = sum(1 for r in recs.values() if r.get("g") == "profile")
    print(f"{len(recs)} keys ({profiles} user-profile), {len(tables['topics'])} topics in "
          f"{len(g.LANGUAGES)} bridge languages, {len(ui_langs)} flow languages")


if __name__ == "__main__":
    main()
