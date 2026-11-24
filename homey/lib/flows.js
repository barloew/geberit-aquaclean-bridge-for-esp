'use strict';

/*
 * Registers the run listeners of every generated flow card (see tools/generate.py) and fires the triggers.
 * The card set comes from lib/entities.json, so the listeners always match the cards in app.json.
 */

const { items } = require('./entities.json');

const TRIGGERS = new Set(['on', 'off', 'changed', 'any', 'one']);
const CONDITIONS = new Set(['is_on', 'above', 'equals']);

// bridge entity key -> { item, profile }
const BY_KEY = new Map();
for (const item of items) {
  if (item.key) BY_KEY.set(item.key, { item, profile: null });
  for (const [p, key] of Object.entries(item.profiles || {})) BY_KEY.set(key, { item, profile: p });
}

function keyOf(item, args) {
  if (!item.profiles) return item.key;
  const key = item.profiles[String(args.profile)];
  if (!key) throw new Error(`profile ${args.profile}?`);
  return key;
}

function bridgeOf(args) {
  const bridge = args.device && args.device.bridge;
  if (!bridge) throw new Error('device not ready');
  return bridge;
}

function stateOf(args, item) {
  const ent = bridgeOf(args).byBridgeKey.get(keyOf(item, args));
  return ent ? ent.state : undefined;
}

const argValue = (v) => (v && typeof v === 'object' ? (v.id !== undefined ? v.id : v.name) : v);

function formatTimestamp(v) {
  if (typeof v !== 'number' || !Number.isFinite(v) || v <= 0) return '';
  return new Date(v * 1000).toISOString().slice(0, 10);
}

class Flows {

  constructor(homey) {
    this.homey = homey;
    this.triggers = new Map();
    this._register();
  }

  _register() {
    const { flow } = this.homey;
    for (const item of items) {
      for (const kind of item.cards) {
        const id = `${item.id}_${kind}`;
        if (TRIGGERS.has(kind)) {
          const card = flow.getDeviceTriggerCard(id);
          if (kind === 'one') {
            card.registerRunListener(async (args, state) => args.button === state.eventType);
          } else if (item.profiles) {
            card.registerRunListener(async (args, state) => String(args.profile) === String(state.profile));
          }
          this.triggers.set(id, card);
        } else if (CONDITIONS.has(kind)) {
          const card = flow.getConditionCard(id);
          card.registerRunListener(async (args) => this._condition(item, kind, args));
          if (kind === 'equals' && item.platform === 'select') this._autocomplete(card, item);
        } else {
          const card = flow.getActionCard(id);
          card.registerRunListener(async (args) => this._action(item, kind, args));
          if (kind === 'set' && item.platform === 'select') this._autocomplete(card, item);
        }
      }
    }
  }

  _autocomplete(card, item) {
    card.registerArgumentAutocompleteListener('value', async (query, args) => {
      const bridge = args.device && args.device.bridge;
      let key;
      try {
        key = keyOf(item, args);
      } catch (err) {
        key = Object.values(item.profiles || {})[0];
      }
      const q = String(query || '').toLowerCase();
      return (bridge ? bridge.options(key) : [])
        .filter((o) => o.toLowerCase().includes(q))
        .map((o) => ({ name: o, id: o }));
    });
  }

  _condition(item, kind, args) {
    const state = stateOf(args, item);
    if (state === undefined || state === null) throw new Error('no value yet');
    if (kind === 'is_on') return state === true;
    if (kind === 'above') return Number(state) > Number(args.value);
    return String(state).trim().toLowerCase() === String(argValue(args.value)).trim().toLowerCase();
  }

  _action(item, kind, args) {
    const bridge = bridgeOf(args);
    const key = keyOf(item, args);
    switch (kind) {
      case 'press':
        return bridge.press(key);
      case 'turn_on':
        return bridge.setSwitch(key, true);
      case 'turn_off':
        return bridge.setSwitch(key, false);
      case 'toggle':
        return bridge.setSwitch(key, !(stateOf(args, item) === true));
      case 'set':
        if (item.platform === 'number') return bridge.setNumber(key, args.value);
        if (item.platform === 'select') return bridge.setSelect(key, argValue(args.value));
        if (item.platform === 'time') return bridge.setTime(key, args.value);
        return bridge.setText(key, args.value);
      default:
        throw new Error(`unknown action ${kind}`);
    }
  }

  /** Called by a device for every state change of a recognised entity. */
  onState(device, ent, value, prev) {
    const hit = BY_KEY.get(ent.bridgeKey);
    if (!hit) return;
    // the first value after (re)connecting is not a change
    if (prev === undefined || prev === null || value === null) return;
    const { item, profile } = hit;
    const state = { profile };
    const fire = (kind, tokens = {}) => {
      if (!item.cards.includes(kind)) return;
      this.triggers.get(`${item.id}_${kind}`).trigger(device, tokens, state)
        .catch((err) => device.error(`trigger ${item.id}_${kind}:`, err.message));
    };
    if (typeof value === 'boolean') {
      fire(value ? 'on' : 'off');
      return;
    }
    if (item.timestamp) {
      fire('changed', { value: formatTimestamp(value) });
    } else if (item.platform === 'sensor' || item.platform === 'number') {
      fire('changed', { value: Number(value) });
    } else {
      fire('changed', { value: String(value) });
    }
  }

  /** Called by a device for every remote control button event. */
  onEvent(device, ent, eventType) {
    const hit = BY_KEY.get(ent.bridgeKey);
    if (!hit || eventType === 'unknown') return;
    const { item } = hit;
    const lang = this.homey.i18n.getLanguage();
    const label = (item.labels[eventType] || {})[lang] || (item.labels[eventType] || {}).en || eventType;
    const tokens = { button: label, long: eventType.endsWith('_long') };
    this.triggers.get(`${item.id}_any`).trigger(device, tokens, { eventType })
      .catch((err) => device.error('trigger any:', err.message));
    this.triggers.get(`${item.id}_one`).trigger(device, {}, { eventType })
      .catch((err) => device.error('trigger one:', err.message));
  }

}

module.exports = Flows;
