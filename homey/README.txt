Control your Geberit AquaClean shower toilet from Homey Flows through the AquaClean Bridge for ESP.

The bridge (an ESP32-S3 next to the toilet, https://github.com/barloew/geberit-aquaclean-bridge-for-esp) keeps one local Bluetooth connection to the toilet. This app talks to the bridge over the ESPHome native API on your own network: no cloud.

Every standard entity of the bridge gets its own flow cards: start the shower, change spray intensity or water temperature, activate a user profile, react when a user is recognised or the shower starts, watch the descaling countdown and toilet faults, use the remote control buttons as triggers, and more. Cards for user profiles have a profile picker. Cards only work for functions your toilet model really has; the bridge detects them itself.

Pairing: flash the bridge with the native API route, save an API encryption key on its /setup page, then add the AquaClean Bridge in Homey and paste that key.

Community app. Not affiliated with or endorsed by Geberit. Geberit and AquaClean are trademarks of their owners.
