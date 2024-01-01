#!/usr/bin/env python3
"""Generates the ready-made firmware of the web installer (MQTT route) and everything around it.

Writes:
  packages/mqtt_setup.yaml                 MQTT broker, port, user, password and time zone set at runtime
  firmware/aquaclean-<board>-<lang>.yaml   one firmware per board and language
  static/boards.json                       the boards for the install page
  .github/workflows/build.yml              the build matrix (board x language)

Run from the repository root with ESPHome's Python (it needs esphome and aioesphomeapi for the time zones):
  python tools/gen_firmware.py
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
os.chdir("/tmp")  # ESPHome has a component called "copy" that shadows the stdlib module from the repo root
from aioesphomeapi.posix_tz import DSTRuleType, parse_posix_tz  # noqa: E402
from esphome.components.time import _extract_tz_string, _load_tzdata  # noqa: E402

BOARDS = {
    "atoms3-lite": "M5Stack AtomS3 Lite",
    "atoms3r": "M5Stack AtomS3R",
    "xiao-esp32s3": "Seeed XIAO ESP32S3",
    "lilygo-tdongle-s3": "LilyGo T-Dongle-S3",
}

# Same order as LANGUAGES in components/geberit_aquaclean/__init__.py
LANGUAGES = ["en", "nl", "de", "fr", "it", "es", "pl", "cs", "hu", "da", "sv", "nb", "fi", "pt", "ar", "tr", "et",
             "hr", "lv", "lt", "ro", "ru", "sk", "sl", "sr", "uk", "zh", "ko", "he"]

# Languages without Latin letters keep English entity names on the MQTT route (topics are built from the name)
NON_LATIN = {"ar", "he", "ru", "uk", "zh", "ko"}

# broker, port, user, password, connect (button: store and restart), topic prefix, time zone
TEXTS = {
    "en": ("MQTT broker", "MQTT port", "MQTT user", "MQTT password", "Connect to MQTT broker", "MQTT topic prefix", "Time zone"),
    "nl": ("MQTT-broker", "MQTT-poort", "MQTT-gebruiker", "MQTT-wachtwoord", "Verbinden met MQTT-broker", "MQTT-topicprefix", "Tijdzone"),
    "de": ("MQTT-Broker", "MQTT-Port", "MQTT-Benutzer", "MQTT-Passwort", "Mit MQTT-Broker verbinden", "MQTT-Topic-Präfix", "Zeitzone"),
    "fr": ("Broker MQTT", "Port MQTT", "Utilisateur MQTT", "Mot de passe MQTT", "Se connecter au broker MQTT",
           "Préfixe de topic MQTT", "Fuseau horaire"),
    "it": ("Broker MQTT", "Porta MQTT", "Utente MQTT", "Password MQTT", "Connetti al broker MQTT", "Prefisso topic MQTT", "Fuso orario"),
    "es": ("Bróker MQTT", "Puerto MQTT", "Usuario MQTT", "Contraseña MQTT", "Conectar al bróker MQTT", "Prefijo de tema MQTT", "Zona horaria"),
    "pl": ("Broker MQTT", "Port MQTT", "Użytkownik MQTT", "Hasło MQTT", "Połącz z brokerem MQTT", "Prefiks tematu MQTT", "Strefa czasowa"),
    "cs": ("MQTT broker", "MQTT port", "Uživatel MQTT", "Heslo MQTT", "Připojit k MQTT brokeru", "Předpona tématu MQTT", "Časové pásmo"),
    "hu": ("MQTT bróker", "MQTT port", "MQTT felhasználó", "MQTT jelszó", "Csatlakozás az MQTT brókerhez", "MQTT téma-előtag", "Időzóna"),
    "da": ("MQTT-broker", "MQTT-port", "MQTT-bruger", "MQTT-adgangskode", "Forbind til MQTT-broker", "MQTT-emnepræfiks", "Tidszone"),
    "sv": ("MQTT-broker", "MQTT-port", "MQTT-användare", "MQTT-lösenord", "Anslut till MQTT-broker", "MQTT-ämnesprefix", "Tidszon"),
    "nb": ("MQTT-megler", "MQTT-port", "MQTT-bruker", "MQTT-passord", "Koble til MQTT-megler", "MQTT-emneprefiks", "Tidssone"),
    "fi": ("MQTT-välittäjä", "MQTT-portti", "MQTT-käyttäjä", "MQTT-salasana", "Yhdistä MQTT-välittäjään",
           "MQTT-aiheen etuliite", "Aikavyöhyke"),
    "pt": ("Broker MQTT", "Porta MQTT", "Utilizador MQTT", "Palavra-passe MQTT", "Ligar ao broker MQTT", "Prefixo de tópico MQTT", "Fuso horário"),
    "tr": ("MQTT aracısı", "MQTT bağlantı noktası", "MQTT kullanıcısı", "MQTT parolası", "MQTT aracısına bağlan",
           "MQTT konu öneki", "Saat dilimi"),
    "et": ("MQTT vahendaja", "MQTT port", "MQTT kasutaja", "MQTT parool", "Ühenda MQTT vahendajaga", "MQTT teema eesliide", "Ajavöönd"),
    "hr": ("MQTT posrednik", "MQTT priključak", "MQTT korisnik", "MQTT lozinka", "Poveži s MQTT posrednikom",
           "MQTT prefiks teme", "Vremenska zona"),
    "lv": ("MQTT starpnieks", "MQTT ports", "MQTT lietotājs", "MQTT parole", "Savienot ar MQTT starpnieku",
           "MQTT tēmas prefikss", "Laika josla"),
    "lt": ("MQTT tarpininkas", "MQTT prievadas", "MQTT naudotojas", "MQTT slaptažodis",
           "Prisijungti prie MQTT tarpininko", "MQTT temos prefiksas", "Laiko juosta"),
    "ro": ("Broker MQTT", "Port MQTT", "Utilizator MQTT", "Parolă MQTT", "Conectare la brokerul MQTT", "Prefix subiect MQTT", "Fus orar"),
    "sk": ("MQTT broker", "MQTT port", "Používateľ MQTT", "Heslo MQTT", "Pripojiť k MQTT brokeru", "Predpona témy MQTT", "Časové pásmo"),
    "sl": ("Posrednik MQTT", "Vrata MQTT", "Uporabnik MQTT", "Geslo MQTT", "Poveži s posrednikom MQTT", "Predpona teme MQTT", "Časovni pas"),
    "sr": ("MQTT posrednik", "MQTT port", "MQTT korisnik", "MQTT lozinka", "Poveži se sa MQTT posrednikom",
           "MQTT prefiks teme", "Vremenska zona"),
}

# Time zones offered on the web page; the default follows the language
TIMEZONES = [
    "UTC", "Europe/London", "Europe/Dublin", "Europe/Lisbon", "Europe/Amsterdam", "Europe/Brussels",
    "Europe/Luxembourg", "Europe/Paris", "Europe/Berlin", "Europe/Zurich", "Europe/Vienna", "Europe/Rome",
    "Europe/Madrid", "Europe/Copenhagen", "Europe/Oslo", "Europe/Stockholm", "Europe/Helsinki", "Europe/Tallinn",
    "Europe/Riga", "Europe/Vilnius", "Europe/Warsaw", "Europe/Prague", "Europe/Bratislava", "Europe/Budapest",
    "Europe/Ljubljana", "Europe/Zagreb", "Europe/Belgrade", "Europe/Bucharest", "Europe/Sofia", "Europe/Athens",
    "Europe/Istanbul", "Europe/Kyiv", "Europe/Moscow", "Asia/Jerusalem", "Asia/Riyadh", "Asia/Dubai",
    "Asia/Shanghai", "Asia/Hong_Kong", "Asia/Singapore", "Asia/Seoul", "Asia/Tokyo", "Australia/Perth",
    "Australia/Sydney", "Pacific/Auckland", "Africa/Johannesburg", "America/New_York", "America/Chicago",
    "America/Denver", "America/Los_Angeles", "America/Sao_Paulo",
]
TZ_DEFAULT = {
    "en": "Europe/London", "nl": "Europe/Amsterdam", "de": "Europe/Berlin", "fr": "Europe/Paris",
    "it": "Europe/Rome", "es": "Europe/Madrid", "pl": "Europe/Warsaw", "cs": "Europe/Prague",
    "hu": "Europe/Budapest", "da": "Europe/Copenhagen", "sv": "Europe/Stockholm", "nb": "Europe/Oslo",
    "fi": "Europe/Helsinki", "pt": "Europe/Lisbon", "ar": "Asia/Dubai", "tr": "Europe/Istanbul",
    "et": "Europe/Tallinn", "hr": "Europe/Zagreb", "lv": "Europe/Riga", "lt": "Europe/Vilnius",
    "ro": "Europe/Bucharest", "ru": "Europe/Moscow", "sk": "Europe/Bratislava", "sl": "Europe/Ljubljana",
    "sr": "Europe/Belgrade", "uk": "Europe/Kyiv", "zh": "Asia/Shanghai", "ko": "Asia/Seoul", "he": "Asia/Jerusalem",
}

assert set(TZ_DEFAULT) == set(LANGUAGES) and all(z in TIMEZONES for z in TZ_DEFAULT.values())
assert all(lang in TEXTS or lang in NON_LATIN for lang in LANGUAGES)

_RULE = {DSTRuleType.NONE: "NONE", DSTRuleType.MONTH_WEEK_DAY: "MONTH_WEEK_DAY",
         DSTRuleType.JULIAN_NO_LEAP: "JULIAN_NO_LEAP", DSTRuleType.DAY_OF_YEAR: "DAY_OF_YEAR"}


def _rule(name: str, r) -> str:
    return (f"{name} = {{{r.time_seconds}, {r.day}, esphome::time::DSTRuleType::{_RULE[r.type]}, {r.month}, "
            f"{r.week}, {r.day_of_week}}};")


def tz_cases() -> str:
    out = []
    for i, zone in enumerate(TIMEZONES):
        data = _load_tzdata(zone)
        if data is None:
            sys.exit(f"time zone {zone} not in tzdata")
        posix = _extract_tz_string(data)
        p = parse_posix_tz(posix)
        out.append(f"              case {i}:  // {zone}: {posix}")
        out.append(f"                tz.std_offset_seconds = {p.std_offset_seconds};")
        out.append(f"                tz.dst_offset_seconds = {p.dst_offset_seconds};")
        out.append(f"                {_rule('tz.dst_start', p.dst_start)}")
        out.append(f"                {_rule('tz.dst_end', p.dst_end)}")
        out.append("                break;")
    return "\n".join(out)


MQTT_SETUP = """\
# MQTT settings at runtime, for the ready-made firmware of the web installer: broker, port, user, password and
# time zone are entered on the ESP's web page (group "AquaClean Bridge") and stored on the ESP.
# "Connect to MQTT broker" restarts the ESP with the new settings. Without a broker MQTT stays off.
# Use with packages/mqtt.yaml and packages/web.yaml. The password is never shown or published again.
#
# Generated by tools/gen_firmware.py (time zones from tzdata); change that script, not this file.

