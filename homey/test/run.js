'use strict';

/*
 * Integration test against a real ESPHome API server: an ESPHome *host* build of test/gen_host_yaml.py
 * (template entities with the bridge's Dutch names and sub-devices). Exercises transport, name matching,
 * sub-device ids, states, events and every action kind through the real flow-card listeners.
 *
 *   python3 test/gen_host_yaml.py .. > aquaclean-test.yaml   # then drop api.encryption
 *   esphome compile aquaclean-test.yaml && ./.esphome/build/.../program &
 *   AQ_HOST=127.0.0.1 AQ_PORT=16053 node test/run.js
 */

const assert = require('assert');
const Bridge = require('../lib/esphome');
const { match } = require('../lib/entities.json');
const Flows = require('../lib/flows');

const fired = [];
const listeners = {};
const card = (type, id) => ({
  registerRunListener(fn) { listeners[`${type}:${id}`] = fn; return this; },
  registerArgumentAutocompleteListener(arg, fn) { listeners[`auto:${type}:${id}`] = fn; return this; },
  async trigger(device, tokens, state) {
    const run = listeners[`trigger:${id}`];
    fired.push({ id, tokens, state });
    return run ? run : true;
  },
});
const homey = {
  flow: {
    getDeviceTriggerCard: (id) => card('trigger', id),
    getConditionCard: (id) => card('condition', id),
    getActionCard: (id) => card('action', id),
  },
  i18n: { getLanguage: () => 'nl' },
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const flows = new Flows(homey);
const bridge = new Bridge({
  host: process.env.AQ_HOST || '127.0.0.1',
  port: Number(process.env.AQ_PORT || 16053),
  key: process.env.AQ_KEY || '',
  match,
});
const device = { bridge, error: console.error };
bridge.on('state', (ent, v, p) => flows.onState(device, ent, v, p));
bridge.on('event', (ent, t) => flows.onEvent(device, ent, t));
bridge.on('error', (e) => console.error('bridge error', e.message));

const action = (id, args = {}) => listeners[`action:${id}`]({ device, ...args });
const condition = (id, args = {}) => listeners[`condition:${id}`]({ device, ...args });
const firedIds = () => fired.map((f) => f.id);

(async () => {
  bridge.connect();
  await new Promise((res, rej) => {
    bridge.once('ready', res);
    setTimeout(() => rej(new Error('not ready')), 10000);
  });
  await sleep(500); // initial states
  const keys = [...bridge.byBridgeKey.keys()].sort();
  console.log('recognised:', keys.join(', '));
  for (const k of ['connected', 'shower_active', 'shower_toggle', 'remote_button', 'spray_intensity',
    'profile_1_spray_intensity', 'profile_4_spray_intensity', 'light_color', 'profile_2_user_name',
    'light_window_1_start', 'profile_4_lady_oscillating_spray', 'days_until_descaling', 'model']) {
    assert(bridge.byBridgeKey.has(k), `not recognised: ${k}`);
  }
  assert.strictEqual(fired.length, 0, 'no triggers for initial states');

  // button -> binary sensor trigger + remote control event
  action('shower_toggle_press');
  await sleep(400);
  assert(firedIds().includes('shower_active_on'), 'shower_active_on');
  const any = fired.find((f) => f.id === 'remote_button_any');
  assert(any && any.tokens.long === true && /Douche|Lady|douche/i.test(any.tokens.button), JSON.stringify(any));
  const one = fired.find((f) => f.id === 'remote_button_one');
  assert(one && await listeners['trigger:remote_button_one']({ button: 'shower_long' }, one.state));
  assert.strictEqual(await condition('shower_active_is_on'), true);

  // same entity name on two sub-devices: only profile 4 may change
  await action('profile_spray_intensity_set', { profile: '4', value: 5 });
  await sleep(400);
  const ch = fired.find((f) => f.id === 'profile_spray_intensity_changed');
  assert(ch && ch.state.profile === '4' && ch.tokens.value === 5, JSON.stringify(ch));
  assert.strictEqual(bridge.byBridgeKey.get('profile_1_spray_intensity').state, 3);
  assert.strictEqual(await listeners['trigger:profile_spray_intensity_changed']({ profile: '4' }, ch.state), true);
  assert.strictEqual(await listeners['trigger:profile_spray_intensity_changed']({ profile: '1' }, ch.state), false);
  assert.strictEqual(await condition('profile_spray_intensity_above', { profile: '4', value: 4 }), true);
  await assert.rejects(action('spray_intensity_set', { value: 9 }), /1–5/);

  // select via autocomplete
  const opts = await listeners['auto:action:light_color_set']('', { device });
  assert(opts.length > 2, 'select options');
  await action('light_color_set', { value: opts[2] });
  await sleep(400);
  assert(fired.find((f) => f.id === 'light_color_changed' && f.tokens.value === opts[2].name));
  assert.strictEqual(await condition('light_color_equals', { value: opts[2] }), true);

  // time, text, switch
  await action('light_window_1_start_set', { value: '06:30' });
  await action('profile_user_name_set', { profile: '2', value: 'Bram' });
  await action('profile_lady_oscillating_spray_toggle', { profile: '4' });
  await sleep(400);
  assert(fired.find((f) => f.id === 'light_window_1_start_changed' && f.tokens.value === '06:30'));
  assert(fired.find((f) => f.id === 'profile_user_name_changed' && f.tokens.value === 'Bram' && f.state.profile === '2'));
  assert(fired.find((f) => f.id === 'profile_lady_oscillating_spray_on' && f.state.profile === '4'));
  assert.strictEqual(await condition('days_until_descaling_above', { value: 41 }), true);
  assert.strictEqual(await condition('model_equals', { value: 'aquaclean sela' }), true);

  console.log(`OK: ${fired.length} triggers fired:`, [...new Set(firedIds())].join(', '));
  bridge.disconnect();
  process.exit(0);
})().catch((err) => {
  console.error('FAIL', err);
  process.exit(1);
});
