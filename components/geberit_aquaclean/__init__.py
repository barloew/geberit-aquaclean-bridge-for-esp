"""Geberit AquaClean Bridge for ESP (ESPHome component, Sela and related models).

The ESP keeps one permanent BLE connection to the toilet and plays the role each client expects:

* towards the toilet it is paired as the remote control and speaks the app protocol on that same link
  (status and commands for Home Assistant);
* the real remote control is paired with the ESP, screens and buttons are passed through unchanged;
* the Geberit Home app connects to the ESP, which advertises as the toilet and relays app frames.

All entities are created by this component. Their names come from ``lang/<language>.yaml`` (Geberit's
own terms per language); option texts of the select entities can be switched at runtime with the
language select. Any entity can be customized by adding its key with entity options, e.g.::

    geberit_aquaclean:
      water_hardness:
        name: Hardness
        disabled_by_default: true
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import json
import logging
import re
import zlib
from typing import Any

import yaml

import esphome.codegen as cg
from esphome.components import (
    binary_sensor,
    ble_client,
    button,
    datetime,
    esp32_ble,
    event,
    light,
    number,
    select,
    sensor,
    switch,
    text,
    text_sensor,
    time,
)
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option, include_builtin_idf_component
from esphome.core import CORE, ID
from esphome.core import entity_helpers
from esphome.coroutine import CoroPriority, coroutine_with_priority
from esphome.helpers import fnv1_hash_object_id
import esphome.final_validate as fv
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_DISABLED_BY_DEFAULT,
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
    CONF_OUTPUT_ID,
    CONF_TIME_ID,
)

CODEOWNERS = []
DEPENDENCIES = ["ble_client", "esp32_ble_tracker"]
AUTO_LOAD = [
    "binary_sensor",
    "button",
    "datetime",
    "event",
    "light",
    "number",
    "select",
    "sensor",
    "switch",
    "text",
    "text_sensor",
]
MULTI_CONF = False

CONF_LANGUAGE = "language"
CONF_LINK_ID = "link_id"
CONF_WEB_SERVER_GROUPS = "web_server_groups"
CONF_STATESTREAM_TOPIC = "statestream_topic"
CONF_LOG_OBFUSCATE = "log_obfuscate"
CONF_MAINT_ACCEPTED = "maintenance_disclaimer_accepted"
CONF_CUSTOM_ICONS = "custom_icons"
CONF_HOMEKIT = "homekit"
CONF_ENABLED = "enabled"
CONF_PORT = "port"
CONF_SETUP_CODE = "setup_code"
CONF_RESET_PAIRINGS = "reset_pairings"
CONF_RUNTIME_TOGGLE = "runtime_toggle"
# esp-homekit-sdk, ESPHome variant (mdns left to ESPHome), vendored in homekit_sdk/ (see its README)
HOMEKIT_SDK_DIR = Path(__file__).resolve().parent.parent.parent / "homekit_sdk"
HOMEKIT_SDK_COMPONENTS = ["esp_hap_core", "esp_hap_apple_profiles", "esp_hap_platform", "hkdf-sha", "mu_srp"]


def _homekit_setup_code(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"\d{3}-\d{2}-\d{3}", value):
        raise cv.Invalid("HomeKit setup code must look like 123-45-678")
    digits = value.replace("-", "")
    if len(set(digits)) == 1 or digits in ("12345678", "87654321"):
        raise cv.Invalid("HomeKit does not accept this setup code (all digits equal or 12345678 / 87654321)")
    return value


def _homekit_sockets(config):
    from esphome.components import socket
    socket.consume_sockets(4, "geberit_aquaclean.homekit")(config)
    socket.consume_sockets(1, "geberit_aquaclean.homekit", socket.SocketType.TCP_LISTEN)(config)
    socket.consume_sockets(1, "geberit_aquaclean.homekit", socket.SocketType.UDP)(config)
    return config


HOMEKIT_SCHEMA = cv.All(
    cv.Schema(
        {
            # only "enabled: true" builds HomeKit in; with false (default) the block is ignored
            cv.Optional(CONF_ENABLED, default=False): cv.boolean,
            # default: derived from the MAC address, shown in the log at every start
            cv.Optional(CONF_SETUP_CODE): _homekit_setup_code,
            cv.Optional(CONF_PORT, default=32042): cv.port,
            # true: forget all Apple Home pairings at the next start (set back to false afterwards)
            cv.Optional(CONF_RESET_PAIRINGS, default=False): cv.boolean,
            # true: HomeKit is compiled in but starts only when the "Apple Home" switch is on (the switch
            # appears, default off). false (default): HomeKit always starts, as before, with no switch.
            cv.Optional(CONF_RUNTIME_TOGGLE, default=False): cv.boolean,
        }
    ),
    _homekit_sockets,
)
CONF_MAINT_CODE = "maintenance_confirmation_code"  # no longer used (v1.26.0 only)
CONF_SUB_DEVICES = "sub_devices"
CONF_REMOTE_MAC_ADDRESS = "remote_mac_address"
CONF_POLL_INTERVAL = "poll_interval"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_SESSION_REFRESH = "session_refresh"
CONF_SETTINGS_REFRESH = "settings_refresh"
CONF_SET_CLOCK = "set_clock"
CONF_IDLE_TIMEOUT = "remote_idle_timeout"
CONF_PAIRING_DURATION = "remote_pairing_duration"

_LOGGER = logging.getLogger(__name__)
ns = cg.esphome_ns.namespace("geberit_aquaclean")
AquaCleanHub = ns.class_("AquaCleanHub", cg.Component, ble_client.BLEClientNode)
RemoteLink = ns.class_("RemoteLink", cg.Component)
SettingNumber = ns.class_("SettingNumber", number.Number)
SettingSelect = ns.class_("SettingSelect", select.Select)
SettingSwitch = ns.class_("SettingSwitch", switch.Switch)
SettingTime = ns.class_("SettingTime", datetime.TimeEntity)
HubButton = ns.class_("HubButton", button.Button)
HubSwitch = ns.class_("HubSwitch", switch.Switch)
RemoteEvent = ns.class_("RemoteEvent", event.Event)
OrientationLight = ns.class_("OrientationLight", light.LightOutput)
UserText = ns.class_("UserText", text.Text)
RawRequestText = ns.class_("RawRequestText", text.Text)
UserSelect = ns.class_("UserSelect", select.Select)
ThresholdNumber = ns.class_("ThresholdNumber", number.Number)
Device = cg.esphome_ns.class_("Device")

# ---------------------------------------------------------------------------------------------------
# Languages
# ---------------------------------------------------------------------------------------------------

# Order = index stored in the ESP (language select): only append new languages at the end
LANGUAGES = ["en", "nl", "de", "fr", "it", "es", "pl", "cs", "hu", "da", "sv", "nb", "fi", "pt", "ar", "tr",
             "et", "hr", "lv", "lt", "ro", "ru", "sk", "sl", "sr", "uk", "zh", "ko", "he"]
# Language codes also accepted (YAML reads an unquoted `no` as false)
LANGUAGE_ALIASES = {"no": "nb", "false": "nb", "kr": "ko", "iw": "he"}
# Scripts without Latin letters: ESPHome derives the entity key (object_id) from the name and turns every
# character outside a-z 0-9 into "_", so these names would all collide. Their entities get the key of the
# English name and show the translated name.
ASCII_ID_LANGUAGES = {"ar", "he", "ru", "uk", "zh", "ko"}
_LANG_DIR = Path(__file__).parent / "lang"


def _load_languages() -> dict[str, dict[str, Any]]:
    out = {}
    for code in LANGUAGES:
        with open(_LANG_DIR / f"{code}.yaml", encoding="utf-8") as f:
            out[code] = yaml.safe_load(f)
    ref = out["en"]
    for code, data in out.items():
        for section in ("devices", "names", "options", "buttons", "features", "variants", "texts"):
            missing = set(ref[section]) - set(data.get(section, {}))
            if missing:
                raise cv.Invalid(f"lang/{code}.yaml: missing {section} keys {sorted(missing)}")
        for key, opts in ref["options"].items():
            if len(data["options"][key]) != len(opts):
                raise cv.Invalid(f"lang/{code}.yaml: options.{key} must have {len(opts)} entries")
        if len(data["button_states"]) != 3:
            raise cv.Invalid(f"lang/{code}.yaml: button_states must have 3 entries")
    return out


I18N = _load_languages()

# ---------------------------------------------------------------------------------------------------
# Toilet settings: kind = which request pair the toilet expects
#   session:  read 07 <id>,     write 08 <id> <LE16>      (running session, only while seated)
#   default:  read 53 00 <id>,  write 54 00 <id> <LE16>   (start values of every session)
#   common:   read 51 <id>,     write 52 <id> <LE16> + 0b (orientation light, water hardness)
#   schedule: read 57 <id>,     write 58 <id> <LE32>      (orientation light time windows, minutes)
# ---------------------------------------------------------------------------------------------------

KIND_SESSION = 0
KIND_DEFAULT = 1
KIND_COMMON = 2
KIND_SCHEDULE = 3
PROBE_COUNTER = 4  # discovery probe: id in the counter list (59)
PROBE_MODULE = 6  # discovery probe: bit of status 64 (connected modules; bit 0 = flush trigger)
PROBE_SLOT = 5  # discovery probe: Home Assistant profile <id> has been created
KIND_LOCAL = 6  # setting kept on the ESP (Home Assistant profiles), not on the toilet
# Bridge-only setting ids for a Home Assistant profile's Lady shower (the toilet keeps a single shared slot for
# oscillation and water temperature; the Lady variant lives only on the bridge). Must match hub.h.
LOCAL_LADY_OSCILLATION = 0x0E
LOCAL_LADY_WATER_TEMPERATURE = 0x0F

# Remote control button codes, in the order of the C++ table (remote_link.cpp) and lang "buttons"
BUTTON_KEYS = [
    "shower",
    "spray_minus",
    "spray_plus",
    "arm_back",
    "arm_forward",
    "lady_shower",
    "profile_1",
    "profile_2",
    "profile_3",
    "arrow_right",
    "arrow_left",
    "arrow_down",
    "arrow_up",
    "oscillation",
    "orientation_light",
    "ok",
    "temperature",
    "home",
    "maintenance",
]
EVENT_TYPES = BUTTON_KEYS + [f"{b}_long" for b in BUTTON_KEYS] + ["unknown"]

# HubButton / HubSwitch actions (enum in entities.h)
(BTN_SHOWER, BTN_LADY_SHOWER, BTN_PAIR_AS_REMOTE, BTN_REFRESH, BTN_UNPAIR_REMOTE, BTN_SCAN, BTN_READ_INFO,
 BTN_REDISCOVER, BTN_SEARCH_TOILET, BTN_ADD_PROFILE, BTN_SEARCH_DEVICES, BTN_FILTER_RESET, BTN_COMMAND, BTN_READ_STATS,
 BTN_HOMEKIT_QR) = range(15)
BTN_DELETE_PROFILE = 48  # + profile number
BTN_IRK_CAPTURE = 64  # + profile number
BTN_IRK_SHOW = 80  # + profile number
BTN_ACTIVATE_PROFILE = 16  # + profile number (1-4)
BTN_STORE_PROFILE = 32  # + profile number
(SW_FORWARD, SW_PAIRING, SW_ADVERTISE, SW_DISCOVERY, SW_IRK_ANDROID, SW_MAINTENANCE,
 SW_HOMEKIT) = range(7)

G_SESSION = "session"
G_DEFAULTS = "defaults"
G_DEVICE = "device"
G_REMOTE = "remote"
G_MAINT = "maintenance"  # physical maintenance: hidden unless switched on for one boot (see hub.cpp)
G_MAIN = None
PROFILES = [1, 2, 3]  # user profiles in the toilet (profile keys 1-3 of the remote control; 53 <profile> <id>)
HA_PROFILES = [4, 5, 6]  # user profiles kept on the ESP, created with a button in Home Assistant
USERS = PROFILES + HA_PROFILES
G_PROFILES = [f"profile_{p}" for p in USERS]
DEVICE_IDS = {
    "toilet": "aquaclean_toilet",
    G_SESSION: "aquaclean_session",
    G_MAINT: "aquaclean_maintenance",
    **{g: f"aquaclean_{g}" for g in G_PROFILES},
}

DIAG = "diagnostic"
CONFIG = "config"


@dataclass
class Ent:
    key: str
    domain: str
    group: str | None
    defaults: dict = field(default_factory=dict)
    # setting entities: (kind, toilet id, max raw value, display offset[, user profile])
    setting: tuple | None = None
    action: int | None = None
    remote: bool = False  # only with a remote control configured
    homekit: bool = False  # only with homekit: configured
    discovery: bool = False  # diagnostic entity for other models, disabled by default
    # capabilities (see CAPS) the toilet must have for this entity; filled from CAPS_OF below
    caps: tuple = ()
    name_key: str | None = None  # key in lang "names" when different from key
    name_args: dict | None = None  # placeholders in the name, e.g. {"n": 1}
    disabled: bool = False  # disabled by default in Home Assistant
    probe: tuple | None = None  # (kind, id, profile) read by the feature discovery, for entities without a setting
    stat: tuple | None = None  # statistics sensor: (kind, counter / timer index), see hub.h StatKind
    ha_profile: bool = True  # default_* entity: also in the Home Assistant profiles 5-6 (needs a feature to hide it)


def _num(key, group, kind, sid, maximum, offset, icon, cat=None):
    d = {CONF_ICON: icon}
    if cat:
        d[CONF_ENTITY_CATEGORY] = cat
    return Ent(key, "number", group, d, setting=(kind, sid, maximum, offset))


def _cmd(key, group, cmd, icon, cat=None, discovery=False):
    """Button that sends command <cmd> (proc 09)."""
    d = {CONF_ICON: icon}
    if cat:
        d[CONF_ENTITY_CATEGORY] = cat
    return Ent(key, "command", group, d, action=cmd, discovery=discovery)


# ---------------------------------------------------------------------------------------------------
# Models and features
#
# Feature bits, in the order of the C++ enum and lang "features".
# ---------------------------------------------------------------------------------------------------

FEATURES = [
    "orientation_light",
    "lady_shower",
    "oscillation",
    "water_temperature",
    "spray_arm",
    "descaling",
    "user_detection",
    "seat_heating",
    "dryer",
    "odor_extraction",
    "automatic_lid",
    "light_sensor",  # orientation light "in the dark" mode (light sensor sensitivity)
]
VARIANTS = ["seat", "complete_system", "floor_standing"]


def _features(*names: str) -> int:
    return sum(1 << FEATURES.index(n) for n in names)


_SELA = ("orientation_light", "lady_shower", "oscillation", "water_temperature", "spray_arm", "descaling",
         "user_detection", "light_sensor")
_MERA_CLASSIC = ("lady_shower", "oscillation", "water_temperature", "spray_arm", "descaling", "user_detection",
                 "dryer", "odor_extraction")
_MERA_COMFORT = _MERA_CLASSIC + ("orientation_light", "seat_heating", "automatic_lid")
_TUMA_COMFORT = _MERA_CLASSIC + ("seat_heating",)
_TUMA_CLASSIC = ("oscillation", "water_temperature", "spray_arm", "descaling")

# Article number prefix -> model. The toilet reports its own article (SAP) number via Identification
# datapoint 0x10000 (x = color), e.g. "146.22x.xx.1"; the first digits after "146." tell the model.
# Provenance: these SAP-number -> model mappings are public Geberit article numbers (catalogue, product
# pages, manuals), cross-checkable anywhere; the device also reports its trade name via DP_NAME (0x10003)
# as a device-side confirmation. So this is a strong, verifiable mapping, not a guess from app sniffs. Only
# the Sela (146.22) is additionally confirmed on real hardware; the rest rest on the public SAP cross-check.
# The AquaClean 4000 has no app and no remote control, so it cannot be reached; listed for completeness.
_CAMA = ("water_temperature", "descaling", "user_detection")

# ---------------------------------------------------------------------------------------------------
# Capabilities: what a model really has, finer than the functions above. Every entity names the
# capabilities it needs (CAPS_OF); an entity is created only when the detected model has all of them.
# Per model from the Geberit Home app's device definitions (data points per model) checked against the
# model overview and the user manuals: docs/models.md lists the table with its sources.
# ---------------------------------------------------------------------------------------------------
CAPS = [
    "shower",                # anal shower: start/stop, running, spray intensity (5 levels)
    "pressure_3",            # spray intensity with 3 levels instead of 5
    "shower_position",       # spray arm position
    "water_temperature",
    "water_heating_switch",  # water heating on/off (session id 0c)
    "oscillation",
    "lady_shower",
    "user_detection",
    "basic_profile",         # stored basic settings (profile 0)
    "user_profiles",         # stored user profiles 1-4
    "remote_control",        # comes with a remote control
    "orientation_light",     # on/off, color, brightness
    "light_mode",            # light mode setting (options: light_mode_sela / light_mode_approach)
    "light_mode_sela",       # light mode: off, on, automatic (light sensor), time-controlled
    "light_mode_approach",   # light mode: off, on, on approach, in the dark
    "light_run_on",
    "light_time_slots",
    "light_sensor",          # light sensor sensitivity
    "descaling",             # descaling status, time left, last descaling, start / continue / cancel
    "descaling_v1",          # descaling steps: idle, preparing, waiting for user, descaling, flushing
    "descaling_v2",          # descaling steps: idle, descaling 2, waiting for user 1, descaling 1, waiting 2
    "descaling_v3",          # descaling steps: idle, descaling or flushing
    "descaling_counters",    # days until descaling, shower lock, showers, number of descalings
    "arm_cleaning",          # spray arm cleaning procedure
    "service_status",        # arm cleaning status (shower / dryer arm not cleaned)
    "spray_calibration",
    "shower_arm_adjust",     # shower arm offset and position adjustment
    "dryer",
    "no_dryer",              # models without a dryer (stop button "stop shower")
    "dryer_fan_power",
    "dryer_arm_adjust",
    "seat_heating",          # seat temperature (session id 07)
    "seat_heating_switch",   # seat heating on/off (session id 0b)
    "odor_extraction",
    "filter",                # odor extraction filter
    "system_flush",          # pre-flush in the profiles (needs the flush module too)
    "auto_flush",            # automatic flush actuation (needs the flush module too)
    "flush_module",          # interface module for an external flush (status 64)
    "lid",                   # automatic lid
    "care_mode",             # accessibility mode
    "demo_mode",
    "water_hardness",
    "registration",
    "draining",
    "trial",
    "statistics",            # usage statistics (49 counters, 4b timers): models of the classic AquaClean family
]
assert len(CAPS) <= 64

_C_SHOWER = ("shower", "shower_position", "water_temperature", "oscillation", "descaling", "descaling_counters",
             "arm_cleaning", "water_hardness", "registration", "statistics")
CAPS_SELA = _C_SHOWER + (
    "lady_shower", "user_detection", "basic_profile", "user_profiles", "remote_control", "orientation_light",
    "light_mode", "light_mode_sela", "light_run_on", "light_time_slots", "light_sensor", "descaling_v1", "spray_calibration",
    "auto_flush", "flush_module", "demo_mode", "no_dryer")
CAPS_MERA_CLASSIC = _C_SHOWER + (
    "lady_shower", "user_detection", "basic_profile", "user_profiles", "remote_control", "descaling_v2",
    "service_status", "shower_arm_adjust", "dryer", "dryer_fan_power", "dryer_arm_adjust", "odor_extraction",
    "filter", "system_flush", "auto_flush", "flush_module", "demo_mode")
CAPS_MERA_FLOOR = CAPS_MERA_CLASSIC + ("care_mode",)
CAPS_MERA_COMFORT = CAPS_MERA_CLASSIC + ("orientation_light", "light_mode", "light_mode_approach", "seat_heating",
                                         "lid")
CAPS_TUMA_COMFORT = _C_SHOWER + (
    "lady_shower", "user_detection", "basic_profile", "user_profiles", "remote_control", "descaling_v2", "dryer",
    "odor_extraction", "filter", "seat_heating", "seat_heating_switch", "water_heating_switch", "demo_mode")
# The complete toilet system can have the optional flush actuation (automatic flush on standing up, flush from
# the remote control or the app; Geberit product information). Discovery hides it when the module is missing.
CAPS_TUMA_COMFORT_COMPLETE = CAPS_TUMA_COMFORT + ("auto_flush", "flush_module")
CAPS_TUMA_CLASSIC = _C_SHOWER + ("basic_profile", "descaling_v2", "water_heating_switch", "no_dryer")
CAPS_CAMA = ("shower", "pressure_3", "water_temperature", "user_detection", "descaling", "descaling_v3",
             "spray_calibration", "draining", "registration", "no_dryer")
CAPS_CAMA_TESTSET = tuple(c for c in CAPS_CAMA if c != "registration") + ("trial",)

MODELS = [
    ("146.21", "AquaClean Mera Comfort", None, _MERA_COMFORT, CAPS_MERA_COMFORT),
    ("146.20", "AquaClean Mera Classic", None, _MERA_CLASSIC, CAPS_MERA_CLASSIC),
    ("146.24", "AquaClean Mera Classic", "floor_standing", _MERA_CLASSIC, CAPS_MERA_FLOOR),
    ("146.22", "AquaClean Sela", None, _SELA, CAPS_SELA),
    ("146.27", "AquaClean Tuma Comfort", "seat", _TUMA_COMFORT, CAPS_TUMA_COMFORT),
    ("146.29", "AquaClean Tuma Comfort", "complete_system", _TUMA_COMFORT, CAPS_TUMA_COMFORT_COMPLETE),
    ("146.31", "AquaClean Tuma Comfort", "floor_standing", _TUMA_COMFORT, CAPS_TUMA_COMFORT),
    ("146.07", "AquaClean Tuma Classic", "seat", _TUMA_CLASSIC, CAPS_TUMA_CLASSIC),
    ("146.09", "AquaClean Tuma Classic", "complete_system", _TUMA_CLASSIC, CAPS_TUMA_CLASSIC),
    ("146.32", "AquaClean Tuma Classic", "floor_standing", _TUMA_CLASSIC, CAPS_TUMA_CLASSIC),
    ("146.34", "AquaClean Cama", None, _CAMA, CAPS_CAMA),
    ("146.30", "AquaClean Cama", None, _CAMA, CAPS_CAMA_TESTSET),
    ("146.13", "AquaClean 4000", None, ("water_temperature", "user_detection"), ()),
]
for _m in MODELS:
    assert all(c in CAPS for c in _m[4]), _m[1]
# A model that is not in the list gets only what every model has
CAPS_UNKNOWN = tuple(c for c in CAPS if all(c in m[4] for m in MODELS[:-1]))


def _caps(names) -> int:
    return sum(1 << CAPS.index(n) for n in names)


# Options or ranges that depend on the model: (entity, capability, option key in lang / maximum raw value).
# Without one of these capabilities the entity keeps its own options (light: Sela, descaling: variant 1).
OPTION_VARIANTS = [
    ("light_activation", "light_mode_approach", "light_activation_approach"),
    ("descaling_state", "descaling_v2", "descaling_state_v2"),
    ("descaling_state", "descaling_v3", "descaling_state_v3"),
]
RANGE_VARIANTS = [("spray_intensity", "pressure_3", 2)]

ENTITIES: list[Ent] = [
    # --- shower session ---------------------------------------------------------------------------
    Ent("seated", "status", G_SESSION, {"device_class": "occupancy", CONF_ICON: "mdi:seat"}, setting=(0,)),
    Ent("shower_active", "status", G_SESSION, {"device_class": "running", CONF_ICON: "mdi:shower-head"}, setting=(1,)),
    Ent("lady_shower_active", "status", G_SESSION, {"device_class": "running", CONF_ICON: "mdi:shower"}, setting=(2,)),
    Ent("shower_toggle", "button", G_SESSION, {CONF_ICON: "mdi:shower-head"}, action=BTN_SHOWER),
    Ent("lady_shower_toggle", "button", G_SESSION, {CONF_ICON: "mdi:shower"}, action=BTN_LADY_SHOWER),
    # Status and commands (see the private reverse-engineering package)
    Ent("dryer_active", "status", G_SESSION, {"device_class": "running", CONF_ICON: "mdi:hair-dryer"}, setting=(3,)),
    _cmd("dryer_toggle", G_SESSION, 0x02, "mdi:hair-dryer"),
    _cmd("stop_all", G_SESSION, 0x03, "mdi:stop-circle-outline"),  # models with a dryer: "stop shower and dryer"
    _cmd("stop_shower", G_SESSION, 0x03, "mdi:stop-circle-outline"),  # the same command without a dryer
    # spray intensity / arm positions are stored 0..4 and shown 1..5 like the remote control;
    # water temperature 0 = heating off, 1..5
    _num("spray_intensity", G_SESSION, KIND_SESSION, 0x02, 4, 1, "mdi:water"),
    _num("spray_arm_position", G_SESSION, KIND_SESSION, 0x04, 4, 1, "mdi:arrow-expand-horizontal"),
    _num("lady_spray_intensity", G_SESSION, KIND_SESSION, 0x03, 4, 1, "mdi:water-outline"),
    _num("lady_spray_arm_position", G_SESSION, KIND_SESSION, 0x05, 4, 1, "mdi:arrow-expand-horizontal"),
    _num("water_temperature", G_SESSION, KIND_SESSION, 0x06, 5, 0, "mdi:thermometer-water"),
    Ent("oscillating_spray", "setting_switch", G_SESSION, {CONF_ICON: "mdi:sine-wave"}, setting=(KIND_SESSION, 0x01, 1, 0)),
    # Mera / Tuma Comfort only (not tested on a toilet): shown only when the model has the function and the
    # toilet answers
    _num("seat_heating", G_SESSION, KIND_SESSION, 0x07, 5, 0, "mdi:car-seat-heater"),
    _num("dryer_temperature", G_SESSION, KIND_SESSION, 0x08, 5, 0, "mdi:heat-wave"),
    # dryer air intensity, levels 1-5
    _num("dryer_intensity", G_SESSION, KIND_SESSION, 0x0D, 4, 1, "mdi:weather-windy"),
    Ent("dryer", "setting_switch", G_SESSION, {CONF_ICON: "mdi:hair-dryer"}, setting=(KIND_SESSION, 0x09, 1, 0)),
    Ent("odor_extraction", "setting_switch", G_SESSION, {CONF_ICON: "mdi:fan"}, setting=(KIND_SESSION, 0x00, 1, 0)),
    Ent("seat_heating_on", "setting_switch", G_SESSION, {CONF_ICON: "mdi:heat-wave"}, setting=(KIND_SESSION, 0x0B, 1, 0)),
    # pre-flush and automatic flush need a flush trigger module (status 64 bit 0); the Sela answers the ids anyway
    Ent("preflush", "setting_switch", G_SESSION, {CONF_ICON: "mdi:water-sync"}, setting=(KIND_SESSION, 0x0A, 1, 0),
        probe=(PROBE_MODULE, 0, 0)),
    Ent("wc_water_heating", "setting_switch", G_SESSION, {CONF_ICON: "mdi:water-thermometer"},
        setting=(KIND_SESSION, 0x0C, 1, 0)),
    # --- default settings (start values of every session) ----------------------------------------
    _num("default_spray_intensity", G_DEFAULTS, KIND_DEFAULT, 0x02, 4, 1, "mdi:water", CONFIG),
    _num("default_spray_arm_position", G_DEFAULTS, KIND_DEFAULT, 0x04, 4, 1, "mdi:arrow-expand-horizontal", CONFIG),
    _num("default_lady_spray_intensity", G_DEFAULTS, KIND_DEFAULT, 0x03, 4, 1, "mdi:water-outline", CONFIG),
    _num("default_lady_spray_arm_position", G_DEFAULTS, KIND_DEFAULT, 0x05, 4, 1, "mdi:arrow-expand-horizontal", CONFIG),
    _num("default_water_temperature", G_DEFAULTS, KIND_DEFAULT, 0x06, 5, 0, "mdi:thermometer-water", CONFIG),
    Ent(
        "default_oscillating_spray",
        "setting_switch",
        G_DEFAULTS,
        {CONF_ICON: "mdi:sine-wave", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_DEFAULT, 0x01, 1, 0),
    ),
    _num("default_seat_heating", G_DEFAULTS, KIND_DEFAULT, 0x07, 5, 0, "mdi:car-seat-heater", CONFIG),
    _num("default_dryer_temperature", G_DEFAULTS, KIND_DEFAULT, 0x08, 5, 0, "mdi:heat-wave", CONFIG),
    _num("default_dryer_intensity", G_DEFAULTS, KIND_DEFAULT, 0x0D, 4, 1, "mdi:weather-windy", CONFIG),
    Ent("default_dryer", "setting_switch", G_DEFAULTS, {CONF_ICON: "mdi:hair-dryer", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_DEFAULT, 0x09, 1, 0)),
    Ent("default_odor_extraction", "setting_switch", G_DEFAULTS, {CONF_ICON: "mdi:fan", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_DEFAULT, 0x00, 1, 0)),
    Ent("default_seat_heating_on", "setting_switch", G_DEFAULTS, {CONF_ICON: "mdi:heat-wave",
        CONF_ENTITY_CATEGORY: CONFIG}, setting=(KIND_DEFAULT, 0x0B, 1, 0)),
    Ent("default_preflush", "setting_switch", G_DEFAULTS, {CONF_ICON: "mdi:water-sync", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_DEFAULT, 0x0A, 1, 0), ha_profile=False, probe=(PROBE_MODULE, 0, 0)),
    Ent("default_wc_water_heating", "setting_switch", G_DEFAULTS, {CONF_ICON: "mdi:water-thermometer",
        CONF_ENTITY_CATEGORY: CONFIG}, setting=(KIND_DEFAULT, 0x0C, 1, 0), ha_profile=False),
    # --- device settings ---------------------------------------------------------------------------
    Ent("orientation_light", "light", G_DEVICE, {CONF_ICON: "mdi:lightbulb-night"}),
    Ent("light_activation", "setting_select", G_DEVICE, {CONF_ICON: "mdi:lightbulb-auto", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x03, 3, 0)),  # Sela: 0 off, 1 on, 2 automatic; time-controlled = 2 + windows (hub.cpp)
    Ent("light_color", "setting_select", G_DEVICE, {CONF_ICON: "mdi:palette", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x02, 6, 0)),
    _num("light_brightness", G_DEVICE, KIND_COMMON, 0x01, 4, 1, "mdi:brightness-6", CONFIG),
    Ent("light_run_on_time", "setting_select", G_DEVICE, {CONF_ICON: "mdi:timer-outline", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x0B, 4, 0)),
    Ent("light_window_1_start", "setting_time", G_DEVICE, {CONF_ENTITY_CATEGORY: CONFIG}, setting=(KIND_SCHEDULE, 0x0C, 1439, 0)),
    Ent("light_window_1_end", "setting_time", G_DEVICE, {CONF_ENTITY_CATEGORY: CONFIG}, setting=(KIND_SCHEDULE, 0x0D, 1439, 0)),
    Ent("light_window_2_start", "setting_time", G_DEVICE, {CONF_ENTITY_CATEGORY: CONFIG}, setting=(KIND_SCHEDULE, 0x0E, 1439, 0)),
    Ent("light_window_2_end", "setting_time", G_DEVICE, {CONF_ENTITY_CATEGORY: CONFIG}, setting=(KIND_SCHEDULE, 0x0F, 1439, 0)),
    Ent("demo_mode", "setting_switch", G_DEVICE, {CONF_ICON: "mdi:presentation-play", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x09, 1, 0)),
    # Mera Comfort only (not tested): automatic lid and odor extraction run-on
    Ent("lid_auto_open", "setting_switch", G_DEVICE, {CONF_ICON: "mdi:arrow-up-box", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x06, 1, 0)),
    Ent("lid_auto_close", "setting_switch", G_DEVICE, {CONF_ICON: "mdi:arrow-down-box", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x07, 1, 0)),
    _num("lid_sensor_range", G_DEVICE, KIND_COMMON, 0x04, 4, 1, "mdi:signal-distance-variant", CONFIG),
    Ent("odor_extraction_run_on", "setting_switch", G_DEVICE, {CONF_ICON: "mdi:fan-clock", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x05, 1, 0)),
    Ent("days_until_descaling", "days", G_DEVICE, {CONF_ICON: "mdi:calendar-clock"}),
    # descaling steps: the texts follow the model (OPTION_VARIANTS)
    Ent("descaling_state", "status_enum", G_DEVICE, {CONF_ICON: "mdi:water-sync"}, setting=(4,)),
    Ent("descaling_time_left", "status_value", G_DEVICE, {CONF_ICON: "mdi:timer-sand", "unit_of_measurement": "min"},
        setting=(5,)),
    _cmd("descaling_start", G_DEVICE, 0x06, "mdi:play-circle-outline", CONFIG),
    _cmd("descaling_continue", G_DEVICE, 0x07, "mdi:skip-next-circle-outline", CONFIG),
    _cmd("descaling_cancel", G_DEVICE, 0x08, "mdi:close-circle-outline", CONFIG),
    _cmd("shower_unlock", G_DEVICE, 0x09, "mdi:lock-open-variant-outline", CONFIG),
    Ent("showers_until_unlock", "counter_value", G_DEVICE, {CONF_ICON: "mdi:lock-clock", CONF_ENTITY_CATEGORY: DIAG},
        setting=(0x03, 0), probe=(PROBE_COUNTER, 0x03, 0)),
    Ent("descaling_requested", "counter_value", G_DEVICE, {CONF_ICON: "mdi:calendar-alert", CONF_ENTITY_CATEGORY: DIAG,
        "device_class": "timestamp"},
        setting=(0x05, 1), probe=(PROBE_COUNTER, 0x05, 0)),
    # descaling due + grace period (45: days until the shower is restricted), then the shower stops
    Ent("shower_restricted_in", "days", G_DEVICE, {CONF_ICON: "mdi:calendar-alert", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("shower_count", "counter", G_DEVICE, {CONF_ICON: "mdi:counter"}),
    # usage statistics (49 counters since commissioning, 4b timers: one entry per use, in seconds)
    *[Ent(k, d, G_DEVICE, dict(defs), stat=st, name_key=nk, name_args=na) for k, d, defs, st, nk, na in (
        ("stat_uptime", "stat_hours", {CONF_ICON: "mdi:timer-outline", CONF_ENTITY_CATEGORY: DIAG}, (1, 0), None, None),
        ("stat_run_time", "stat_hours", {CONF_ICON: "mdi:timer-play-outline", CONF_ENTITY_CATEGORY: DIAG}, (1, 1),
         None, None),
        ("stat_showers", "stat_counter", {CONF_ICON: "mdi:shower-head"}, (0, 3), None, None),
        ("stat_lady_showers", "stat_counter", {CONF_ICON: "mdi:shower-head"}, (0, 6), None, None),
        ("stat_oscillation", "stat_counter", {CONF_ICON: "mdi:sine-wave", CONF_ENTITY_CATEGORY: DIAG}, (0, 10),
         None, None),
        ("stat_lady_oscillation", "stat_counter", {CONF_ICON: "mdi:sine-wave", CONF_ENTITY_CATEGORY: DIAG},
         (0, 11), None, None),
        ("stat_dryer", "stat_counter", {CONF_ICON: "mdi:fan"}, (0, 12), None, None),
        ("stat_odor", "stat_counter", {CONF_ICON: "mdi:air-filter", CONF_ENTITY_CATEGORY: DIAG}, (0, 13), None,
         None),
        ("stat_seat_heating", "stat_counter", {CONF_ICON: "mdi:heat-wave", CONF_ENTITY_CATEGORY: DIAG}, (0, 14),
         None, None),
        ("stat_lid", "stat_counter", {CONF_ICON: "mdi:toilet", CONF_ENTITY_CATEGORY: DIAG}, (0, 15), None, None),
        ("stat_cleaning", "stat_counter", {CONF_ICON: "mdi:spray-bottle", CONF_ENTITY_CATEGORY: DIAG}, (0, 17),
         None, None),
        ("stat_users", "stat_counter", {CONF_ICON: "mdi:account-multiple"}, (0, 18), None, None),
        ("stat_system_flush", "stat_counter", {CONF_ICON: "mdi:waves", CONF_ENTITY_CATEGORY: DIAG}, (0, 19),
         None, None),
        *[(f"stat_profile_{p}", "stat_counter", {CONF_ICON: "mdi:account-check", CONF_ENTITY_CATEGORY: DIAG},
           (0, 19 + p), "stat_profile", {"n": p}) for p in (1, 2, 3, 4)],
        ("stat_last_shower", "stat_seconds", {CONF_ICON: "mdi:timer-sand"}, (2, 0), None, None),
        ("stat_avg_shower", "stat_seconds", {CONF_ICON: "mdi:timer-sand"}, (3, 0), None, None),
        ("stat_last_lady", "stat_seconds", {CONF_ICON: "mdi:timer-sand"}, (2, 1), None, None),
        ("stat_avg_lady", "stat_seconds", {CONF_ICON: "mdi:timer-sand"}, (3, 1), None, None),
        ("stat_last_sitting", "stat_seconds", {CONF_ICON: "mdi:seat-recline-normal"}, (2, 4), None, None),
        ("stat_avg_sitting", "stat_seconds", {CONF_ICON: "mdi:seat-recline-normal"}, (3, 4), None, None),
    )],
    Ent("last_descaling", "timestamp", G_DEVICE, {CONF_ICON: "mdi:calendar-check"}),
    Ent("descaling_count", "counter", G_DEVICE, {CONF_ICON: "mdi:counter", CONF_ENTITY_CATEGORY: DIAG}),
    # odor extraction filter: counter list 59 id 07, reset = command 2f
    Ent("days_until_filter_change", "days", G_DEVICE, {CONF_ICON: "mdi:air-filter"}, probe=(PROBE_COUNTER, 0x07, 0)),
    Ent("filter_reset", "button", G_DEVICE, {CONF_ICON: "mdi:air-filter", CONF_ENTITY_CATEGORY: CONFIG},
        action=BTN_FILTER_RESET),
    _cmd("odor_toggle", G_DEVICE, 0x0C, "mdi:fan"),
    _cmd("odor_run_on_toggle", G_DEVICE, 0x0D, "mdi:fan-clock"),
    Ent("last_error", "status_value", G_DEVICE, {CONF_ICON: "mdi:alert-circle-outline", CONF_ENTITY_CATEGORY: DIAG},
        setting=(6,)),
    Ent("last_error_text", "status_enum", G_DEVICE, {CONF_ICON: "mdi:alert-circle-outline", CONF_ENTITY_CATEGORY: DIAG},
        setting=(6,)),  # same status id 6, rendered as a module-aware label + hex (error_map)
    # Toilet fault as a problem alarm: status id 6 is 0 (no error) or a raw fault code; on = fault present.
    # Published as value != 0 via the status-flag path, so controllers (Homey, ioBroker, HA) get an alarm.
    Ent("toilet_problem", "status", G_DEVICE, {"device_class": "problem", CONF_ICON: "mdi:toilet"}, setting=(6,)),
    Ent("service_state", "status_enum", G_DEVICE, {CONF_ICON: "mdi:spray", CONF_ENTITY_CATEGORY: DIAG}, setting=(7,)),
    _cmd("arm_cleaning_start", G_DEVICE, 0x04, "mdi:spray", CONFIG),
    _cmd("arm_cleaning_next", G_DEVICE, 0x05, "mdi:skip-next", CONFIG),
    Ent("spray_calibration", "status", G_DEVICE, {"device_class": "running", CONF_ICON: "mdi:tune-vertical",
        CONF_ENTITY_CATEGORY: DIAG}, setting=(8,)),
    # automatic lid (Mera Comfort)
    _cmd("lid_toggle", G_DEVICE, 0x0A, "mdi:arrange-bring-forward"),
    _cmd("lid_calibration", G_DEVICE, 0x0B, "mdi:tune", CONFIG),
    _cmd("lid_angle_start", G_DEVICE, 0x21, "mdi:angle-acute", CONFIG),
    _cmd("lid_angle_up", G_DEVICE, 0x23, "mdi:plus", CONFIG),
    _cmd("lid_angle_down", G_DEVICE, 0x24, "mdi:minus", CONFIG),
    _cmd("lid_angle_save", G_DEVICE, 0x22, "mdi:content-save", CONFIG),
    # settings without a known model: shown only when the toilet answers the id
    Ent("auto_flush", "setting_select", G_DEVICE, {CONF_ICON: "mdi:water-sync", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x08, 2, 0), probe=(PROBE_MODULE, 0, 0)),
    # light sensor ("in the dark" light mode): only models with a light sensor; the Sela answers the id anyway
    Ent("light_sensor_sensitivity", "number", G_DEVICE, {CONF_ICON: "mdi:brightness-auto", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x0A, 4, 1)),
    Ent("accessibility_mode", "setting_switch", G_DEVICE, {CONF_ICON: "mdi:human-cane", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x0C, 1, 0)),
    Ent("water_hardness", "setting_select", G_DEVICE, {CONF_ICON: "mdi:water-opacity", CONF_ENTITY_CATEGORY: CONFIG},
        setting=(KIND_COMMON, 0x00, 2, 0)),
    # --- remote control ----------------------------------------------------------------------------
    Ent("remote_connected", "binary_sensor", G_REMOTE, {"device_class": "connectivity", CONF_ICON: "mdi:remote"}, remote=True),
    Ent("remote_passthrough", "binary_sensor", G_REMOTE, {CONF_ICON: "mdi:swap-horizontal", CONF_ENTITY_CATEGORY: DIAG}, remote=True),
    Ent("remote_last_button", "text_sensor", G_REMOTE, {CONF_ICON: "mdi:gesture-tap-button"}, remote=True),
    Ent("remote_button", "event", G_REMOTE, {CONF_ICON: "mdi:gesture-tap-button"}, remote=True),
    Ent("remote_firmware", "text_sensor", G_REMOTE, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:chip"}, remote=True),
    Ent("remote_address", "text_sensor", G_REMOTE, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:bluetooth"}, remote=True),
    Ent("remote_forward", "hub_switch", G_REMOTE, {CONF_ICON: "mdi:remote", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "RESTORE_DEFAULT_ON"}, action=SW_FORWARD, remote=True),
    Ent("remote_pairing", "hub_switch", G_REMOTE, {CONF_ICON: "mdi:link-variant", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "ALWAYS_OFF"}, action=SW_PAIRING, remote=True),
    Ent("remote_unpair", "button", G_REMOTE, {CONF_ICON: "mdi:link-off", CONF_ENTITY_CATEGORY: CONFIG},
        action=BTN_UNPAIR_REMOTE, remote=True),
    # --- the bridge itself --------------------------------------------------------------------------
    Ent("connected", "binary_sensor", G_MAIN, {"device_class": "connectivity", CONF_ICON: "mdi:transit-connection", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("app_connected", "binary_sensor", G_MAIN, {"device_class": "connectivity", CONF_ICON: "mdi:cellphone-link",
        CONF_ENTITY_CATEGORY: DIAG}),
    Ent("reconnects", "counter", G_MAIN, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:connection"}),
    Ent("timeouts", "counter", G_MAIN, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:timer-alert-outline"}),
    Ent("connection_uptime", "uptime", G_MAIN, {CONF_ICON: "mdi:calendar-check", CONF_ENTITY_CATEGORY: DIAG}),
    # Health & resource watchdog (step 4): one coarse verdict (visible) plus memory diagnostics (hidden)
    Ent("bridge_health", "status_enum", G_MAIN, {CONF_ICON: "mdi:heart-pulse"}, setting=(0x101,)),  # synthetic index, not a status id
    # Bridge problem alarm: on when the bridge decides the toilet is wedged and needs a power-cycle. The rich
    # four-state story stays in bridge_health; this boolean maps to an alarm capability on Homey / ioBroker / HA.
    Ent("bridge_problem", "binary_sensor", G_MAIN, {"device_class": "problem", CONF_ICON: "mdi:alert"}),
    Ent("free_heap", "gauge", G_MAIN, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:memory"}),
    Ent("min_free_heap", "gauge", G_MAIN, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:memory"}),
    Ent("largest_free_block", "gauge", G_MAIN, {CONF_ENTITY_CATEGORY: DIAG, CONF_ICON: "mdi:memory"}),
    Ent("pair_as_remote", "button", G_MAIN, {CONF_ICON: "mdi:link-plus", CONF_ENTITY_CATEGORY: CONFIG}, action=BTN_PAIR_AS_REMOTE),
    Ent("irk_android", "hub_switch", G_MAIN, {CONF_ICON: "mdi:keyboard-outline", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "RESTORE_DEFAULT_OFF"}, action=SW_IRK_ANDROID),
    Ent("advertise", "hub_switch", G_MAIN, {CONF_ICON: "mdi:bluetooth-audio", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "RESTORE_DEFAULT_ON"}, action=SW_ADVERTISE),
    Ent("refresh_settings", "button", G_MAIN, {CONF_ICON: "mdi:refresh", CONF_ENTITY_CATEGORY: DIAG}, action=BTN_REFRESH),
    Ent("model", "text_sensor", G_MAIN, {CONF_ICON: "mdi:toilet"}),
    Ent("recognized_user", "text_sensor", G_MAIN, {CONF_ICON: "mdi:account-search"}),
    Ent("active_user", "text_sensor", G_MAIN, {CONF_ICON: "mdi:account-check"}),
    Ent("search_devices", "button", G_MAIN, {CONF_ICON: "mdi:bluetooth-settings", CONF_ENTITY_CATEGORY: CONFIG},
        action=BTN_SEARCH_DEVICES),
    Ent("recognition_threshold", "threshold", G_MAIN, {CONF_ICON: "mdi:signal-distance-variant",
        CONF_ENTITY_CATEGORY: CONFIG}),
    Ent("functions", "text_sensor", G_MAIN, {CONF_ICON: "mdi:format-list-checks", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("rediscover", "button", G_MAIN, {CONF_ICON: "mdi:magnify-expand", CONF_ENTITY_CATEGORY: DIAG},
        action=BTN_REDISCOVER),
    Ent("firmware", "text_sensor", G_MAIN, {CONF_ICON: "mdi:chip", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("toilet_signal", "rssi", G_MAIN, {CONF_ENTITY_CATEGORY: DIAG}),
    Ent("toilet_address", "text_sensor", G_MAIN, {CONF_ICON: "mdi:bluetooth", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("search_toilet", "button", G_MAIN, {CONF_ICON: "mdi:bluetooth-audio", CONF_ENTITY_CATEGORY: DIAG},
        action=BTN_SEARCH_TOILET),
    Ent("device_type", "text_sensor", G_MAIN, {CONF_ICON: "mdi:toilet", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("article_number", "text_sensor", G_MAIN, {CONF_ICON: "mdi:barcode", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("serial_number", "text_sensor", G_MAIN, {CONF_ICON: "mdi:identifier", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("production_date", "text_sensor", G_MAIN, {CONF_ICON: "mdi:factory", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("commissioning_date", "text_sensor", G_MAIN, {CONF_ICON: "mdi:calendar-check", CONF_ENTITY_CATEGORY: DIAG}),
    Ent("registration", "status_enum", G_MAIN, {CONF_ICON: "mdi:account-key", CONF_ENTITY_CATEGORY: DIAG},
        setting=(0x100,)),  # proc 55, not a status id
    # --- discovery (other models): all disabled by default --------------------------------------------
    # Physical maintenance: shown with a long press of the maintenance key on the remote control (disclaimer accepted in the YAML);
    # this switch is there only while it is shown, to hide it again
    Ent("maintenance_mode", "hub_switch", G_MAIN, {CONF_ICON: "mdi:hammer-wrench", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "ALWAYS_OFF"}, action=SW_MAINTENANCE),
    Ent("discovery", "hub_switch", G_MAIN, {CONF_ICON: "mdi:magnify-scan", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "RESTORE_DEFAULT_OFF"}, action=SW_DISCOVERY, discovery=True),
    Ent("raw_status", "text_sensor", G_MAIN, {CONF_ICON: "mdi:code-brackets", CONF_ENTITY_CATEGORY: DIAG}, discovery=True),
    Ent("last_response", "text_sensor", G_MAIN, {CONF_ICON: "mdi:message-reply-text", CONF_ENTITY_CATEGORY: DIAG},
        discovery=True),
    # Free protocol request as a datapoint (every route): type "<context> <procedure> <args>" in hex, the answer
    # lands in last_response. Route-neutral twin of the aquaclean_raw_request native-API action.
    Ent("raw_request", "raw_text", G_MAIN, {CONF_ICON: "mdi:console-line", CONF_ENTITY_CATEGORY: DIAG},
        discovery=True),
    Ent("toilet_advertisement", "text_sensor", G_MAIN, {CONF_ICON: "mdi:bluetooth", CONF_ENTITY_CATEGORY: DIAG},
        discovery=True),
    Ent("procedures_seen", "text_sensor", G_MAIN, {CONF_ICON: "mdi:format-list-numbered", CONF_ENTITY_CATEGORY: DIAG},
        discovery=True),
    Ent("scan_settings", "button", G_MAIN, {CONF_ICON: "mdi:magnify", CONF_ENTITY_CATEGORY: DIAG}, action=BTN_SCAN,
        discovery=True),
    Ent("settings_scan", "text_sensor", G_MAIN, {CONF_ICON: "mdi:table-search", CONF_ENTITY_CATEGORY: DIAG},
        discovery=True),
    # statistics counters, histograms and timers (49 / 4a / 4b): read into the log, for finding out what the
    # toilet answers before they become sensors
    Ent("read_statistics", "button", G_MAIN, {CONF_ICON: "mdi:chart-box-outline", CONF_ENTITY_CATEGORY: DIAG},
        action=BTN_READ_STATS, discovery=True),
    # HomeKit: runtime on/off switch (only with homekit runtime_toggle; default off). Turning it on starts
    # HomeKit; turning it off restarts the bridge without it. See _homekit_to_code for the gating.
    Ent("homekit_enable", "hub_switch", G_MAIN, {CONF_ICON: "mdi:apple", CONF_ENTITY_CATEGORY: CONFIG,
        "restore_mode": "RESTORE_DEFAULT_OFF"}, action=SW_HOMEKIT, homekit=True),
    # HomeKit: show the setup QR code again (page /homekit with the web server; also a Home Assistant notification)
    Ent("homekit_qr", "button", G_MAIN, {CONF_ICON: "mdi:qrcode", CONF_ENTITY_CATEGORY: CONFIG},
        action=BTN_HOMEKIT_QR, homekit=True),
    # HomeKit: the pairing code and the /homekit page URL as a sensor, so every controller (Apple Home, Homey,
    # ioBroker, openHAB, Home Assistant) can show it without the Home Assistant-only notification
    Ent("homekit_code", "text_sensor", G_MAIN, {CONF_ICON: "mdi:cellphone-key", CONF_ENTITY_CATEGORY: DIAG},
        homekit=True),
    Ent("read_device_info", "button", G_MAIN, {CONF_ICON: "mdi:information-variant", CONF_ENTITY_CATEGORY: DIAG},
        action=BTN_READ_INFO, discovery=True),
    Ent("device_data", "text_sensor", G_MAIN, {CONF_ICON: "mdi:database-search", CONF_ENTITY_CATEGORY: DIAG},
        discovery=True),
    Ent("counters", "text_sensor", G_MAIN, {CONF_ICON: "mdi:counter", CONF_ENTITY_CATEGORY: DIAG}, discovery=True),
    # Functions without a known model: disabled by default.
    # Status 0a and 64-6a are read only while discovery is on.
    Ent("draining", "status", G_DEVICE, {"device_class": "running", CONF_ICON: "mdi:water-pump",
        CONF_ENTITY_CATEGORY: DIAG}, setting=(0x0A,), discovery=True),
    _cmd("drain", G_DEVICE, 0x34, "mdi:water-pump", CONFIG, True),
    Ent("flush", "command", G_DEVICE, {CONF_ICON: "mdi:ubisoft"}, action=0x25, probe=(PROBE_MODULE, 0, 0)),
    Ent("ssm_modules", "status_value", G_DEVICE, {CONF_ICON: "mdi:puzzle", CONF_ENTITY_CATEGORY: DIAG},
        setting=(0x64,), discovery=True),
    Ent("lid_offset", "status_value", G_DEVICE, {CONF_ICON: "mdi:angle-acute", CONF_ENTITY_CATEGORY: DIAG},
        setting=(0x68,), discovery=True),
    Ent("shower_arm_offset", "status_value", G_DEVICE, {CONF_ICON: "mdi:arrow-expand-horizontal",
        CONF_ENTITY_CATEGORY: DIAG}, setting=(0x69,), discovery=True),
    Ent("dryer_arm_offset", "status_value", G_DEVICE, {CONF_ICON: "mdi:arrow-expand-horizontal",
        CONF_ENTITY_CATEGORY: DIAG}, setting=(0x6A,), discovery=True),
    _cmd("shower_arm_adjust_start", G_DEVICE, 0xA3, "mdi:arrow-expand-horizontal", CONFIG, True),
    _cmd("shower_arm_adjust_up", G_DEVICE, 0xA5, "mdi:plus", CONFIG, True),
    _cmd("shower_arm_adjust_down", G_DEVICE, 0xAC, "mdi:minus", CONFIG, True),
    _cmd("shower_arm_adjust_save", G_DEVICE, 0xA4, "mdi:content-save", CONFIG, True),
    _cmd("dryer_arm_adjust_start", G_DEVICE, 0xAD, "mdi:arrow-expand-horizontal", CONFIG, True),
    _cmd("dryer_arm_adjust_up", G_DEVICE, 0xAF, "mdi:plus", CONFIG, True),
    _cmd("dryer_arm_adjust_down", G_DEVICE, 0xB0, "mdi:minus", CONFIG, True),
    _cmd("dryer_arm_adjust_save", G_DEVICE, 0xAE, "mdi:content-save", CONFIG, True),
    Ent("trial_end", "counter_value", G_DEVICE, {CONF_ICON: "mdi:calendar-end", CONF_ENTITY_CATEGORY: DIAG,
        "device_class": "timestamp"},
        setting=(0x11, 1), discovery=True),
    Ent("trial_remaining", "counter_value", G_DEVICE, {CONF_ICON: "mdi:calendar-clock", CONF_ENTITY_CATEGORY: DIAG},
        setting=(0x56, 2), discovery=True),
    Ent("trial_extended_remaining", "counter_value", G_DEVICE, {CONF_ICON: "mdi:calendar-clock",
        CONF_ENTITY_CATEGORY: DIAG}, setting=(0x5A, 2), discovery=True),
]
# Capabilities per entity. Every entity is listed here or in ESP_ONLY / TOILET_CORE: the build stops on an
# entity without a classification, so nothing is shown on a toilet by accident.
def _all(*keys, caps):
    return {k: caps for k in keys}


CAPS_OF = {
    **_all("seated", caps=("user_detection",)),
    **_all("shower_active", "shower_toggle", caps=("shower",)),
    **_all("spray_intensity", caps=("shower",)),
    **_all("stop_all", caps=("shower", "dryer")),
    **_all("stop_shower", caps=("shower", "no_dryer")),
    **_all("lady_shower_active", "lady_shower_toggle", "lady_spray_intensity", "lady_spray_arm_position",
           caps=("lady_shower",)),
    **_all("spray_arm_position", caps=("shower_position",)),
    **_all("water_temperature", caps=("water_temperature",)),
    **_all("wc_water_heating", caps=("water_heating_switch",)),
    **_all("oscillating_spray", caps=("oscillation",)),
    **_all("seat_heating", caps=("seat_heating",)),
    **_all("seat_heating_on", caps=("seat_heating_switch",)),
    **_all("dryer_active", "dryer_toggle", "dryer", "dryer_temperature", caps=("dryer",)),
    **_all("dryer_intensity", caps=("dryer_fan_power",)),
    **_all("odor_extraction", "odor_extraction_run_on", "odor_toggle", "odor_run_on_toggle",
           caps=("odor_extraction",)),
    **_all("days_until_filter_change", "filter_reset", caps=("filter",)),
    **_all("preflush", caps=("system_flush",)),
    **_all("auto_flush", caps=("auto_flush",)),
    **_all("flush", "ssm_modules", caps=("flush_module",)),
    **_all("orientation_light", "light_color", "light_brightness", caps=("orientation_light",)),
    **_all("light_activation", caps=("light_mode",)),
    **_all("light_run_on_time", caps=("light_run_on",)),
    **_all("light_window_1_start", "light_window_1_end", "light_window_2_start", "light_window_2_end",
           caps=("light_time_slots",)),
    **_all("light_sensor_sensitivity", caps=("light_sensor",)),
    **_all("descaling_time_left", "last_descaling", "descaling_start", "descaling_continue", "descaling_cancel",
           caps=("descaling",)),
    **_all("descaling_state", caps=("descaling",)),
    **_all("days_until_descaling", "shower_restricted_in", "shower_count", "descaling_count", "showers_until_unlock",
           "descaling_requested", "shower_unlock", caps=("descaling_counters",)),
    **_all("arm_cleaning_start", "arm_cleaning_next", caps=("arm_cleaning",)),
    **_all("service_state", caps=("service_status",)),
    **_all("spray_calibration", caps=("spray_calibration",)),
    **_all("shower_arm_offset", "shower_arm_adjust_start", "shower_arm_adjust_up", "shower_arm_adjust_down",
           "shower_arm_adjust_save", caps=("shower_arm_adjust",)),
    **_all("dryer_arm_offset", "dryer_arm_adjust_start", "dryer_arm_adjust_up", "dryer_arm_adjust_down",
           "dryer_arm_adjust_save", caps=("dryer_arm_adjust",)),
    **_all("lid_auto_open", "lid_auto_close", "lid_sensor_range", "lid_toggle", "lid_calibration", "lid_angle_start",
           "lid_angle_up", "lid_angle_down", "lid_angle_save", "lid_offset", caps=("lid",)),
    **_all("accessibility_mode", caps=("care_mode",)),
    **_all("demo_mode", caps=("demo_mode",)),
    **_all("water_hardness", caps=("water_hardness",)),
    **_all("registration", caps=("registration",)),
    **_all("draining", "drain", caps=("draining",)),
    **_all("trial_end", "trial_remaining", "trial_extended_remaining", caps=("trial",)),
    **_all("stat_uptime", "stat_run_time", caps=("statistics",)),
    **_all("stat_showers", "stat_last_shower", "stat_avg_shower", caps=("statistics", "shower")),
    **_all("stat_lady_showers", "stat_last_lady", "stat_avg_lady", caps=("statistics", "lady_shower")),
    **_all("stat_oscillation", caps=("statistics", "oscillation")),
    **_all("stat_lady_oscillation", caps=("statistics", "lady_shower", "oscillation")),
    **_all("stat_dryer", caps=("statistics", "dryer")),
    **_all("stat_odor", caps=("statistics", "odor_extraction")),
    **_all("stat_seat_heating", caps=("statistics", "seat_heating")),
    **_all("stat_lid", caps=("statistics", "lid")),
    **_all("stat_cleaning", caps=("statistics", "arm_cleaning")),
    **_all("stat_users", "stat_last_sitting", "stat_avg_sitting", caps=("statistics", "user_detection")),
    **_all("stat_system_flush", caps=("statistics", "flush_module")),
    **_all(*(f"stat_profile_{p}" for p in (1, 2, 3, 4)), caps=("statistics", "user_profiles")),
    **_all("remote_connected", "remote_passthrough", "remote_last_button", "remote_button", "remote_firmware",
           "remote_address", "remote_forward", "remote_pairing", "remote_unpair", "pair_as_remote",
           caps=("remote_control",)),
}
# the basic settings (profile 0): the capability of the setting plus stored basic settings
for _k in [e.key for e in ENTITIES if e.key.startswith("default_")]:
    CAPS_OF[_k] = CAPS_OF[_k.removeprefix("default_")] + ("basic_profile",)
# Entities of the ESP itself, and toilet entities every reachable toilet has
ESP_ONLY = {
    "app_connected", "irk_android", "advertise", "recognized_user", "active_user",
    "search_devices", "recognition_threshold", "toilet_address", "search_toilet", "maintenance_mode", "discovery",
    "toilet_advertisement", "read_statistics", "procedures_seen", "scan_settings", "settings_scan", "read_device_info", "device_data",
    "counters", "raw_status", "last_response", "raw_request", "toilet_signal", "homekit_enable", "homekit_qr",
    "homekit_code",
    "bridge_health", "bridge_problem", "free_heap", "min_free_heap", "largest_free_block",
}
TOILET_CORE = {
    "connected", "reconnects", "timeouts", "connection_uptime", "refresh_settings", "model", "functions",
    "rediscover", "firmware", "device_type", "article_number", "serial_number", "production_date",
    "commissioning_date", "last_error", "last_error_text", "toilet_problem",
}
for _e in ENTITIES:
    if _e.key in CAPS_OF:
        _e.caps = CAPS_OF[_e.key]
        assert all(c in CAPS for c in _e.caps), _e.key
    elif _e.key not in ESP_ONLY and _e.key not in TOILET_CORE:
        raise ValueError(f"geberit_aquaclean: entity {_e.key} has no capability classification")

# User profiles 1-4: the default settings once more per profile (53/54 <profile> <id>). Shown only for the
# profiles the toilet answers (feature discovery).
_profile_ents = []
_default_ents = [e for e in ENTITIES if e.key.startswith("default_")]
for _p, _g in zip(USERS, G_PROFILES):
    _toilet = _p in PROFILES
    # toilet profiles: shown when the toilet answers the profile; Home Assistant profiles: once created
    _probe = (KIND_DEFAULT, 0x02, _p) if _toilet else (PROBE_SLOT, _p, 0)
    # toilet profiles need stored user profiles; Home Assistant profiles are stored on the ESP
    _pcaps = ("user_profiles",) if _toilet else ()
    for _e in _default_ents:
        if not _toilet and not _e.ha_profile:
            continue  # no model known for it: a Home Assistant profile could not hide it on toilets without it
        _kind, _sid, _max, _off = _e.setting
        _profile_ents.append(
            Ent(
                f"{_g}_{_e.key.removeprefix('default_')}",
                _e.domain,
                _g,
                dict(_e.defaults),
                setting=(_kind if _toilet else KIND_LOCAL, _sid, _max, _off, _p),
                caps=tuple(c for c in _e.caps if c != "basic_profile") + _pcaps,
                name_key=_e.key.removeprefix("default_"),
                probe=(_e.probe if _e.probe is not None else None) if _toilet else _probe,
            )
        )
    if not _toilet:
        # Home Assistant profiles (4-6) only: a Lady shower oscillation and water temperature of their own,
        # kept on the bridge. The toilet has one shared slot for each, so this per-shower split cannot live on
        # the toilet; hub.cpp writes it into the session when the Lady shower starts for the recognized user.
        _profile_ents.append(
            Ent(f"{_g}_lady_oscillating_spray", "setting_switch", _g,
                {CONF_ICON: "mdi:sine-wave", CONF_ENTITY_CATEGORY: CONFIG},
                setting=(KIND_LOCAL, LOCAL_LADY_OSCILLATION, 1, 0, _p),
                caps=("oscillation", "lady_shower"), name_key="lady_oscillating_spray", probe=_probe)
        )
        _profile_ents.append(
            Ent(f"{_g}_lady_water_temperature", "number", _g,
                {CONF_ICON: "mdi:thermometer-water", CONF_ENTITY_CATEGORY: CONFIG},
                setting=(KIND_LOCAL, LOCAL_LADY_WATER_TEMPERATURE, 5, 0, _p),
                caps=("water_temperature", "lady_shower"), name_key="lady_water_temperature", probe=_probe)
        )
    # Activate = copy the profile into the session settings, as a profile key of the remote control does.
    # Store = session settings into the profile, as a long press of the profile key does.
    for _key, _act, _icon in (("activate", BTN_ACTIVATE_PROFILE, "mdi:account-check"),
                              ("store", BTN_STORE_PROFILE, "mdi:content-save")):
        _profile_ents.append(
            Ent(f"{_g}_{_key}", "button", G_SESSION, {CONF_ICON: _icon}, action=_act + _p,
                name_key=f"profile_{_key}", name_args={"n": _p}, probe=_probe, caps=_pcaps)
        )
    # every profile: clear its user (profiles 4-6 also back to the default settings)
    _profile_ents.append(
        Ent(f"{_g}_delete", "button", _g, {CONF_ICON: "mdi:account-remove", CONF_ENTITY_CATEGORY: CONFIG},
            action=BTN_DELETE_PROFILE + _p, name_key="profile_clear", probe=_probe, caps=_pcaps)
    )
    # IRK capture: the phone pairs once with the ESP and hands over its identity key (irk_capture.cpp)
    _profile_ents.append(
        Ent(f"{_g}_irk_capture", "button", _g, {CONF_ICON: "mdi:key-wireless", CONF_ENTITY_CATEGORY: CONFIG},
            action=BTN_IRK_CAPTURE + _p, name_key="irk_capture", probe=_probe, caps=_pcaps)
    )
    # show the stored IRK as a Home Assistant notification (the sensor itself only shows its last digits)
    _profile_ents.append(
        Ent(f"{_g}_irk_show", "button", _g, {CONF_ICON: "mdi:key-chain-variant", CONF_ENTITY_CATEGORY: DIAG},
            action=BTN_IRK_SHOW + _p, name_key="irk_show", probe=_probe, caps=_pcaps)
    )
    # The user of the profile: name, Bluetooth device, welcome light; recognized / seated
    for _key, _dom, _defaults in (
        ("user_name", "user_text", {CONF_ICON: "mdi:account", CONF_ENTITY_CATEGORY: CONFIG}),
        ("user_ha_device", "user_entity", {CONF_ICON: "mdi:account-badge", CONF_ENTITY_CATEGORY: CONFIG}),
        ("welcome_light", "user_welcome", {CONF_ICON: "mdi:lightbulb-on", CONF_ENTITY_CATEGORY: CONFIG}),
        ("user_recognized", "user_flag", {"device_class": "presence"}),
        ("user_active", "user_flag", {"device_class": "occupancy"}),
        ("user_rssi", "user_rssi", {CONF_ENTITY_CATEGORY: DIAG}),
        ("user_irk", "user_irk", {CONF_ICON: "mdi:key-variant", CONF_ENTITY_CATEGORY: DIAG}),
        # enter a known IRK (32 hex digits as Show IRK gives it, or base64); never shown again
        ("user_irk_set", "user_irk_set", {CONF_ICON: "mdi:key-plus", CONF_ENTITY_CATEGORY: CONFIG}),
    ):
        _profile_ents.append(
            Ent(f"{_g}_{_key}", _dom, _g, dict(_defaults), name_key=_key, probe=_probe, caps=_pcaps,
                name_args={"n": _p} if _key == "user_rssi" else None)
        )
_i = max(i for i, e in enumerate(ENTITIES) if e.group == G_DEFAULTS) + 1
ENTITIES[_i:_i] = _profile_ents

# ---------------------------------------------------------------------------------------------------
# Devices in Home Assistant: AquaClean Bridge (the ESP itself), AquaClean Toilet, Active
# session, User profile 1-6
# ---------------------------------------------------------------------------------------------------
G_TOILET = "toilet"
_REMOVED = {"advertise", "search_devices", "read_device_info", "device_data", "counters", "add_profile",
            "last_error"}  # numeric code superseded by last_error_text (module-aware label + hex)
_TO_MAIN = {"remote_passthrough", "remote_firmware", "remote_address", "remote_forward", "remote_pairing",
            "remote_unpair"}
_TO_SESSION = {"remote_connected", "remote_last_button", "remote_button", "app_connected", "recognized_user",
               "active_user"}
_TO_TOILET = {"toilet_signal", "reconnects", "timeouts", "connection_uptime", "refresh_settings", "functions", "rediscover",
              "firmware", "toilet_address", "model", "device_type", "article_number", "serial_number",
              "production_date", "commissioning_date", "raw_status", "last_response", "toilet_advertisement",
              "procedures_seen", "scan_settings", "settings_scan", "registration", "last_error", "last_error_text"}
_DISABLED = {"shower_restricted_in", "search_toilet", "remote_passthrough", "remote_firmware", "remote_address", "remote_forward",
             "refresh_settings", "functions", "timeouts", "rediscover", "remote_last_button"}
ENTITIES = [e for e in ENTITIES if e.key not in _REMOVED]
# entity categories
_CATEGORY = {"connected": None, "app_connected": None, "descaling_count": None, "discovery": DIAG, "model": DIAG}
for _e in ENTITIES:
    if _e.key in _CATEGORY:
        _e.defaults = {k: v for k, v in _e.defaults.items() if k != CONF_ENTITY_CATEGORY}
        if _CATEGORY[_e.key] is not None:
            _e.defaults[CONF_ENTITY_CATEGORY] = _CATEGORY[_e.key]
for _e in ENTITIES:
    if _e.key in _TO_MAIN:
        _e.group = G_MAIN
    elif _e.key in _TO_SESSION:
        _e.group = G_SESSION
    elif _e.key in _TO_TOILET or _e.group in (G_DEFAULTS, G_DEVICE):
        _e.group = G_TOILET
    elif _e.group == G_REMOTE:
        _e.group = G_SESSION
    _e.disabled = _e.key in _DISABLED

# Physical maintenance: procedures that need someone at the toilet. Information about maintenance (when it is
# due, error code, cleaning status) stays with the toilet.
MAINT_KEYS = {
    "descaling_state", "descaling_time_left", "descaling_start", "descaling_continue", "descaling_cancel", "shower_unlock", "arm_cleaning_start",
    "arm_cleaning_next", "spray_calibration", "lid_calibration", "lid_angle_start", "lid_angle_up", "lid_angle_down",
    "lid_angle_save", "filter_reset", "drain", "draining", "lid_offset", "shower_arm_offset", "dryer_arm_offset",
    *(f"{a}_arm_adjust_{k}" for a in ("shower", "dryer") for k in ("start", "up", "down", "save")),
}
for _e in ENTITIES:
    if _e.key in MAINT_KEYS:
        _e.group = G_MAINT

ENT_BY_KEY = {e.key: e for e in ENTITIES}
SETTING_SELECT_KEYS = [e.key for e in ENTITIES if e.domain == "setting_select"]

# Opt-in custom icons (custom_icons: true). These entities then carry an "aquaclean:<name>" icon
# instead of their mdi default, so the matching SVG from images/ui/ is shown wherever the
# aquaclean: prefix is known (Home Assistant with the HACS icon-set, the bridge's own web page with
# its bundle). Default off: the mdi icons are kept, which work everywhere with no extra install. The
# icon names here must match the set in tools/icons/generate.py (which bakes the frontends).
# An icon that the YAML overrode explicitly is left untouched (see the build loop).
# Which stock mdi icon maps to which aquaclean: SVG. Applied to EVERY entity that carries that mdi
# icon, so a function's icon is consistent everywhere it appears: the active session, the basic
# profile (default_*), every per-profile clone, and the statistics counters. These five mdi strings
# belong only to these functions, so matching by icon has no unintended collisions. An icon that the
# YAML overrode explicitly is left alone (see the build loop). Icon names must match the set in
# tools/icons/generate.py (which bakes the frontends).
_CUSTOM_ICON_BY_MDI = {
    "mdi:shower-head": "aquaclean:shower",
    "mdi:shower": "aquaclean:lady_shower",
    "mdi:water": "aquaclean:spray_intensity",
    "mdi:water-outline": "aquaclean:lady_spray_intensity",
    "mdi:car-seat-heater": "aquaclean:seat_heating",
}
CUSTOM_ICON_MAP = {
    _k: _CUSTOM_ICON_BY_MDI[_e.defaults[CONF_ICON]]
    for _k, _e in ENT_BY_KEY.items()
    if _e.defaults.get(CONF_ICON) in _CUSTOM_ICON_BY_MDI
}
# The lady-shower counter carries the generic shower-head icon upstream; give it the lady SVG.
if "stat_lady_showers" in ENT_BY_KEY:
    CUSTOM_ICON_MAP["stat_lady_showers"] = "aquaclean:lady_shower"


def _schema_for(e: Ent):
    d = e.domain
    if d in ("status", "binary_sensor"):
        return binary_sensor.binary_sensor_schema()
    if d in ("text_sensor", "user_irk"):
        return text_sensor.text_sensor_schema()
    if d == "counter":
        return sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing")
    if d == "gauge":
        # data_size so Homey / ioBroker map the heap byte counts to a size capability, not a generic esphome_*
        return sensor.sensor_schema(unit_of_measurement="B", device_class="data_size", accuracy_decimals=0,
                                    state_class="measurement")
    if d == "timestamp":
        return sensor.sensor_schema(device_class="timestamp", accuracy_decimals=0)
    if d == "status_value":
        return sensor.sensor_schema(accuracy_decimals=0, state_class="measurement")
    if d in ("status_enum",):
        return text_sensor.text_sensor_schema()
    if d == "stat_counter":
        return sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing")
    if d == "stat_hours":
        return sensor.sensor_schema(unit_of_measurement="h", device_class="duration", accuracy_decimals=1,
                                    state_class="total_increasing")
    if d == "stat_seconds":
        return sensor.sensor_schema(unit_of_measurement="s", device_class="duration", accuracy_decimals=0,
                                    state_class="measurement")
    if d == "counter_value":
        return sensor.sensor_schema(accuracy_decimals=0)
    if d == "command":
        return button.button_schema(HubButton)
    if d == "days":
        return sensor.sensor_schema(unit_of_measurement="d", state_class="measurement", accuracy_decimals=0)
    if d == "uptime":
        # the moment of connecting (ISO 8601, exact): Home Assistant shows "3 hours ago", "2 days ago" by itself
        return text_sensor.text_sensor_schema(device_class="timestamp", icon="mdi:bluetooth-connect")
    if d == "button":
        return button.button_schema(HubButton)
    if d == "hub_switch":
        return switch.switch_schema(HubSwitch)
    if d == "setting_switch":
        return switch.switch_schema(SettingSwitch, default_restore_mode="DISABLED")
    if d == "number":
        return number.number_schema(SettingNumber)
    if d == "setting_select":
        return select.select_schema(SettingSelect)
    if d == "setting_time":
        return datetime.time_schema(SettingTime)
    if d == "event":
        return event.event_schema(RemoteEvent)
    if d in ("user_text", "user_entity"):
        return text.text_schema(UserText, mode="text")
    if d == "user_irk_set":
        return text.text_schema(UserText, mode="password")
    if d == "raw_text":
        return text.text_schema(RawRequestText, mode="text")
    if d in ("user_device", "user_welcome"):
        return select.select_schema(UserSelect)
    if d == "user_flag":
        return binary_sensor.binary_sensor_schema()
    if d == "user_rssi":
        return sensor.sensor_schema(unit_of_measurement="dBm", device_class="signal_strength",
                                    state_class="measurement", accuracy_decimals=0)
    if d == "rssi":
        return sensor.sensor_schema(unit_of_measurement="dBm", device_class="signal_strength",
                                    state_class="measurement", accuracy_decimals=0)
    if d == "threshold":
        return number.number_schema(ThresholdNumber, unit_of_measurement="dBm")
    if d == "light":
        return light.RGB_LIGHT_SCHEMA.extend({cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(OrientationLight)})
    raise ValueError(d)


# Internal domain of an entity -> its ESPHome (and MQTT) component. The generators of the controller apps
# (nodered/, homey/, iobroker/) import this so the MQTT topic <prefix>/<component>/<object_id> stays in step
# with the firmware.
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


def _entity_name(lang: dict, e: Ent, sub_devices: bool, mqtt: bool = False) -> str:
    name = lang["names"][e.name_key or e.key]
    if e.name_args:
        name = name.format(**e.name_args)
    # Without sub-devices the non-session names would collide with the session entities, so prefix the group.
    # Over MQTT the sub-device is not part of the topic (see _mqtt_object_id), so the profile entities collide
    # with the session and with each other even with sub-devices; prefix only the profile groups there, which
    # leaves every other topic unchanged.
    if (not sub_devices and e.group is not None and e.group != G_SESSION) or (mqtt and e.group in G_PROFILES):
        name = f"{lang['devices'][e.group]} {name}"
    return name


def _mqtt_object_id(name: str) -> str:
    """ESPHome's MQTT object id: per UTF-8 byte, ' ' -> '_', A-Z -> a-z, anything but [a-z0-9_-] -> '_'."""
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