substitutions:
  mqtt_setup_broker: MQTT broker
  mqtt_setup_port: MQTT port
  mqtt_setup_user: MQTT user
  mqtt_setup_password: MQTT password
  mqtt_setup_connect: Connect to MQTT broker
  mqtt_setup_topic_prefix: MQTT topic prefix

globals:
  - id: aquaclean_mqtt_broker
    type: std::string
    restore_value: true
    max_restore_data_length: 64
  - id: aquaclean_mqtt_topic_prefix
    type: std::string
    restore_value: true
    max_restore_data_length: 64
    initial_value: '"aquaclean"'
  - id: aquaclean_mqtt_user
    type: std::string
    restore_value: true
    max_restore_data_length: 64
  - id: aquaclean_mqtt_password
    type: std::string
    restore_value: true
    max_restore_data_length: 64
  - id: aquaclean_mqtt_port
    type: int
    restore_value: true
    initial_value: "1883"

mqtt:
  # connected in on_boot below once a broker is stored; no restarts while the broker is unreachable
  enable_on_boot: false
  reboot_timeout: 0s

esphome:
  on_boot:
    - priority: -100
      then:
        - lambda: |-
            const std::string &broker = id(aquaclean_mqtt_broker);
            if (broker.empty()) {
              ESP_LOGW("aquaclean.mqtt", "No MQTT broker set: enter it on http://%s.local (AquaClean Bridge) "
                       "and press Connect to MQTT broker", App.get_name().c_str());
              return;
            }
            auto *mqtt = esphome::mqtt::global_mqtt_client;
            mqtt->set_broker_address(broker);
            mqtt->set_broker_port(static_cast<uint16_t>(id(aquaclean_mqtt_port)));
            mqtt->set_username(id(aquaclean_mqtt_user));
            mqtt->set_password(id(aquaclean_mqtt_password));
            const std::string &prefix = id(aquaclean_mqtt_topic_prefix);
            if (!prefix.empty())
              mqtt->set_topic_prefix(prefix, "");  // 2nd arg != prefix → use it verbatim, no MAC suffix
            ESP_LOGI("aquaclean.mqtt", "MQTT broker %s:%d, topic prefix %s", broker.c_str(),
                     id(aquaclean_mqtt_port), prefix.c_str());
            mqtt->enable();

