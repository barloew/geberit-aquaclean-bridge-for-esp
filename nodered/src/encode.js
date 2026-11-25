// Encode: a command for the bridge -> the right MQTT topic and payload.
//   msg.topic   = the entity key (e.g. "shower_toggle", "orientation_light", "light_color")
//   msg.payload = the new value:
//                 buttons: anything            switches, light: true / false / "toggle"
//                 numbers: a number            selections: option number (0, 1, ...) or its text in any language
//                 times: "HH:MM"               texts: a text
// Errors go to node.error, so a Catch node can handle them.
if (!T || !C) return null;
const key = String(msg.topic || "");
const e = T.e[key];
function fail(id, vars) {
    const text = t(id, Object.assign({ key }, vars || {}));
    node.status({ fill: "red", shape: "dot", text });
    node.error(text, msg);
    return null;
}
if (!e) return fail("err_unknown");
if (!e.w) return fail("err_readonly");
const base = LEARN.byKey[key];
if (!base) return fail("err_unseen");
const disc = LEARN.disc[base] || {};
const S = global.get(C.contextName) || { state: {} };
const v = msg.payload;
const kind = base.split("/").slice(-2)[0];

function onOff(x) {
    if (typeof x === "string") {
        const s = x.trim().toLowerCase();
        if (s === "toggle") return "TOGGLE";
        if (["on", "true", "1"].includes(s)) return "ON";
        if (["off", "false", "0"].includes(s)) return "OFF";
        return null;
    }
    if (typeof x === "boolean" || typeof x === "number") return x ? "ON" : "OFF";
    return null;
}

let payload;
switch (kind) {
    case "button":
        payload = "PRESS";
        break;
    case "switch":
        payload = onOff(v);
        if (!payload) return fail("err_value", { value: JSON.stringify(v) });
        break;
    case "light": {
        if (v && typeof v === "object") { payload = JSON.stringify(v); break; }
        let s = onOff(v);
        if (!s) return fail("err_value", { value: JSON.stringify(v) });
        if (s === "TOGGLE") s = S.state[key] ? "OFF" : "ON";
        payload = JSON.stringify({ state: s });
        break;
    }
    case "number": {
        const n = Number(v);
        const min = disc.min !== undefined ? disc.min : e.min;
        const max = disc.max !== undefined ? disc.max : e.max;
        if (v === "" || v === null || !Number.isFinite(n)) return fail("err_value", { value: JSON.stringify(v) });
        if ((min !== undefined && n < min) || (max !== undefined && n > max)) return fail("err_range", { min, max });
        payload = String(n);
        break;
    }
    case "select": {
        // the bridge expects the option text in its own language: take it from discovery, else from the table
        const current = disc.options || (e.o ? optionList(e.o[0], LEARN.lang || "en") : null);
        let index = null;
        if (typeof v === "number" || (typeof v === "string" && /^\d+$/.test(v.trim()))) index = Number(v);
        else if (typeof v === "string") {
            const hit = e.o ? optionIndex(e, v) : null;
            if (hit) index = hit.index;
            else if (current && current.includes(v)) { payload = v; break; }
        }
        if (index === null || !current || !current[index]) return fail("err_option", { value: JSON.stringify(v) });
        payload = current[index];
        break;
    }
    case "time": {
        let h, m, s = 0;
        if (v && typeof v === "object") { h = v.hour; m = v.minute; s = v.second || 0; }
        else {
            const r = /^(\d{1,2}):(\d{2})(?::(\d{2}))?$/.exec(String(v).trim());
            if (r) { h = +r[1]; m = +r[2]; s = +(r[3] || 0); }
        }
        if (!(h >= 0 && h < 24 && m >= 0 && m < 60)) return fail("err_value", { value: JSON.stringify(v) });
        payload = JSON.stringify({ hour: h, minute: m, second: s });
        break;
    }
    default:  // text
        if (v === undefined || v === null || typeof v === "object") return fail("err_value", { value: JSON.stringify(v) });
        payload = String(v);
}
node.status({ fill: "green", shape: "dot", text: t("st_sent", { key }) });
return { topic: `${base}/command`, payload, qos: 0, retain: false };