def _expand_entities(config: dict) -> dict:
    """Fill in every entity (name in the chosen language, device, defaults) before validation."""
    if not isinstance(config, dict):
        raise cv.Invalid("expected a mapping")
    config = dict(config)
    lang_code = str(config.get(CONF_LANGUAGE, "en")).lower()
    lang_code = LANGUAGE_ALIASES.get(lang_code, lang_code)
    if CONF_LANGUAGE in config:
        config[CONF_LANGUAGE] = lang_code
    if lang_code not in I18N:
        raise cv.Invalid(f"language must be one of {', '.join(LANGUAGES)}", path=[CONF_LANGUAGE])
    lang = I18N[lang_code]
    sub_devices = config.get(CONF_SUB_DEVICES, True)
    web_groups = config.get(CONF_WEB_SERVER_GROUPS, False)
    has_remote = CONF_REMOTE_MAC_ADDRESS in config
    # HomeKit is only built with "enabled: true"; a disabled block is dropped here, before anything uses it
    if CONF_HOMEKIT in config:
        hk = config[CONF_HOMEKIT]
        if not isinstance(hk, dict):
            raise cv.Invalid("expected homekit options (enabled, setup_code, ...)", path=[CONF_HOMEKIT])
        if not cv.boolean(hk.get(CONF_ENABLED, False)):
            del config[CONF_HOMEKIT]
    has_homekit = CONF_HOMEKIT in config
    for e in ENTITIES:
        if e.remote and not has_remote:
            if e.key in config:
                raise cv.Invalid(f"'{e.key}' needs '{CONF_REMOTE_MAC_ADDRESS}'", path=[e.key])
            continue
        if e.homekit and not has_homekit:
            if e.key in config:
                raise cv.Invalid(f"'{e.key}' needs '{CONF_HOMEKIT}'", path=[e.key])
            continue
        # the Apple Home on/off switch exists only with homekit: runtime_toggle: true
        if e.key == "homekit_enable" and not (has_homekit and config[CONF_HOMEKIT].get(CONF_RUNTIME_TOGGLE)):
            if e.key in config:
                raise cv.Invalid("'homekit_enable' needs homekit: runtime_toggle: true", path=[e.key])
            continue
        user = config.get(e.key) or {}
        if not isinstance(user, dict):
            raise cv.Invalid("expected entity options (name, icon, disabled_by_default, ...)", path=[e.key])
        # validated (duplicate check, object_id) with the English name for scripts without Latin letters;
        # to_code then shows the translated name with the English key
        merged = {CONF_NAME: _entity_name(I18N["en"] if lang_code in ASCII_ID_LANGUAGES else lang, e,
                                          bool(sub_devices))}
        merged.update(e.defaults)
        if e.discovery or e.disabled:
            merged[CONF_DISABLED_BY_DEFAULT] = True
        if e.domain == "light":
            merged.update({"default_transition_length": "0s", "gamma_correct": 1.0, "restore_mode": "ALWAYS_OFF"})
        if sub_devices and e.group is not None:
            merged[CONF_DEVICE_ID] = DEVICE_IDS[e.group]
        if web_groups:
            # web interface (packages/web.yaml): one group per device, entities in the order of this list
            merged["web_server"] = {
                "sorting_group_id": f"aquaclean_web_{e.group or 'bridge'}",
                "sorting_weight": ENTITIES.index(e),
            }
        merged.update(user)
        config[e.key] = merged
    return config