text:
  - platform: template
    id: aquaclean_mqtt_broker_text
    name: ${mqtt_setup_broker}
    icon: mdi:server-network
    entity_category: config
    mode: text
    max_length: 63
    update_interval: never
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 90
    lambda: "return id(aquaclean_mqtt_broker);"
    set_action:
      - lambda: id(aquaclean_mqtt_broker) = x;
      - component.update: aquaclean_mqtt_broker_text
  - platform: template
    id: aquaclean_mqtt_user_text
    name: ${mqtt_setup_user}
    icon: mdi:account
    entity_category: config
    mode: text
    max_length: 63
    update_interval: never
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 92
    lambda: "return id(aquaclean_mqtt_user);"
    set_action:
      - lambda: id(aquaclean_mqtt_user) = x;
      - component.update: aquaclean_mqtt_user_text
  - platform: template
    id: aquaclean_mqtt_password_text
    name: ${mqtt_setup_password}
    icon: mdi:form-textbox-password
    entity_category: config
    mode: password
    max_length: 63
    update_interval: never
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 93
    # the stored password is never shown or published; "********" means: unchanged
    lambda: |-
      return std::string(id(aquaclean_mqtt_password).empty() ? "" : "********");
    set_action:
      - lambda: |-
          if (x != "********")
            id(aquaclean_mqtt_password) = x;
      - component.update: aquaclean_mqtt_password_text
  - platform: template
    id: aquaclean_mqtt_topic_prefix_text
    name: ${mqtt_setup_topic_prefix}
    icon: mdi:label-outline
    entity_category: config
    mode: text
    max_length: 48
    update_interval: never
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 96
    # base of every MQTT topic (<prefix>/<component>/<entity>/state|command); neutral by default, not the
    # discovery prefix (that stays "homeassistant" in packages/mqtt.yaml, the shared discovery convention).
    lambda: "return id(aquaclean_mqtt_topic_prefix);"
    set_action:
      - lambda: id(aquaclean_mqtt_topic_prefix) = x;
      - component.update: aquaclean_mqtt_topic_prefix_text

