'use strict';

const Homey = require('homey');
const Bridge = require('../../lib/esphome');
const { match, capabilities } = require('../../lib/entities.json');

class AquaCleanDevice extends Homey.Device {

  async onInit() {
    this.setUnavailable(this.homey.__('device.connecting')).catch(this.error);
    this.start();
  }

  start() {
    const s = this.getSettings();
    const bridge = new Bridge({
      host: s.host,
      port: Number(s.port) || 6053,
      key: s.encryption_key,
      match,
      log: (...a) => this.log(...a),
    });
    bridge.on('ready', () => {
      this.setAvailable().catch(this.error);
      const info = bridge.deviceInfo || {};
      this.setSettings({
        firmware: `${info.projectName || ''} ${info.projectVersion || ''} (ESPHome ${info.esphomeVersion || '?'})`.trim(),
        entities: `${bridge.byBridgeKey.size} / ${bridge.entities.size}`,
      }).catch(this.error);
      for (const [key, cap] of Object.entries(capabilities)) {
        const ent = bridge.byBridgeKey.get(key);
        if (ent && typeof ent.state === 'boolean') this.setCapabilityValue(cap, ent.state).catch(this.error);
      }
    });
    bridge.on('unavailable', () => {
      this.setUnavailable(this.homey.__('device.disconnected')).catch(this.error);
    });
    bridge.on('state', (ent, value, prev) => {
      const cap = capabilities[ent.bridgeKey];
      if (cap && typeof value === 'boolean' && this.hasCapability(cap)) {
        this.setCapabilityValue(cap, value).catch(this.error);
      }
      this.homey.app.flows.onState(this, ent, value, prev);
    });
    bridge.on('event', (ent, eventType) => this.homey.app.flows.onEvent(this, ent, eventType));
    bridge.on('error', (err) => this.log('bridge:', err.message));
    this.bridge = bridge;
    try {
      bridge.connect();
    } catch (err) {
      this.error(err);
    }
  }

  stop() {
    if (this.bridge) this.bridge.disconnect();
    this.bridge = null;
  }

  async onSettings({ changedKeys }) {
    if (changedKeys.some((k) => ['host', 'port', 'encryption_key'].includes(k))) {
      this.stop();
      // start with the new settings once Homey has stored them
      this.homey.setTimeout(() => this.start(), 500);
    }
  }

  // mDNS: follow the bridge when its IP address changes
  onDiscoveryResult(result) {
    return result.id === this.getData().id;
  }

  async onDiscoveryAvailable(result) {
    await this._followAddress(result);
  }

  onDiscoveryAddressChanged(result) {
    this._followAddress(result).catch(this.error);
  }

  async _followAddress(result) {
    if (!result.address || result.address === this.getSetting('host')) return;
    if (!/^[\d.]+$/.test(this.getSetting('host'))) return; // a host name: leave it to DNS
    this.log('bridge moved to', result.address);
    await this.setSettings({ host: result.address });
    this.stop();
    this.start();
  }

  async onDeleted() {
    this.stop();
  }

  async onUninit() {
    this.stop();
  }

}

module.exports = AquaCleanDevice;