BASE_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AquaCleanHub),
            cv.GenerateID(CONF_LINK_ID): cv.declare_id(RemoteLink),
            cv.Optional(CONF_LANGUAGE, default="en"): cv.one_of(*LANGUAGES, lower=True),
            cv.Optional(CONF_SUB_DEVICES, default=True): cv.boolean,
            # set by packages/web.yaml: group the entities in the web interface like the devices
            cv.Optional(CONF_WEB_SERVER_GROUPS, default=False): cv.boolean,
            cv.Optional(CONF_STATESTREAM_TOPIC, default=""): cv.string,
            cv.Optional(CONF_LOG_OBFUSCATE, default=False): cv.boolean,
            # physical maintenance: accept the disclaimer; then a long press of the maintenance key shows it
            cv.Optional(CONF_MAINT_ACCEPTED, default=False): cv.boolean,
            # opt-in: give a few shower/spray entities the own "aquaclean:" SVG icons instead of mdi.
            # Needs the frontend(s) that know the prefix; default off keeps the universal mdi icons.
            cv.Optional(CONF_CUSTOM_ICONS, default=False): cv.boolean,
            # Apple Home (HomeKit) directly on the ESP: shower, lady shower, dryer and odor extraction as switches
            cv.Optional(CONF_HOMEKIT): HOMEKIT_SCHEMA,
            cv.Optional(CONF_MAINT_CODE): cv.string,
            cv.Optional(CONF_REMOTE_MAC_ADDRESS): cv.mac_address,
            cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
            cv.Optional(CONF_POLL_INTERVAL, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_RESPONSE_TIMEOUT, default="1s"): cv.positive_time_period_milliseconds,
            # session settings are read this often while somebody is seated
            cv.Optional(CONF_SESSION_REFRESH, default="2s"): cv.positive_time_period_milliseconds,
            # other settings are read again this often (changes made with the remote control)
            cv.Optional(CONF_SETTINGS_REFRESH, default="5min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_SET_CLOCK, default=True): cv.boolean,
            cv.Optional(CONF_IDLE_TIMEOUT, default="15s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PAIRING_DURATION, default="120s"): cv.positive_time_period_milliseconds,
            **{
                (cv.Optional(e.key) if e.remote or e.homekit else cv.Required(e.key)): _schema_for(e)
                for e in ENTITIES
            },
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)

CONFIG_SCHEMA = cv.All(_expand_entities, BASE_SCHEMA)


def _web_group_names(config):
    # Group names of the web interface follow the language: groups whose name is "auto" (the default of
    # packages/web.yaml) get the device names of the chosen language
    web = fv.full_config.get().get("web_server")
    if not web or not config.get(CONF_WEB_SERVER_GROUPS):
        return
    devices = I18N[config[CONF_LANGUAGE]]["devices"]
    names = {"aquaclean_web_toilet": devices["toilet"], "aquaclean_web_session": devices["session"],
             "aquaclean_web_maintenance": devices["maintenance"]}
    for p in USERS:
        names[f"aquaclean_web_profile_{p}"] = devices[f"profile_{p}"]
    for group in web.get("sorting_groups", []):
        gid = str(group[CONF_ID])
        if gid in names and str(group[CONF_NAME]).startswith("auto"):
            group[CONF_NAME] = names[gid]


def _mqtt_unique_names(config):
    # On the MQTT route the topic is <prefix>/<component>/<object_id> and the object id is the sanitized entity
    # name only; the sub-device is not part of it. With sub-devices the profile entities would otherwise take the
    # same names as the session entities (and as each other), so their topics would collide and one command would
    # change the running session and every stored profile. Prefix the profile groups' names here (MQTT route only),
    # so their object ids become unique while every other topic stays the same. Then guard that nothing collides.
    lang_code = config[CONF_LANGUAGE]
    name_lang = I18N["en"] if lang_code in ASCII_ID_LANGUAGES else I18N[lang_code]
    sub = config[CONF_SUB_DEVICES]
    for e in ENTITIES:
        if e.key in config and e.group in G_PROFILES:
            if config[e.key][CONF_NAME] == _entity_name(name_lang, e, sub):  # not renamed in the YAML
                config[e.key][CONF_NAME] = _entity_name(name_lang, e, sub, mqtt=True)
    seen = {}
    for e in ENTITIES:
        if e.key not in config:
            continue
        topic = f"{PLATFORM[e.domain]}/{_mqtt_object_id(config[e.key][CONF_NAME])}"
        if seen.get(topic, e.key) != e.key:
            raise cv.Invalid(
                f"geberit_aquaclean: the MQTT topic '{topic}' would be shared by '{seen[topic]}' and '{e.key}'. "
                "Two entities with the same name and component publish and receive on one topic over MQTT; "
                "give one of them a different name."
            )
        seen[topic] = e.key


def _final_validate(config):
    full = fv.full_config.get()
    if "api" in full and "mqtt" in full:
        raise cv.Invalid(
            "geberit_aquaclean: choose one route, the native API (api:) or MQTT (mqtt:), not both: Home Assistant "
            "would show everything twice. In examples/atoms3-lite.yaml the substitution 'route' picks one."
        )
    _web_group_names(config)
    if "mqtt" in full:
        _mqtt_unique_names(config)
    if not config[CONF_SUB_DEVICES]:
        return config
    declared = {str(d[CONF_ID]) for d in fv.full_config.get().get("esphome", {}).get("devices", [])}
    needed = [v for g, v in DEVICE_IDS.items() if g != G_REMOTE or CONF_REMOTE_MAC_ADDRESS in config]
    missing = [v for v in needed if v not in declared]
    if missing:
        raise cv.Invalid(
            f"sub_devices: true needs these ids under 'esphome: devices:': {', '.join(missing)} "
            "(packages/aquaclean.yaml declares them), or set sub_devices: false"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


# ---------------------------------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------------------------------


def _user_of(e: Ent) -> int:
    return int(e.group.removeprefix("profile_"))


def _c_str(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def _log_table(code: str):
    """Log texts of one language (lang/log/<code>.json, English -> translation), sorted bytewise like strcmp."""
    path = _LANG_DIR / "log" / f"{code}.json"
    if not path.exists():
        return None, 0
    with open(path, encoding="utf-8") as f:
        texts = json.load(f)
    # a translation must have the same format codes in the same order as the English text, or it could crash
    # the logger: such a translation is left out (English is logged)
    fmt = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcspn%]")
    for en in [en for en, tr in texts.items() if fmt.findall(en) != fmt.findall(tr)]:
        _LOGGER.warning("lang/log/%s.json: format codes differ, left out: %s", code, en)
        del texts[en]
    items = sorted(texts.items(), key=lambda kv: kv[0].encode("utf-8"))
    name = f"aquaclean_log_{code}"
    body = ",\n".join(f"  {{{_c_str(en)}, {_c_str(tr)}}}" for en, tr in items)
    cg.add_global(
        cg.RawStatement(f"static const esphome::geberit_aquaclean::LogText {name}[] = {{\n{body}\n}};")
    )
    return cg.RawExpression(name), len(items)


def _table(name: str, items: list[str]):
    return cg.shared_progmem_array(name, cg.const_char_ptr, [str(i) for i in items])


@coroutine_with_priority(CoroPriority.FINAL)
async def _homekit_libsodium():
    from esphome.components.esp32 import KEY_COMPONENTS, KEY_ESP32
    components = CORE.data[KEY_ESP32][KEY_COMPONENTS]
    if not any(name.endswith("/libsodium") for name in components):
        add_idf_component(name="espressif/libsodium", ref="~1.0.20")


async def _homekit_to_code(hub, config, lang, created_vars):
    hk = config[CONF_HOMEKIT]
    if "mdns" not in CORE.config or CORE.config["mdns"].get("disabled", False):
        raise cv.Invalid("homekit needs mdns (Apple Home finds the ESP via mDNS)", path=[CONF_HOMEKIT])
    for comp in HOMEKIT_SDK_COMPONENTS:
        add_idf_component(name=comp, path=str(HOMEKIT_SDK_DIR / comp))
    # libsodium: the one ESPHome uses for API encryption (esphome/libsodium) when it is there, otherwise
    # espressif/libsodium; both in one build make ESP-IDF stop ("Can't decide which one to pick")
    CORE.add_job(_homekit_libsodium)
    include_builtin_idf_component("esp_http_server")
    add_idf_sdkconfig_option("CONFIG_HAP_HTTP_SERVER_PORT", hk[CONF_PORT])
    add_idf_sdkconfig_option("CONFIG_HAP_HTTP_STACK_SIZE", 10240)
    add_idf_sdkconfig_option("CONFIG_HAP_HTTP_MAX_OPEN_SOCKETS", 4)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_HKDF_C", True)
    cg.add_define("USE_AQUACLEAN_HOMEKIT")
    cg.add(hub.set_homekit(hk.get(CONF_SETUP_CODE, ""), hk[CONF_RESET_PAIRINGS]))
    # With the runtime toggle, HomeKit waits for the "Apple Home" switch (default off); otherwise it is on.
    cg.add(hub.set_homekit_runtime_enabled(not hk.get(CONF_RUNTIME_TOGGLE, False)))
    names = [lang["buttons"]["shower"], lang["buttons"]["lady_shower"], lang["names"]["dryer"],
             lang["names"]["odor_extraction"]]
    for i, n in enumerate(names):
        cg.add(hub.set_homekit_name(i, n))
    nothing = cg.RawExpression("nullptr")
    cg.add(hub.set_homekit_entities(created_vars.get("shower_active", nothing),
                                    created_vars.get("lady_shower_active", nothing),
                                    created_vars.get("dryer_active", nothing),
                                    created_vars.get("odor_extraction", nothing)))


# Translated name -> entity key of the English name (ASCII_ID_LANGUAGES)
_HASH_OVERRIDE: dict[str, int] = {}


def _install_hash_override() -> None:
    """ESPHome computes the entity key from the name in setup_entity; give translated names the English key."""
    if getattr(entity_helpers.fnv1_hash_object_id, "_aquaclean", False):
        return
    original = entity_helpers.fnv1_hash_object_id

    def hashed(name: str) -> int:
        return _HASH_OVERRIDE[name] if name in _HASH_OVERRIDE else original(name)

    hashed._aquaclean = True
    entity_helpers.fnv1_hash_object_id = hashed


async def to_code(config):
    # GATT server: toilet services (~60 attributes) + the IRK capture services (19); default limit 100
    add_idf_sdkconfig_option("CONFIG_BT_GATT_MAX_SR_ATTRIBUTES", 150)
    # and services: GAP, GATT, 5 toilet services, 2 IRK capture services; default limit 8
    add_idf_sdkconfig_option("CONFIG_BT_GATT_MAX_SR_PROFILES", 16)
    lang_code = config[CONF_LANGUAGE]
    lang = I18N[lang_code]
    hub = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(hub, config)
    await ble_client.register_ble_node(hub, config)

    cg.add(hub.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(hub.set_response_timeout(config[CONF_RESPONSE_TIMEOUT]))
    cg.add(hub.set_session_refresh(config[CONF_SESSION_REFRESH]))
    cg.add(hub.set_settings_refresh(config[CONF_SETTINGS_REFRESH]))
    cg.add(hub.set_set_clock(config[CONF_SET_CLOCK]))
    cg.add(hub.set_default_language(LANGUAGES.index(lang_code)))
    if CONF_TIME_ID in config:
        cg.add(hub.set_time(await cg.get_variable(config[CONF_TIME_ID])))

    # Sub-devices: declared in the package (esphome: devices:), names follow the chosen language
    if config[CONF_SUB_DEVICES]:
        for group, dev_id in DEVICE_IDS.items():
            dev = await cg.get_variable(ID(dev_id, type=Device))
            cg.add(dev.set_name(lang["devices"][group]))
            if group == G_MAINT:
                cg.add(hub.set_maintenance_device(dev))

    # Remote link (ESP as remote towards the toilet, toilet towards the remote control, app relay)
    link = cg.new_Pvariable(config[CONF_LINK_ID])
    await cg.register_component(link, {})
    cg.add(link.set_hub(hub))
    cg.add(hub.set_link(link))
    if CONF_REMOTE_MAC_ADDRESS in config:
        # 00:00:00:00:00:00 = automatic: the remote control that pairs in the pairing window is used
        cg.add(link.set_remote_address(config[CONF_REMOTE_MAC_ADDRESS].as_hex))
    cg.add(link.set_idle_timeout(config[CONF_IDLE_TIMEOUT]))
    cg.add(link.set_pairing_duration(config[CONF_PAIRING_DURATION]))
    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gap_event_handler(ble, link)
    esp32_ble.register_gattc_event_handler(ble, link)
    cg.add_define("USE_ESP32_BLE_SERVER")
    esp32_ble.register_gatts_event_handler(ble, link)
    from esphome.components import esp32_ble_tracker

    esp32_ble_tracker.register_ble_features({esp32_ble_tracker.BLEFeatures.ESP_BT_DEVICE})
    esp32_ble_tracker._request_listener_slot()  # noqa: SLF001

    # Log texts in the chosen language (English stays as written in the code)
    for i, code in enumerate(LANGUAGES):
        table, count = _log_table(code)
        if table is not None:
            cg.add(hub.set_log_table(i, table, count))

    # Translations used at runtime (select options, remote control button texts)
    for i, code in enumerate(LANGUAGES):
        tr = I18N[code]
        cg.add(
            link.set_button_texts(
                i,
                _table(f"aquaclean_buttons_{code}", [tr["buttons"][k] for k in BUTTON_KEYS] + [tr["buttons"]["unknown"]]),
                _table(f"aquaclean_button_states_{code}", tr["button_states"]),
            )
        )

    # Models, feature names and model variants per language (model entity, functions entity)
    for prefix, name, variant, feats, caps in MODELS:
        cg.add(hub.add_model(prefix, name, VARIANTS.index(variant) if variant else -1, _features(*feats),
                             cg.RawExpression(f"{_caps(caps)}ULL")))
    cg.add(hub.set_unknown_caps(cg.RawExpression(f"{_caps(CAPS_UNKNOWN)}ULL")))
    for i, code in enumerate(LANGUAGES):
        tr = I18N[code]
        cg.add(
            hub.set_texts(
                i,
                _table(f"aquaclean_features_{code}", [tr["features"][k] for k in FEATURES]),
                _table(f"aquaclean_variants_{code}", [tr["variants"][k] for k in VARIANTS]),
                tr["texts"]["not_supported"],
                tr["texts"]["model_unknown"],
            )
        )

    for i, code in enumerate(LANGUAGES):
        tr = I18N[code]
        cg.add(hub.set_user_labels(i, _table(f"aquaclean_users_{code}", [tr["devices"][g] for g in G_PROFILES]),
                                   tr["texts"]["nobody"]))

    # linked Home Assistant device trackers are followed through the API, or in the MQTT route through
    # Home Assistant's mqtt_statestream
    if "api" in CORE.config:
        cg.add_define("USE_API_HOMEASSISTANT_STATES")
        cg.add_define("USE_API_HOMEASSISTANT_SERVICES")  # IRK shown as a Home Assistant notification
    # custom_icons: the aquaclean: icon strings set below render in Home Assistant (icon-set module) and
    # on the bridge's own web_server v3 page only once that page knows the prefix. With local:true the page
    # is a compressed blob baked into ESPHome's server_index_v3.h, so we inject the icon collection into it
    # here, at build time — it travels with the component over github:// and reapplies on every build, with
    # no manual patch step. Guarded to web_server builds; apply() never fails the build over a cosmetic icon.
    if "web_server" in CORE.config:
        from . import web_icons

        _changed = web_icons.apply(enable=bool(config[CONF_CUSTOM_ICONS]))
        if config[CONF_CUSTOM_ICONS] and _changed:
            _LOGGER.info("custom_icons: injected aquaclean icons into the web page (%d array(s))", _changed)
    if config[CONF_LOG_OBFUSCATE]:
        cg.add(hub.set_log_obfuscate(True))
    if config.get(CONF_MAINT_CODE):
        _LOGGER.warning("%s is no longer used: physical maintenance is switched on with a long press of the "
                        "maintenance key on the remote control", CONF_MAINT_CODE)
    cg.add(hub.set_maintenance_consent(config[CONF_MAINT_ACCEPTED]))
    if config[CONF_STATESTREAM_TOPIC]:
        cg.add(hub.set_statestream_topic(config[CONF_STATESTREAM_TOPIC]))
    welcome_tables = {
        code: _table(f"aquaclean_welcome_light_{code}", I18N[code]["options"]["welcome_light"]) for code in LANGUAGES
    }
    gated_keys = []
    created_vars = {}
    translate_names = lang_code in ASCII_ID_LANGUAGES
    if translate_names and "mqtt" in CORE.config:
        # MQTT builds its topics from the name at runtime, so translated names would collide there
        _LOGGER.warning("language %s with MQTT: entity names stay English (MQTT topics are made from the name)",
                        lang_code)
        translate_names = False
    if translate_names:
        _install_hash_override()
    for e in ENTITIES:
        if e.key not in config:
            continue
        conf = config[e.key]
        if translate_names:
            english = _entity_name(I18N["en"], e, config[CONF_SUB_DEVICES])
            if conf[CONF_NAME] == english:  # not renamed in the YAML
                shown = _entity_name(lang, e, config[CONF_SUB_DEVICES])
                key = fnv1_hash_object_id(english)
                if _HASH_OVERRIDE.get(shown, key) != key:
                    raise cv.Invalid(f"lang/{lang_code}.yaml: '{shown}' is the name of two different entities")
                _HASH_OVERRIDE[shown] = key
                conf = {**conf, CONF_NAME: shown}
        if config[CONF_CUSTOM_ICONS] and e.key in CUSTOM_ICON_MAP:
            # replace only the default mdi icon, never one the YAML set explicitly
            if conf.get(CONF_ICON) == e.defaults.get(CONF_ICON):
                conf = {**conf, CONF_ICON: CUSTOM_ICON_MAP[e.key]}
        d = e.domain
        if d == "status":
            var = await binary_sensor.new_binary_sensor(conf)
            cg.add(hub.add_status_flag(e.setting[0], var))
        elif d == "binary_sensor":
            var = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(link if e.remote else hub, f"set_{e.key}_sensor")(var))
        elif d == "text_sensor":
            var = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(link if e.remote or e.key == "toilet_address" else hub, f"set_{e.key}_sensor")(var))
        elif d == "uptime":
            var = await text_sensor.new_text_sensor(conf)
            cg.add(hub.set_connection_uptime_sensor(var))
        elif d in ("stat_counter", "stat_hours", "stat_seconds"):
            var = await sensor.new_sensor(conf)
            cg.add(hub.add_stat_sensor(e.stat[0], e.stat[1], var))
        elif d in ("counter", "days", "timestamp", "gauge"):
            var = await sensor.new_sensor(conf)
            cg.add(getattr(hub, f"set_{e.key}_sensor")(var))
        elif d in ("user_text", "user_entity"):
            kind = 1 if d == "user_entity" else 0
            # Home Assistant device: max length 100 + profile number, so the link script can find the field of a
            # profile without knowing entity ids (they depend on the language). The name field also takes a
            # device tracker.
            var = await text.new_text(conf, max_length=100 + _user_of(e) if kind else 63)
            cg.add(var.set_user(hub, _user_of(e), kind))
            cg.add(hub.register_user_text(_user_of(e), kind, var))
        elif d == "user_device":
            var = await select.new_select(conf, options=[])
            cg.add(var.set_user(hub, _user_of(e), 0))
            cg.add(hub.register_user_select(_user_of(e), 0, var))
        elif d == "user_welcome":
            var = await select.new_select(conf, options=lang["options"]["welcome_light"])
            cg.add(var.set_user(hub, _user_of(e), 1))
            cg.add(hub.register_user_select(_user_of(e), 1, var))
            for i, code in enumerate(LANGUAGES):
                cg.add(var.set_translation(i, welcome_tables[code]))
        elif d == "user_flag":
            var = await binary_sensor.new_binary_sensor(conf)
            cg.add(hub.set_user_flag_sensor(_user_of(e), 1 if e.key.endswith("_active") else 0, var))
        elif d == "rssi":
            var = await sensor.new_sensor(conf)
            cg.add(hub.set_toilet_signal_sensor(var))
        elif d == "user_irk_set":
            var = await text.new_text(conf, max_length=64)
            cg.add(var.set_user(hub, _user_of(e), 2))
        elif d == "raw_text":
            var = await text.new_text(conf, max_length=64)
            cg.add(var.set_hub(hub))
        elif d == "user_irk":
            var = await text_sensor.new_text_sensor(conf)
            cg.add(hub.set_user_irk_sensor(_user_of(e), var))
        elif d == "user_rssi":
            var = await sensor.new_sensor(conf)
            cg.add(hub.set_user_rssi_sensor(_user_of(e), var))
        elif d == "threshold":
            var = await number.new_number(conf, min_value=-100, max_value=-30, step=1)
            cg.add(var.set_hub(hub))
            cg.add(hub.set_threshold_number(var))
        elif d == "button":
            var = await button.new_button(conf)
            cg.add(var.set_action(hub, e.action))
        elif d == "command":
            var = await button.new_button(conf)
            cg.add(var.set_command(hub, e.action))
        elif d == "status_value":
            var = await sensor.new_sensor(conf)
            cg.add(hub.add_status_value(e.setting[0], var))
        elif d == "status_enum":
            var = await text_sensor.new_text_sensor(conf)
            skey = e.setting[0]
            cg.add(hub.add_status_enum(e.setting[0], skey, var))
            if e.key == "last_error_text":
                cg.add(hub.set_status_enum_error_map(skey))
            for i, code in enumerate(LANGUAGES):
                opts = I18N[code]["options"][e.key]
                cg.add(hub.set_status_enum_texts(skey, i, _table(f"aquaclean_{e.key}_{code}", opts), len(opts)))
            for _k, _cap, _okey in OPTION_VARIANTS:
                if _k != e.key:
                    continue
                for i, code in enumerate(LANGUAGES):
                    opts = I18N[code]["options"][_okey]
                    cg.add(hub.add_status_enum_variant(skey, cg.RawExpression(f"{_caps([_cap])}ULL"), i,
                                                       _table(f"aquaclean_{_okey}_{code}", opts), len(opts)))
        elif d == "counter_value":
            var = await sensor.new_sensor(conf)
            cg.add(hub.add_counter_value(e.setting[0], e.setting[1], var))
        elif d == "hub_switch":
            var = await switch.new_switch(conf)
            cg.add(var.set_action(hub, e.action))
            cg.add(hub.register_switch(var))
        elif d == "event":
            var = await event.new_event(conf, event_types=EVENT_TYPES)
            cg.add(link.set_button_event(var))
        elif d == "light":
            out = cg.new_Pvariable(conf[CONF_OUTPUT_ID])
            await light.register_light(out, conf)
            cg.add(out.set_hub(hub))
            cg.add(hub.set_light(out))
        else:
            kind, sid, maximum, offset = e.setting[:4]
            profile = e.setting[4] if len(e.setting) > 4 else 0
            if d == "number":
                var = await number.new_number(conf, min_value=offset, max_value=maximum + offset, step=1)
                for _k, _cap, _max in RANGE_VARIANTS:
                    if e.key == _k or e.key.endswith("_" + _k):
                        cg.add(hub.add_number_variant(var, cg.RawExpression(f"{_caps([_cap])}ULL"), _max))
            elif d == "setting_switch":
                var = await switch.new_switch(conf)
            elif d == "setting_select":
                var = await select.new_select(conf, options=lang["options"][e.key])
                for i, code in enumerate(LANGUAGES):
                    opts = I18N[code]["options"][e.key]
                    cg.add(var.set_translation(i, _table(f"aquaclean_{e.key}_{code}", opts), len(opts)))
                for _k, _cap, _okey in OPTION_VARIANTS:
                    if _k != e.key or e.setting[0] == KIND_LOCAL:
                        continue
                    cg.add(hub.add_select_variant(var, cg.RawExpression(f"{_caps([_cap])}ULL")))
                    for i, code in enumerate(LANGUAGES):
                        opts = I18N[code]["options"][_okey]
                        cg.add(var.set_translation_alt(i, _table(f"aquaclean_{_okey}_{code}", opts), len(opts)))
                cg.add(hub.register_select(var))
            elif d == "setting_time":
                var = await datetime.new_datetime(conf)
            cg.add(var.configure_setting(hub, kind, sid, maximum, offset, profile))
            cg.add(hub.register_setting(var))

        created_vars[e.key] = var
        if e.key in MAINT_KEYS:
            cg.add(hub.add_maintenance_entity(var))

        # Entities of a feature or with a setting id are shown only when the toilet has them
        probe_kind, probe_id, probe_profile = -1, 0, 0
        if e.probe is not None:
            probe_kind, probe_id, probe_profile = e.probe
        elif e.setting is not None and len(e.setting) >= 4:
            probe_kind, probe_id = e.setting[0], e.setting[1]
            probe_profile = e.setting[4] if len(e.setting) > 4 else 0
        elif e.key == "days_until_descaling":
            probe_kind, probe_id = PROBE_COUNTER, 0x01
        if not e.caps and probe_kind < 0:
            continue
        if d == "light":
            var = await cg.get_variable(conf[CONF_ID])
        req = cg.RawExpression(f"{_caps(e.caps)}ULL")
        if e.setting is not None and len(e.setting) >= 4:
            cg.add(hub.add_gated_setting(var, var, req, probe_kind, probe_id, probe_profile))
        else:
            cg.add(hub.add_gated(var, req, probe_kind, probe_id, probe_profile))
        gated_keys.append(f"{e.key}:{','.join(e.caps)}")  # changed capabilities also redo the discovery

    # names of shower, Lady shower and dryer for the "only while seated" log line
    for i, n in enumerate([lang["buttons"]["shower"], lang["buttons"]["lady_shower"], lang["names"]["dryer"]]):
        cg.add(hub.set_seated_name(i, n))

    if CONF_HOMEKIT in config:
        await _homekit_to_code(hub, config, lang, created_vars)

    # Stored discovery results belong to this list of entities
    if len(gated_keys) > 384:  # MAX_GATED in hub.h
        raise cv.Invalid(f"too many entities for the feature discovery ({len(gated_keys)} > 384)")
    cg.add(hub.set_layout(zlib.crc32(" ".join(gated_keys).encode()) & 0xFFFFFFFF))