number:
  - platform: template
    id: aquaclean_mqtt_port_number
    name: ${mqtt_setup_port}
    icon: mdi:numeric
    entity_category: config
    mode: box
    min_value: 1
    max_value: 65535
    step: 1
    update_interval: never
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 91
    lambda: "return id(aquaclean_mqtt_port);"
    set_action:
      - lambda: id(aquaclean_mqtt_port) = static_cast<int>(x);
      - component.update: aquaclean_mqtt_port_number

button:
  - platform: template
    name: ${mqtt_setup_connect}
    icon: mdi:connection
    entity_category: config
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 94
    on_press:
      - logger.log: "MQTT settings stored, restarting"
      - delay: 500ms
      - lambda: App.safe_reboot();
"""

# Time-zone selector, shared by both web-installer routes (MQTT and native API). The chosen zone is stored and
# applied on boot; every time platform feeds the same system clock, so this also gives SNTP-only controllers
# (Homey, ioBroker) correct local time. Needs the web.yaml sorting group "aquaclean_web_bridge".
# Generated by tools/gen_firmware.py (zones + offsets from tzdata); change that script, not packages/timezone.yaml.
TIMEZONE = """\
substitutions:
  tz_label: Time zone
  tz_default: Europe/Amsterdam

esphome:
  on_boot:
    - priority: -100
      then:
        - script.execute:
            id: aquaclean_apply_timezone
            index: !lambda 'return id(aquaclean_timezone).active_index().value_or(0);'

script:
  - id: aquaclean_apply_timezone
    parameters:
      index: int
    then:
      - lambda: |-
          esphome::time::ParsedTimezone tz{};
          switch (index) {
__CASES__
            default:
              return;
          }
          esphome::time::set_global_tz(tz);

