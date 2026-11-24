#!/usr/bin/env python3
"""Generate the Homey flow cards and the entity table from the bridge component.

Run with a Python that has ESPHome installed (the component imports it):

    python3 tools/generate.py            # from homey/ in the bridge repository

Reads components/geberit_aquaclean/__init__.py (ENTITIES) and lang/*.yaml, writes:

  drivers/aquaclean/driver.flow.compose.json   all flow cards, 13 Homey languages
  lib/entities.json                             what the app needs at runtime to find each entity

Only names, kinds and value ranges are exported: nothing about the toilet protocol.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import OrderedDict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from templates import BRIDGE_LANG, HOMEY_LANGS, T  # noqa: E402

# internal domain of the component -> ESPHome platform
PLATFORM = {
    "status": "binary_sensor", "binary_sensor": "binary_sensor", "user_flag": "binary_sensor",
    "stat_counter": "sensor", "stat_hours": "sensor", "stat_seconds": "sensor", "counter": "sensor",
    "days": "sensor", "timestamp": "sensor", "gauge": "sensor", "rssi": "sensor", "user_rssi": "sensor",
    "status_value": "sensor", "counter_value": "sensor",
    "text_sensor": "text_sensor", "uptime": "text_sensor", "user_irk": "text_sensor", "status_enum": "text_sensor",
    "user_text": "text", "user_entity": "text", "user_irk_set": "text", "raw_text": "text",
    "setting_select": "select", "user_welcome": "select", "user_device": "select",
    "number": "number", "threshold": "number",
    "setting_switch": "switch", "hub_switch": "switch",
    "button": "button", "command": "button",
    "setting_time": "time",
    "event": "event",
    "light": "light",
}
# write-only: the value is never shown again, so no trigger on it
SECRET = {"user_irk_set"}
# Homey device capabilities fed from an entity (shown on the device tile)
CAPABILITIES = {"toilet_problem": "alarm_problem"}

FAMILY_RE = [(re.compile(r"^profile_(\d)_(.+)$"), lambda m: f"profile_{m.group(2)}"),
             (re.compile(r"^stat_profile_(\d)$"), lambda m: "stat_profile")]


def load_component(bridge: Path):
    sys.path.insert(0, str(bridge / "components"))
    import geberit_aquaclean as g  # noqa: PLC0415
    return g


def number_range(e):
    if e.domain == "threshold":
        return -100, -30
    _kind, _sid, maximum, offset = e.setting[:4]
    return offset, maximum + offset


def ellipsis(text: str) -> str:
    return re.sub(r"\[\[\w+\]\]", "…", text)


def write_ui(root: Path) -> None:
    """App manifest, driver manifest and locales in every Homey language."""
    import ui_strings as U  # noqa: PLC0415

    def tr(key):
        return {c: U.S[key][c] for c in HOMEY_LANGS}

    for c in HOMEY_LANGS:
        loc = {}
        for key, texts in U.S.items():
            section, name = key.split(".", 1)
            if section in ("pair", "device"):
                loc.setdefault(section, {})[name] = texts[c]
        (root / "locales").mkdir(exist_ok=True)
        (root / "locales" / f"{c}.json").write_text(json.dumps(loc, ensure_ascii=False, indent=2) + "\n",
                                                    encoding="utf-8")
    driver = {
        "name": {c: U.DRIVER_NAME[c] for c in HOMEY_LANGS},
        "class": "other",
        "capabilities": sorted(set(CAPABILITIES.values())),
        "platforms": ["local"],
        "connectivity": ["lan"],
        "discovery": "esphome",
        "images": {"small": "{{driverAssetsPath}}/images/small.png",
                   "large": "{{driverAssetsPath}}/images/large.png",
                   "xlarge": "{{driverAssetsPath}}/images/xlarge.png"},
        "pair": [{"id": "connect"}],
        "settings": [
            {"type": "group", "label": tr("settings.connection"), "children": [
                {"id": "host", "type": "text", "label": tr("settings.host"), "value": ""},
                {"id": "port", "type": "number", "label": tr("settings.port"), "value": 6053, "min": 1,
                 "max": 65535},
                {"id": "encryption_key", "type": "password", "label": tr("settings.key"), "value": ""},
            ]},
            {"type": "group", "label": tr("settings.info"), "children": [
                {"id": "firmware", "type": "label", "label": tr("settings.firmware"), "value": ""},
                {"id": "entities", "type": "label", "label": tr("settings.entities"), "value": ""},
            ]},
        ],
    }
    (root / "drivers/aquaclean/driver.compose.json").write_text(
        json.dumps(driver, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    app_path = root / ".homeycompose/app.json"
    app = json.loads(app_path.read_text(encoding="utf-8")) if app_path.exists() else {}
    app.update({
        "name": {c: U.APP_NAME for c in HOMEY_LANGS},
        "description": {c: U.APP_DESC[c] for c in HOMEY_LANGS},
    })
    app_path.write_text(json.dumps(app, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bridge", type=Path, default=ROOT.parent,
                    help="checkout of geberit-aquaclean-bridge-for-esp (default: the repository this app lives in)")
    args = ap.parse_args()
    g = load_component(args.bridge.resolve())
    lang = g.I18N
    for code in HOMEY_LANGS:
        assert BRIDGE_LANG.get(code, code) in lang, f"bridge has no language {code}"

    # "standard" entities: everything the bridge creates enabled by default (no discovery / disabled ones)
    ents = [e for e in g.ENTITIES if not e.discovery and not e.disabled]

    # ------------------------------------------------------------------ match table (all 29 bridge languages)
    match: dict[str, str] = {}

    def add_match(dev: str, name: str, key: str) -> None:
        k = f"{dev}\u0001{name}"
        if match.get(k, key) != key:
            raise SystemExit(f"name collision: {dev!r} / {name!r} -> {match[k]} and {key}")
        match[k] = key

    for code in g.LANGUAGES:
        tr = lang[code]
        for e in ents:
            dev = "" if e.group is None else tr["devices"][e.group]
            add_match(dev, g._entity_name(tr, e, True), e.key)  # sub-devices (the default)
            add_match("", g._entity_name(tr, e, False), e.key)  # sub_devices: false

    # ------------------------------------------------------------------ items: single entities and profile families
    items: "OrderedDict[str, dict]" = OrderedDict()
    for e in ents:
        family, profile = None, None
        for rx, fam in FAMILY_RE:
            m = rx.match(e.key)
            if m:
                family, profile = fam(m), int(m.group(1))
        base = family or e.key
        it = items.get(base)
        if it is None:
            it = items[base] = {"id": base, "e": e, "platform": PLATFORM[e.domain], "profiles": {}, "key": None}
        if family:
            it["profiles"][profile] = e.key
        else:
            it["key"] = e.key

    def name_of(e, code: str) -> str:
        return lang[BRIDGE_LANG.get(code, code)]["names"][e.name_key or e.key]

    def subject(it, code: str):
        raw = name_of(it["e"], code)
        if not it["profiles"]:
            return raw.format(**(it["e"].name_args or {})), raw.format(**(it["e"].name_args or {}))
        if "{n}" in raw:
            return raw.replace("{n}", "…"), raw.replace("{n}", "[[profile]]")
        return f"{raw} ({T[code]['profile']})", f"{raw} ([[profile]])"

    def profile_arg(it) -> dict:
        has_n = "{n}" in name_of(it["e"], "en")
        values = []
        for p in sorted(it["profiles"]):
            if has_n:
                label = {c: str(p) for c in HOMEY_LANGS}
            else:
                label = {c: lang[BRIDGE_LANG.get(c, c)]["devices"][f"profile_{p}"] for c in HOMEY_LANGS}
            values.append({"id": str(p), "label": label})
        return {"type": "dropdown", "name": "profile", "title": {c: T[c]["arg_profile"] for c in HOMEY_LANGS},
                "values": values}

    def card(it, kind: str, template: str, extra_args=(), tokens=None, title_override=None) -> dict:
        title, fmt = {}, {}
        for c in HOMEY_LANGS:
            plain_s, fmt_s = subject(it, c)
            if title_override:
                title[c], fmt[c] = plain_s, fmt_s
            else:
                title[c] = ellipsis(T[c][template].format(s=plain_s))
                fmt[c] = T[c][template].format(s=fmt_s)
        out = {"id": f"{it['id']}_{kind}", "title": title}
        it.setdefault("kinds", []).append(kind)
        a = ([profile_arg(it)] if it["profiles"] else []) + list(extra_args)
        if a:
            out["titleFormatted"] = fmt
            out["args"] = a
        if tokens:
            out["tokens"] = tokens
        return out

    def tok(typ: str, example) -> list:
        return [{"name": "value", "type": typ, "title": {c: T[c]["tok_value"] for c in HOMEY_LANGS},
                 "example": example}]

    def arg(typ: str, **kw) -> dict:
        return {"type": typ, "name": "value", "title": {c: T[c]["arg_value"] for c in HOMEY_LANGS}, **kw}

    triggers, conditions, actions, runtime = [], [], [], []
    for it in items.values():
        e, p = it["e"], it["platform"]
        is_ts = e.domain == "timestamp" or e.defaults.get("device_class") == "timestamp"
        rt = {"id": it["id"], "platform": p}
        if it["profiles"]:
            rt["profiles"] = {str(k): v for k, v in sorted(it["profiles"].items())}
        else:
            rt["key"] = it["key"]
        if p in ("binary_sensor", "switch", "light"):
            triggers += [card(it, "on", "turned_on"), card(it, "off", "turned_off")]
            conditions.append(card(it, "is_on", "is_on"))
            if p != "binary_sensor":
                actions += [card(it, "turn_on", "turn_on"), card(it, "turn_off", "turn_off"),
                            card(it, "toggle", "toggle")]
        elif p == "sensor":
            if is_ts:
                rt["timestamp"] = True
                triggers.append(card(it, "changed", "changed", tokens=tok("string", "2026-11-24")))
            else:
                triggers.append(card(it, "changed", "changed", tokens=tok("number", 1)))
                conditions.append(card(it, "above", "above", [arg("number", placeholder={"en": "0"})]))
        elif p == "number":
            lo, hi = number_range(e)
            rt.update(min=lo, max=hi)
            triggers.append(card(it, "changed", "changed", tokens=tok("number", lo)))
            conditions.append(card(it, "above", "above", [arg("number", min=lo, max=hi, step=1)]))
            actions.append(card(it, "set", "set_to", [arg("number", min=lo, max=hi, step=1)]))
        elif p == "select":
            triggers.append(card(it, "changed", "changed", tokens=tok("string", "…")))
            conditions.append(card(it, "equals", "equals", [arg("autocomplete")]))
            actions.append(card(it, "set", "set_to", [arg("autocomplete")]))
        elif p == "text_sensor":
            triggers.append(card(it, "changed", "changed", tokens=tok("string", "…")))
            conditions.append(card(it, "equals", "equals", [arg("text")]))
        elif p == "text":
            if e.domain in SECRET:
                rt["secret"] = True
            else:
                triggers.append(card(it, "changed", "changed", tokens=tok("string", "…")))
            actions.append(card(it, "set", "set_to", [arg("text")]))
        elif p == "time":
            triggers.append(card(it, "changed", "changed", tokens=tok("string", "22:00")))
            actions.append(card(it, "set", "set_to", [arg("time")]))
        elif p == "button":
            actions.append(card(it, "press", "", title_override=True))
        elif p == "event":
            en = lang["en"]
            values = []
            for et in g.EVENT_TYPES:
                if et == "unknown":
                    continue
                btn, long = (et[:-5], True) if et.endswith("_long") else (et, False)
                label = {}
                for c in HOMEY_LANGS:
                    tr = lang[BRIDGE_LANG.get(c, c)]
                    label[c] = tr["buttons"][btn] + (f" ({tr['button_states'][1]})" if long else "")
                values.append({"id": et, "label": label})
            rt["labels"] = {et["id"]: et["label"] for et in values}
            it["kinds"] = ["any", "one"]
            triggers.append({
                "id": f"{it['id']}_any",
                "title": {c: T[c]["remote_any"] for c in HOMEY_LANGS},
                "tokens": [
                    {"name": "button", "type": "string", "title": {c: T[c]["tok_button"] for c in HOMEY_LANGS},
                     "example": en["buttons"]["shower"]},
                    {"name": "long", "type": "boolean", "title": {c: T[c]["tok_long"] for c in HOMEY_LANGS},
                     "example": False},
                ],
            })
            triggers.append({
                "id": f"{it['id']}_one",
                "title": {c: T[c]["remote_one_plain"] for c in HOMEY_LANGS},
                "titleFormatted": {c: T[c]["remote_one"] for c in HOMEY_LANGS},
                "args": [{"type": "dropdown", "name": "button",
                          "title": {c: T[c]["arg_button"] for c in HOMEY_LANGS}, "values": values}],
            })
        else:
            raise SystemExit(f"no cards for platform {p} ({e.key})")
        rt["cards"] = it.get("kinds", [])
        runtime.append(rt)

    ids = [c["id"] for c in triggers + conditions + actions]
    assert len(ids) == len(set(ids)), "duplicate card id"

    (ROOT / "drivers/aquaclean").mkdir(parents=True, exist_ok=True)
    (ROOT / "lib").mkdir(exist_ok=True)
    flow = {"triggers": triggers, "conditions": conditions, "actions": actions}
    (ROOT / "drivers/aquaclean/driver.flow.compose.json").write_text(
        json.dumps(flow, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (ROOT / "lib/entities.json").write_text(json.dumps({
        "bridgeLanguages": g.LANGUAGES,
        "capabilities": CAPABILITIES,
        "items": runtime,
        "match": match,
    }, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    write_ui(ROOT)
    print(f"{len(ents)} entities -> {len(items)} items: {len(triggers)} triggers, {len(conditions)} conditions, "
          f"{len(actions)} actions ({len(ids)} cards), {len(match)} name matches")


if __name__ == "__main__":
    main()
