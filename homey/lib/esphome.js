'use strict';

/*
 * ESPHome native API link to the AquaClean bridge.
 *
 * Uses the transport (Noise encryption, framing, ping, time) of @2colors/esphome-native-api, but does entity
 * listing, state handling and commands itself: the library's entity layer does not know event and time
 * entities, and it drops the sub-device id the bridge relies on (entities with the same name live on
 * different sub-devices, e.g. "Spray intensity" in every user profile).
 */

const EventEmitter = require('events');
const fs = require('fs');
const path = require('path');
const { Connection } = require('@2colors/esphome-native-api');
const messages = require('@2colors/esphome-native-api/lib/utils/messages');

const { pb } = messages;

// The library only maps the message ids it knows; an unknown id (e.g. an event) closes the connection. Register
// every message of its own api.proto that the generated protobuf code has.
(function registerAllMessages() {
  const proto = fs.readFileSync(
    path.join(path.dirname(require.resolve('@2colors/esphome-native-api/lib/protoc/api_pb')), 'api.proto'),
    'utf8',
  );
  const rx = /message\s+(\w+)\s*\{[^}]*?option\s*\(id\)\s*=\s*(\d+)\s*;/g;
  let m;
  while ((m = rx.exec(proto)) !== null) {
    const [, type, id] = m;
    if (!pb[type] || messages.id_to_type[id]) continue;
    messages.id_to_type[id] = type;
    messages.type_to_id[type] = id;
    pb[type].type = type;
    pb[type].id = id;
  }
}());

const LIST_PREFIX = 'ListEntities';
const PLATFORMS = {
  BinarySensor: 'binary_sensor',
  Sensor: 'sensor',
  TextSensor: 'text_sensor',
  Switch: 'switch',
  Number: 'number',
  Select: 'select',
  Text: 'text',
  Button: 'button',
  Light: 'light',
  Time: 'time',
  Event: 'event',
};
const STATE_MESSAGES = {
  BinarySensorStateResponse: 'binary_sensor',
  SensorStateResponse: 'sensor',
  TextSensorStateResponse: 'text_sensor',
  SwitchStateResponse: 'switch',
  NumberStateResponse: 'number',
  SelectStateResponse: 'select',
  TextStateResponse: 'text',
  LightStateResponse: 'light',
  TimeStateResponse: 'time',
  EventResponse: 'event',
};

const pad = (n) => String(n).padStart(2, '0');

class Bridge extends EventEmitter {

  /**
   * @param {object} opts host, port, key (base64 API encryption key), match (name table from entities.json),
   *   log (function)
   */
  constructor({
    host, port = 6053, key, match, log = () => {},
  }) {
    super();
    this.host = host;
    this.port = port;
    this.key = key;
    this.match = match;
    this.log = log;
    this.entities = new Map(); // "<deviceId>:<key>" -> entity
    this.byBridgeKey = new Map(); // bridge entity key (e.g. "shower_active") -> entity
    this.deviceInfo = null;
    this.ready = false;
  }

  connect() {
    this.conn = new Connection({
      host: this.host,
      port: this.port,
      encryptionKey: this.key || '',
      clientInfo: 'Homey AquaClean Bridge',
      reconnect: true,
      reconnectInterval: 15000,
    });
    this.conn.on('authorized', () => this._onAuthorized().catch((err) => {
      this.log('initialisation failed:', err.message);
      this.emit('error', err);
    }));
    this.conn.on('unauthorized', () => this._setReady(false));
    this.conn.on('disconnected', () => this._setReady(false));
    this.conn.on('error', (err) => this.emit('error', err));
    this.conn.on('message', (type, msg) => this._onMessage(type, msg));
    this.conn.connect();
  }

  disconnect() {
    this._setReady(false);
    if (this.conn) {
      try {
        this.conn.disconnect();
      } catch (err) {
        // already gone
      }
      this.conn = null;
    }
  }

  _setReady(ready) {
    if (this.ready === ready) return;
    this.ready = ready;
    this.emit(ready ? 'ready' : 'unavailable');
  }

  async _onAuthorized() {
    this.deviceInfo = await this.conn.deviceInfoService();
    const devices = new Map((this.deviceInfo.devicesList || []).map((d) => [d.deviceId, d.name]));
    const list = await this._listEntities();
    this.entities.clear();
    this.byBridgeKey.clear();
    for (const { platform, cfg } of list) {
      const deviceId = cfg.deviceId || 0;
      const devName = deviceId ? (devices.get(deviceId) || '') : '';
      const bridgeKey = this.match[`${devName}\u0001${cfg.name}`] || null;
      const ent = {
        platform, key: cfg.key, deviceId, name: cfg.name, cfg, bridgeKey, state: undefined,
      };
      this.entities.set(`${deviceId}:${cfg.key}`, ent);
      if (bridgeKey) this.byBridgeKey.set(bridgeKey, ent);
    }
    const unknown = [...this.entities.values()].filter((e) => !e.bridgeKey).length;
    this.log(`${this.entities.size} entities, ${this.byBridgeKey.size} recognised, ${unknown} not used`);
    this.conn.subscribeStatesService();
    this._setReady(true);
  }