select:
  - platform: template
    id: aquaclean_timezone
    name: ${tz_label}
    icon: mdi:map-clock
    entity_category: config
    optimistic: true
    restore_value: true
    initial_option: ${tz_default}
    web_server:
      sorting_group_id: aquaclean_web_bridge
      sorting_weight: 95
    options:
__OPTIONS__
    on_value:
      - script.execute:
          id: aquaclean_apply_timezone
          index: !lambda "return i;"
"""

FIRMWARE_MQTT = """\
# Ready-made firmware for the web installer: {board_name}, MQTT route, language: {lang}.
# Generated by tools/gen_firmware.py; change that script, not this file.
# Wi-Fi is set up in the browser (Improv) or through the hotspot "AquaClean Setup". The MQTT broker and the time
# zone are entered on the ESP's web page (http://<name>.local, group AquaClean Bridge). The toilet and the
# remote control are found automatically.
substitutions:
  name: aquaclean
  friendly_name: Geberit AquaClean Bridge
  language: {lang}
  tz_label: {tz_label}
  tz_default: {tz}
{texts}
esphome:
  name: ${{name}}
  friendly_name: ${{friendly_name}}
  name_add_mac_suffix: true

packages:
  board: !include ../packages/board-{board}.yaml
  aquaclean: !include ../packages/aquaclean.yaml
  mqtt: !include ../packages/mqtt.yaml
  web: !include ../packages/web.yaml
  mqtt_setup: !include ../packages/mqtt_setup.yaml
  timezone: !include ../packages/timezone.yaml
  provisioning: !include ../packages/provisioning.yaml

external_components:
  - source:
      type: local
      path: ../components
    components: [geberit_aquaclean]

# Apple Home (HomeKit) is compiled into every web-installer image but stays off until the "Apple Home"
# switch is turned on (bridge web page or the controller). Turning it on starts HomeKit; off restarts without it.
geberit_aquaclean:
  homekit:
    enabled: true
    runtime_toggle: true

logger:
  logs:
    ble_device_base: INFO
ota:
  - platform: esphome
"""

FIRMWARE_API = """\
# Ready-made firmware for the web installer: {board_name}, native-API route (runtime key), language: {lang}.
# Generated by tools/gen_firmware.py; change that script, not this file.
# Codeless onboarding for any native-API controller (Home Assistant, Homey, ioBroker):
#   Wi-Fi is set in the browser (Improv) or via the hotspot "AquaClean Setup"; when no Wi-Fi is known or
#   reachable the bridge opens that hotspot (captive portal). The native-API encryption key is NOT in YAML:
#   set it once at http://<name>.local/setup (also reachable on the hotspot at http://192.168.4.1/setup),
#   then add the bridge in your controller with that key. The toilet and the remote control are found
#   automatically.
substitutions:
  name: aquaclean
  friendly_name: Geberit AquaClean Bridge
  language: {lang}
  tz_label: {tz_label}
  tz_default: {tz}
esphome:
  name: ${{name}}
  friendly_name: ${{friendly_name}}
  name_add_mac_suffix: true

packages:
  board: !include ../packages/board-{board}.yaml
  aquaclean: !include ../packages/aquaclean.yaml
  route: !include ../packages/api_setup.yaml
  web: !include ../packages/web.yaml
  timezone: !include ../packages/timezone.yaml
  provisioning: !include ../packages/provisioning.yaml

external_components:
  - source:
      type: local
      path: ../components
    components: [geberit_aquaclean]

# Apple Home (HomeKit) is compiled into every web-installer image but stays off until the "Apple Home"
# switch is turned on (bridge web page or the controller). Turning it on starts HomeKit; off restarts without it.
geberit_aquaclean:
  homekit:
    enabled: true
    runtime_toggle: true

logger:
  logs:
    ble_device_base: INFO
ota:
  - platform: esphome
"""

WORKFLOW = """\
# Builds the ready-made firmware (MQTT route, one per board and language) and publishes the web installer on
# GitHub Pages. Enable Pages with source "GitHub Actions" in the repository settings.
# Generated by tools/gen_firmware.py; change that script, not this file.
name: Build firmware and web installer

