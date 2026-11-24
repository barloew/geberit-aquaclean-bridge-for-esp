'use strict';

const Homey = require('homey');
const Flows = require('./lib/flows');

class AquaCleanApp extends Homey.App {

  async onInit() {
    this.flows = new Flows(this.homey);
    this.log('AquaClean Bridge app ready');
  }

}

module.exports = AquaCleanApp;