  async _listEntities() {
    const out = [];
    const onMessage = (type, msg) => {
      if (!type.startsWith(LIST_PREFIX) || type === 'ListEntitiesDoneResponse') return;
      const platform = PLATFORMS[type.slice(LIST_PREFIX.length, -'Response'.length)];
      if (platform) out.push({ platform, cfg: msg });
    };
    this.conn.on('message', onMessage);
    try {
      await this.conn.sendMessageAwaitResponse(new pb.ListEntitiesRequest(), 'ListEntitiesDoneResponse', 20);
    } finally {
      this.conn.off('message', onMessage);
    }
    return out;
  }

  _onMessage(type, msg) {
    const platform = STATE_MESSAGES[type];
    if (!platform) return;
    const ent = this.entities.get(`${msg.deviceId || 0}:${msg.key}`);
    if (!ent || !ent.bridgeKey) return;
    let value;
    if (platform === 'event') {
      this.emit('event', ent, msg.eventType);
      return;
    }
    if (msg.missingState) {
      value = null;
    } else if (platform === 'time') {
      value = `${pad(msg.hour)}:${pad(msg.minute)}`;
    } else {
      value = msg.state;
    }
    const prev = ent.state;
    ent.state = value;
    if (prev !== value) this.emit('state', ent, value, prev);
  }

  // ------------------------------------------------------------------------------------------- commands

  entity(bridgeKey) {
    const ent = this.byBridgeKey.get(bridgeKey);
    if (!ent) throw new Error(`not available on this toilet: ${bridgeKey}`);
    if (!this.ready) throw new Error('bridge not connected');
    return ent;
  }

  _send(Msg, ent, set) {
    const m = new Msg();
    m.setKey(ent.key);
    if (ent.deviceId && m.setDeviceId) m.setDeviceId(ent.deviceId);
    set(m);
    this.conn.sendCommandMessage(m);
  }

  press(bridgeKey) {
    const ent = this.entity(bridgeKey);
    this._send(pb.ButtonCommandRequest, ent, () => {});
  }

  setSwitch(bridgeKey, on) {
    const ent = this.entity(bridgeKey);
    if (ent.platform === 'light') {
      this._send(pb.LightCommandRequest, ent, (m) => {
        m.setHasState(true);
        m.setState(on);
      });
    } else {
      this._send(pb.SwitchCommandRequest, ent, (m) => m.setState(on));
    }
  }

  setNumber(bridgeKey, value) {
    const ent = this.entity(bridgeKey);
    const { minValue, maxValue } = ent.cfg;
    const v = Number(value);
    // the real range can be narrower than the card's (e.g. 3 spray levels on some models)
    if (!Number.isFinite(v) || v < minValue || v > maxValue) {
      throw new Error(`${ent.name}: ${minValue}–${maxValue}`);
    }
    this._send(pb.NumberCommandRequest, ent, (m) => m.setState(v));
  }

  setSelect(bridgeKey, option) {
    const ent = this.entity(bridgeKey);
    if (!(ent.cfg.optionsList || []).includes(option)) throw new Error(`${ent.name}: ${option}?`);
    this._send(pb.SelectCommandRequest, ent, (m) => m.setState(option));
  }

  setText(bridgeKey, text) {
    const ent = this.entity(bridgeKey);
    this._send(pb.TextCommandRequest, ent, (m) => m.setState(String(text)));
  }

  setTime(bridgeKey, hhmm) {
    const ent = this.entity(bridgeKey);
    const m = /^(\d{1,2}):(\d{2})$/.exec(String(hhmm).trim());
    if (!m || Number(m[1]) > 23 || Number(m[2]) > 59) throw new Error(`${ent.name}: HH:MM`);
    this._send(pb.TimeCommandRequest, ent, (msg) => {
      msg.setHour(Number(m[1]));
      msg.setMinute(Number(m[2]));
      msg.setSecond(0);
    });
  }

  options(bridgeKey) {
    const ent = this.byBridgeKey.get(bridgeKey);
    return ent ? (ent.cfg.optionsList || []) : [];
  }

}

/** One-shot connection test for pairing: resolves with device info and the number of recognised entities. */
Bridge.probe = ({
  host, port, key, match, timeout = 20000,
}) => new Promise((resolve, reject) => {
  const b = new Bridge({
    host, port, key, match,
  });
  const done = (err, res) => {
    clearTimeout(timer);
    b.disconnect();
    if (err) reject(err); else resolve(res);
  };
  const timer = setTimeout(() => done(new Error('timeout')), timeout);
  b.on('ready', () => done(null, { info: b.deviceInfo, recognised: b.byBridgeKey.size, total: b.entities.size }));
  b.on('error', (err) => {
    if (/handshake|decrypt|encryption|noise|key/i.test(err.message)) done(new Error('encryption'));
  });
  try {
    b.connect();
  } catch (err) {
    done(err);
  }
});

module.exports = Bridge;