on:
  push:
    tags: ["v*"]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        route: [mqtt, api]
        board: [{boards}]
        language: [{languages}]
    steps:
      - uses: actions/checkout@v4
      - uses: esphome/build-action@v7
        id: esphome
        with:
          yaml-file: firmware/aquaclean-${{{{ matrix.route }}}}-${{{{ matrix.board }}}}-${{{{ matrix.language }}}}.yaml
          version: "2026.10.2"
          complete-manifest: true
      - name: Collect
        run: |
          mkdir -p out/${{{{ matrix.route }}}}/${{{{ matrix.board }}}}/${{{{ matrix.language }}}}
          cp -r "${{{{ steps.esphome.outputs.name }}}}"/* out/${{{{ matrix.route }}}}/${{{{ matrix.board }}}}/${{{{ matrix.language }}}}/
      - uses: actions/upload-artifact@v4
        with:
          name: firmware-${{{{ matrix.route }}}}-${{{{ matrix.board }}}}-${{{{ matrix.language }}}}
          path: out

  pages:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{{{ steps.deployment.outputs.page_url }}}}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/download-artifact@v4
        with:
          path: artifacts
      - name: Assemble site
        run: |
          mkdir -p site/firmware
          cp static/index.html static/boards.json site/
          cp -r static/series-250 site/series-250
          for d in artifacts/firmware-*; do cp -r "$d"/* site/firmware/; done
      - name: Add the Alba firmware if a Series 250 release exists
        env:
          GH_TOKEN: ${{{{ github.token }}}}
        run: |
          tag=$(gh release list --repo "$GITHUB_REPOSITORY" --limit 100 --json tagName --jq 'map(.tagName | select(startswith("alba-v"))) | .[0]')
          if [ -n "$tag" ] && gh release download "$tag" --repo "$GITHUB_REPOSITORY" --pattern firmware.bin --dir site/series-250; then
            echo "Added Alba firmware from $tag"
          else
            echo "No Alba firmware.bin available (no release, or the release has no asset yet);"
            echo "the Series 250 installer page ships without a binary. This never blocks the 248 deploy."
          fi
      - uses: actions/upload-pages-artifact@v3
        with:
          path: site
      - id: deployment
        uses: actions/deploy-pages@v4
"""


def main() -> None:
    options = "\n".join(f"      - {z}" for z in TIMEZONES)
    (ROOT / "packages/mqtt_setup.yaml").write_text(MQTT_SETUP, encoding="utf-8")
    # Time-zone selector shared by both routes (zones + offsets baked in from tzdata).
    (ROOT / "packages/timezone.yaml").write_text(
        TIMEZONE.replace("__CASES__", tz_cases()).replace("__OPTIONS__", options), encoding="utf-8")

    fw = ROOT / "firmware"
    for old in fw.glob("aquaclean-*.yaml"):
        old.unlink()
    keys = ("broker", "port", "user", "password", "connect", "topic_prefix")
    for board, board_name in BOARDS.items():
        for lang in LANGUAGES:
            t = TEXTS.get(lang, TEXTS["en"])
            texts = "\n".join(f"  mqtt_setup_{k}: {json.dumps(v, ensure_ascii=False)}" for k, v in zip(keys, t))
            tz_label = json.dumps(t[6], ensure_ascii=False)  # the localized "Time zone" label
            (fw / f"aquaclean-mqtt-{board}-{lang}.yaml").write_text(
                FIRMWARE_MQTT.format(board=board, board_name=board_name, lang=lang, tz=TZ_DEFAULT[lang],
                                     tz_label=tz_label, texts=texts),
                encoding="utf-8")
            # Native-API route: runtime key via /setup, same AP/captive-portal Wi-Fi fallback (provisioning).
            (fw / f"aquaclean-api-{board}-{lang}.yaml").write_text(
                FIRMWARE_API.format(board=board, board_name=board_name, lang=lang, tz=TZ_DEFAULT[lang],
                                    tz_label=tz_label),
                encoding="utf-8")

    (ROOT / "static/boards.json").write_text(
        json.dumps([{"id": b, "name": n} for b, n in BOARDS.items()], indent=2) + "\n", encoding="utf-8")
    (ROOT / ".github/workflows/build.yml").write_text(
        WORKFLOW.format(boards=", ".join(BOARDS), languages=", ".join(LANGUAGES)), encoding="utf-8")
    print(f"{2 * len(BOARDS) * len(LANGUAGES)} firmware files (mqtt + api), {len(TIMEZONES)} time zones")


if __name__ == "__main__":
    main()
