// --- shared helpers (the same in Decode and Encode) ---
const T = global.get("aquaclean_tables");
const C = flow.get("aquaclean_config");
const LEARN = flow.get("aquaclean_learn") || { byKey: {}, disc: {}, lang: null };

// language of labels and messages
function uiLang() {
    return C.language === "auto" ? (LEARN.lang || "en") : C.language;
}
// a text of the flow itself, in the chosen language
function t(id, vars) {
    const table = T.ui[uiLang()] || T.ui.en;
    let s = table[T.uiKeys.indexOf(id)] || T.ui.en[T.uiKeys.indexOf(id)];
    for (const [k, v] of Object.entries(vars || {})) s = s.split(`{${k}}`).join(String(v));
    return s;
}
// FNV-1a of "<component>/<object id>": the key of the topic table (same hash as tools/generate.py)
function topicHash(s) {
    let h = 0x811c9dc5;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
    return h.toString(36);
}
function keyOf(component, objectId) {
    const i = T.topics[topicHash(`${component}/${objectId}`)];
    return i === undefined ? null : T.keys[i];
}
// option lists of an entity in one language (with fallback for languages without a list)
function optionList(group, lang) {
    const g = T.options[group];
    return g && (g[lang] || g[T.fallback[lang]] || g.en);
}
// index of an option label in any language; remembers the bridge language when it is unambiguous
function optionIndex(e, label) {
    const groups = e.o || [];
    const order = LEARN.lang ? [LEARN.lang] : [];
    if (label === "") return null;
    for (const g of groups) {
        // bridge languages first (they can be the bridge's own), then the extra Node-RED languages
        for (const lang of order.concat(T.languages, Object.keys(T.options[g]))) {
            const list = T.options[g][lang];
            const i = list ? list.indexOf(label) : -1;
            if (i >= 0) {
                if (!LEARN.lang) {
                    const langs = T.languages.filter(l => T.options[g][l] && T.options[g][l].indexOf(label) === i);
                    if (langs.length === 1) LEARN.lang = langs[0];
                }
                return { index: i, group: g };
            }
        }
    }
    return null;
}
