'use strict';

const Homey = require('homey');
const Bridge = require('../../lib/esphome');
const { match } = require('../../lib/entities.json');

const PROJECT = 'geberit.aquaclean';

function looksLikeBridge(r) {
  const txt = r.txt || {};
  return txt.project_name === PROJECT || /aquaclean/i.test(`${r.host || ''} ${r.name || ''} ${txt.friendly_name || ''}`);
}

class AquaCleanDriver extends Homey.Driver {

  async onPair(session) {
    session.setHandler('discover', async () => {
      const results = Object.values(this.getDiscoveryStrategy().getDiscoveryResults());
      return results
        .map((r) => ({
          id: r.id,
          address: r.address,
          port: r.port || 6053,
          name: (r.txt && r.txt.friendly_name) || r.name || r.host || r.address,
          bridge: looksLikeBridge(r),
        }))
        .sort((a, b) => Number(b.bridge) - Number(a.bridge));
    });

    session.setHandler('connect', async ({ host, port, key }) => {
      const h = String(host || '').trim();
      const k = String(key || '').trim();
      const p = Number(port) || 6053;
      if (!h) throw new Error(this.homey.__('pair.error_host'));
      let res;
      try {
        res = await Bridge.probe({
          host: h, port: p, key: k, match,
        });
      } catch (err) {
        if (err.message === 'encryption') throw new Error(this.homey.__('pair.error_key'));
        throw new Error(this.homey.__('pair.error_connect', { error: err.message }));
      }
      if (res.recognised === 0) throw new Error(this.homey.__('pair.error_not_bridge'));
      const mac = String(res.info.macAddress || h).toLowerCase().replace(/[^0-9a-f]/g, '');
      return {
        name: res.info.friendlyName || res.info.name || 'AquaClean Bridge',
        data: { id: mac },
        settings: { host: h, port: p, encryption_key: k },
      };
    });
  }

}

module.exports = AquaCleanDriver;
