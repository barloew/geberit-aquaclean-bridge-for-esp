// Decode: turns the bridge's MQTT messages into one message per entity.
//   msg.topic   = the entity key, the same in every language (e.g. "shower_active", "water_temperature")
//   msg.payload = the value: true/false, a number, a text, or for selections the option number (0, 1, 2, ...)
//   msg.aquaclean = { key, group, kind, name, label, unit, known, mqtt, raw }
// Output 1: every state   Output 2: remote control buttons   Output 3: connection and alarms
if (!T || !C) return null;
const P = C.topicPrefix, D = C.discoveryPrefix;
const S = global.get(C.contextName) || { online: null, state: {}, label: {}, updated: {} };
const raw = msg.payload === undefined || msg.payload === null ? "" : String(msg.payload);
const ALARMS = ["connected", "toilet_problem", "bridge_problem", "bridge_health"];

function status() {
    if (S.online === false) node.status({ fill: "red", shape: "ring", text: t("st_offline") });
    else if (S.online === true) node.status({ fill: "green", shape: "dot", text: t("st_online", { n: Object.keys(S.state).length }) });
    else node.status({ fill: "yellow", shape: "ring", text: t("st_wait") });
}
function save() { flow.set("aquaclean_learn", LEARN); global.set(C.contextName, S); }

// 1. availability: <prefix>/status is "online" or "offline"
if (msg.topic === `${P}/status`) {
    S.online = raw === "online";
    save(); status();
    return [null, null, { topic: "online", payload: S.online, aquaclean: { key: "online", group: "bridge", mqtt: msg.topic, raw } }];
}

// 2. discovery: <discovery prefix>/<component>/<node>/<object id>/config, only the bridge's own entities
if (msg.topic.startsWith(`${D}/`) && msg.topic.endsWith("/config")) {
    const parts = msg.topic.split("/");
    if (parts.length !== 5 || raw === "") return null;
    let cfg;
    try { cfg = JSON.parse(raw); } catch (err) { return null; }
    const st = cfg.state_topic || cfg["~"] || cfg.command_topic || "";
    if (!String(st).startsWith(`${P}/`)) return null;
    const base = `${P}/${parts[1]}/${parts[3]}`;
    LEARN.disc[base] = {
        name: cfg.name, options: cfg.options, min: cfg.min, max: cfg.max, step: cfg.step,
        unit: cfg.unit_of_measurement, device: cfg.device && cfg.device.name
    };
    const key = keyOf(parts[1], parts[3]);
    if (key) LEARN.byKey[key] = base;
    save();
    return null;
}

// 3. states: <prefix>/<component>/<object id>/state
if (!msg.topic.startsWith(`${P}/`) || !msg.topic.endsWith("/state")) return null;
const rest = msg.topic.slice(P.length + 1).split("/");
if (rest.length !== 3) return null;
const [component, objectId] = rest;
const base = `${P}/${component}/${objectId}`;
const key = keyOf(component, objectId);
const e = key ? T.e[key] : null;
const disc = LEARN.disc[base] || {};
if (key) LEARN.byKey[key] = base;

let value = raw, label, index, json = null;
try { if (raw.startsWith("{")) json = JSON.parse(raw); } catch (err) { json = null; }
switch (component) {
    case "binary_sensor":
    case "switch":
        value = raw === "ON" ? true : raw === "OFF" ? false : null;
        break;
    case "light":
        value = json && json.state ? json.state === "ON" : null;
        break;
    case "sensor":
    case "number": {
        const n = parseFloat(raw);
        value = Number.isFinite(n) ? n : null;
        if (value !== null && e && e.ts) value = value * 1000;  // timestamp in seconds -> ms (JavaScript time)
        break;
    }
    case "time":
        value = json ? `${String(json.hour).padStart(2, "0")}:${String(json.minute).padStart(2, "0")}` : raw;
        break;
    case "event":
        value = json ? json.event_type : raw;
        break;
    default:  // text_sensor, select, text
        value = raw;
}
if (e && e.ts === "iso" && value) value = Date.parse(value) || null;  // moment of connecting -> ms
if (e && e.o && typeof value === "string") {  // selections and status texts -> option number
    const hit = optionIndex(e, value, true);
    if (hit) {
        index = hit.index;
        label = (optionList(hit.group, uiLang()) || [])[index] || value;
        value = index;
    }
} else if (component === "select") {
    label = value;  // e.g. the time zone: no fixed list, the text itself
}
if (component === "event" && e) {
    const btn = String(value).replace(/_long$/, "");
    const lang = T.buttons[uiLang()] ? uiLang() : (T.fallback[uiLang()] || "en");
    const b = T.buttons[lang];
    label = (b.names[btn] || btn) + (String(value).endsWith("_long") ? ` (${b.states[1]})` : "");
}

const id = key || `unknown/${component}/${objectId}`;
const changed = JSON.stringify(S.state[id]) !== JSON.stringify(value);
S.state[id] = value;
if (label !== undefined) S.label[id] = label;
S.updated[id] = Date.now();
if (S.online === null) S.online = true;  // states arrive: the bridge is there
save(); status();
if (C.onlyChanges && !changed && component !== "event") return null;

const out = {
    topic: id,
    payload: value,
    aquaclean: {
        key: id, known: !!e, group: e ? e.g : null, kind: component,
        name: disc.name || null, label, unit: disc.unit || (e && e.u) || null,
        mqtt: msg.topic, raw
    }
};
if (component === "event") return [out, out, null];
return [out, null, ALARMS.includes(id) ? out : null];
