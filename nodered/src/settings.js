// Apply settings: reads the "Settings" inject, keeps them in flow context and (re)subscribes the MQTT in node.
// Change the settings in the inject node "Settings", not here.
const defaults = {
    topicPrefix: "aquaclean",        // the bridge's MQTT topic prefix (field "MQTT topic prefix" on its web page)
    discoveryPrefix: "homeassistant", // the discovery prefix (the shared convention; rarely changed)
    language: "auto",                // language of labels and messages: "auto" or a code such as "en", "nl", "ja"
    allowSharedWrites: false,        // see the README: shared MQTT topics of the user profiles
    onlyChanges: true,               // send a state only when its value changed
    contextName: "aquaclean"         // name of the global context object with every state
};
const cfg = Object.assign({}, defaults, (typeof msg.payload === "object" && msg.payload) || {});
cfg.topicPrefix = String(cfg.topicPrefix).replace(/^\/+|\/+$/g, "");
cfg.discoveryPrefix = String(cfg.discoveryPrefix).replace(/^\/+|\/+$/g, "");
const T = global.get("aquaclean_tables");
if (cfg.language !== "auto" && T && !T.ui[cfg.language]) {
    node.warn(`language "${cfg.language}" is not available, using "auto". Available: ${Object.keys(T.ui).join(", ")}`);
    cfg.language = "auto";
}
flow.set("aquaclean_config", cfg);
// start clean: learned topics and the state of the previous deploy
flow.set("aquaclean_learn", { byKey: {}, disc: {}, lang: null });
global.set(cfg.contextName, { online: null, state: {}, label: {}, updated: {} });
node.status({ fill: "blue", shape: "dot", text: `${cfg.topicPrefix}/# · ${cfg.language}` });
return [[
    { action: "unsubscribe", topic: true },
    { action: "subscribe", topic: [
        { topic: `${cfg.topicPrefix}/#`, qos: 0 },
        { topic: `${cfg.discoveryPrefix}/+/+/+/config`, qos: 0 }
    ] }
]];
