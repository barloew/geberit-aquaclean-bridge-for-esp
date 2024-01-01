# Models and their entities

Every entity of the component names the capabilities it needs (`CAPS_OF` in `__init__.py`); the build stops on an entity without a classification. After the feature discovery has read the article number, an entity is created in Home Assistant only when the detected model has all its capabilities. A model that is not in the list gets only what every model has. On top of that, a setting the toilet does not answer stays hidden, and pre-flush, automatic flush and flush need the interface module for the flush actuation (status 64, bit 0).

## Sources

* **Geberit Home app 2.14.2**: the device definitions the app carries for every AquaClean model (`Home.Core.dll`, data point list per model: AqC Sela, Mera Classic, Mera Floorstanding, Mera Comfort, Tuma Classic, Tuma Comfort, Cama, Cama Testset), its maintenance screens per model (descaling, arm cleaning, filter change) and its enum variants.
* **Geberit AquaClean model overview** (functions per model; it decides where the app's demo data says more, e.g. no orientation light on the Mera Classic).
* **AquaClean Sela user manual** 968.684.00.0 (09): light mode *automatic* with light sensor sensitivity, automatic flush only with the optional interface module, descaling and shower head cleaning menus.

Device series in the app: **248** is the classic AquaClean protocol that this bridge speaks (variants 1 Mera floor-standing, 2 Mera Classic, 3 Mera Comfort, 4 Tuma Classic, 5 Tuma Comfort, 6 Sela, 7 WhirlSpray tester, shown as *Cama Testset*, 8 Cama). The *Cama test set* (146.30x) is a Geberit trial unit with an expiry date and a return dialog, not a model for sale; the bridge treats it as a Cama without registration. **250** is the app's new AquaClean platform: the AquaClean Alba (146.350, 146.351). It has its own code path in the app and is not supported by this bridge. Series 250 uses an encrypted protocol (protocol v2) instead of the cleartext Series 248 protocol; a separate, experimental ESP bridge for the Alba is in the [`series-250-bridge` branch](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/tree/series-250-bridge).

Flush actuation (automatic flush on standing up, flush from the remote control or the app) needs Geberit's interface module, except on the floor-standing Mera where it is built in: Mera Comfort and Classic, Sela (from 2019, optional) and the Tuma Comfort complete system (146.29x, optional, from Geberit's product information rather than the app). The Tuma Classic, the Alba and the separate shower toilet seats have no flush actuation. Draining is only in the app's data points of the Cama: the Sela (from 2019), Mera and Tuma refresh and drain the water themselves and never need draining by hand; only the old models with an internal boiler (AquaClean 4000, 5000, 8000, Sela before 2019) have a drain screw.

Not checked on a toilet: everything except the Sela. The usage statistics (`statistics`) are read by the app's classic AquaClean product (Sela, Mera, Tuma); the app does not read them for the Cama. The descaling step texts of the Mera and Tuma (variant 2) and the light mode options of the Mera Comfort (off, on, on approach, in the dark) are the app's variants assigned by model family, not confirmed.

## Series 250 (AquaClean Alba) — not supported by this bridge

> ⚠️ **Not supported here.** Series 250 runs on the encrypted *protocol v2*, which this ESPHome component does not implement. The functions below are documented so the model's capabilities are clear, but the bridge cannot talk to a Series 250 device. A separate, experimental ESP bridge for the Alba is in the [`series-250-bridge` branch](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/tree/series-250-bridge).

| Model | Article | Series | Notes |
|---|---|---|---|
| AquaClean Alba | 146.350, 146.351 | 250 | Remote control art. 245.368.01.1; shower only; SoftClosing (mechanical) lid; no orientation light |

**AquaClean Alba functions** (from the Alba manual — Dutch user manual). The Alba is a **basic shower
toilet**: it does **not** have the comfort functions of the larger models — **no lady shower, no dryer, no odor
extraction, no orientation light, no motorised/app-controlled lid (the SoftClosing lid is a mechanical damped
close only), no flush actuation, no seat heating.**

Remote control (front) — all daily functions:

- `<Shower>`: start / stop the shower. The shower always runs **with the oscillating arm movement**.
- `<+>` / `<–>` **while showering**: shower jet strength, 5 levels.
- `<+>` and `<–>` **together, while showering**: switch the oscillating movement off / on.
- Status LED (visible only when needed).
- `<Shower>` + `<+>` **together**: extend the shower arm to clean the shower head; a press of `<Shower>` retracts it.
- `<Shower>` + `<–>` **held 5 s**: start the descaling program (or start it from the app).

Geberit Home app:

- Shower start / stop, shower jet strength (5 levels) and the oscillating movement on / off.
- **Shower-arm position (5 levels) — app only** (it is not adjustable on the remote).
- Water temperature (5 levels) and water heating on / off.
- Descaling ([Maintenance & care] → [Descaling]) and the shower-head cleaning routine.
- User profiles — an **app-side** feature (every model): in the Geberit Home app each user creates a personal
  profile, and applying it writes those settings to the toilet in one batch. The Alba **remote** has no profile
  keys and the Alba toilet keeps no recallable profiles of its own; its basic profile is just the factory
  defaults (arm position level 3, oscillation on, water heating on, water temperature level 3).

What a given Alba exposes still depends on its firmware; the list above is what the Alba manual documents.

## Capabilities per model (Series 248)

| Capability | Mera Comfort | Mera Classic | Mera Classic floor-standing | Sela | Tuma Comfort | Tuma Classic | Cama | Cama test set | unknown model |
|---|---|---|---|---|---|---|---|---|---|
| `shower` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `pressure_3` |  |  |  |  |  |  | ✓ | ✓ |  |
| `shower_position` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |
| `water_temperature` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `water_heating_switch` |  |  |  |  | ✓ | ✓ |  |  |  |
| `oscillation` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |
| `lady_shower` | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |  |
| `user_detection` | ✓ | ✓ | ✓ | ✓ | ✓ |  | ✓ | ✓ |  |
| `basic_profile` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |
| `user_profiles` | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |  |
| `remote_control` | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |  |
| `orientation_light` | ✓ |  |  | ✓ |  |  |  |  |  |
| `light_mode` | ✓ |  |  | ✓ |  |  |  |  |  |
| `light_mode_sela` |  |  |  | ✓ |  |  |  |  |  |
| `light_mode_approach` | ✓ |  |  |  |  |  |  |  |  |
| `light_run_on` |  |  |  | ✓ |  |  |  |  |  |
| `light_time_slots` |  |  |  | ✓ |  |  |  |  |  |
| `light_sensor` |  |  |  | ✓ |  |  |  |  |  |
| `descaling` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `descaling_v1` |  |  |  | ✓ |  |  |  |  |  |
| `descaling_v2` | ✓ | ✓ | ✓ |  | ✓ | ✓ |  |  |  |
| `descaling_v3` |  |  |  |  |  |  | ✓ | ✓ |  |
| `descaling_counters` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |
| `arm_cleaning` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |
| `service_status` | ✓ | ✓ | ✓ |  |  |  |  |  |  |
| `spray_calibration` |  |  |  | ✓ |  |  | ✓ | ✓ |  |
| `shower_arm_adjust` | ✓ | ✓ | ✓ |  |  |  |  |  |  |
| `dryer` | ✓ | ✓ | ✓ |  | ✓ |  |  |  |  |
| `no_dryer` |  |  |  | ✓ |  | ✓ | ✓ | ✓ |  |
| `dryer_fan_power` | ✓ | ✓ | ✓ |  |  |  |  |  |  |
| `dryer_arm_adjust` | ✓ | ✓ | ✓ |  |  |  |  |  |  |
| `seat_heating` | ✓ |  |  |  | ✓ |  |  |  |  |
| `seat_heating_switch` |  |  |  |  | ✓ |  |  |  |  |
| `odor_extraction` | ✓ | ✓ | ✓ |  | ✓ |  |  |  |  |
| `filter` | ✓ | ✓ | ✓ |  | ✓ |  |  |  |  |
| `system_flush` | ✓ | ✓ | ✓ |  |  |  |  |  |  |
| `auto_flush` | ✓ | ✓ | ✓ | ✓ | complete system |  |  |  |  |
| `flush_module` | ✓ | ✓ | ✓ | ✓ | complete system |  |  |  |  |
| `lid` | ✓ |  |  |  |  |  |  |  |  |
| `care_mode` |  |  | ✓ |  |  |  |  |  |  |
| `demo_mode` | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |  |
| `water_hardness` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |
| `registration` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |
| `draining` |  |  |  |  |  |  | ✓ | ✓ |  |
| `trial` |  |  |  |  |  |  |  | ✓ |  |
| `statistics` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |  |  |  |

## Entities per capability

| Entity | Needs |
|---|---|
| User detected (`seated`) | `user_detection` |
| Shower active (`shower_active`) | `shower` |
| Lady shower active (`lady_shower_active`) | `lady_shower` |
| Start or stop shower (`shower_toggle`) | `shower` |
| Start or stop Lady shower (`lady_shower_toggle`) | `lady_shower` |
| Dryer running (`dryer_active`) | `dryer` |
| Toggle dryer (`dryer_toggle`) | `dryer` |
| Stop shower and dryer (`stop_all`) | `shower`, `dryer` |
| Stop shower (`stop_shower`) | `shower`, `no_dryer` |
| Spray intensity (`spray_intensity`) | `shower` |
| Spray arm position (`spray_arm_position`) | `shower_position` |
| Spray intensity Lady (`lady_spray_intensity`) | `lady_shower` |
| Spray arm position Lady (`lady_spray_arm_position`) | `lady_shower` |
| Water temperature (`water_temperature`) | `water_temperature` |
| Oscillating spray (`oscillating_spray`) | `oscillation` |
| Seat heating (`seat_heating`) | `seat_heating` |
| Dryer temperature (`dryer_temperature`) | `dryer` |
| Dryer intensity (`dryer_intensity`) | `dryer_fan_power` |
| Dryer (`dryer`) | `dryer` |
| Odor extraction (`odor_extraction`) | `odor_extraction` |
| Seat heating on (`seat_heating_on`) | `seat_heating_switch` |
| Pre-flush (`preflush`) | `system_flush` |
| WC water heating (`wc_water_heating`) | `water_heating_switch` |
| Default spray intensity (`default_spray_intensity`) | `shower`, `basic_profile` |
| Default spray arm position (`default_spray_arm_position`) | `shower_position`, `basic_profile` |
| Default spray intensity Lady (`default_lady_spray_intensity`) | `lady_shower`, `basic_profile` |
| Default spray arm position Lady (`default_lady_spray_arm_position`) | `lady_shower`, `basic_profile` |
| Default water temperature (`default_water_temperature`) | `water_temperature`, `basic_profile` |
| Default oscillating spray (`default_oscillating_spray`) | `oscillation`, `basic_profile` |
| Default seat heating (`default_seat_heating`) | `seat_heating`, `basic_profile` |
| Default dryer temperature (`default_dryer_temperature`) | `dryer`, `basic_profile` |
| Default dryer intensity (`default_dryer_intensity`) | `dryer_fan_power`, `basic_profile` |
| Default dryer (`default_dryer`) | `dryer`, `basic_profile` |
| Default odor extraction (`default_odor_extraction`) | `odor_extraction`, `basic_profile` |
| Default seat heating on (`default_seat_heating_on`) | `seat_heating_switch`, `basic_profile` |
| Default pre-flush (`default_preflush`) | `system_flush`, `basic_profile` |
| Default WC water heating (`default_wc_water_heating`) | `water_heating_switch`, `basic_profile` |
| Spray intensity (`profile_1_spray_intensity`) | `shower`, `user_profiles` |
| Spray arm position (`profile_1_spray_arm_position`) | `shower_position`, `user_profiles` |
| Spray intensity Lady (`profile_1_lady_spray_intensity`) | `lady_shower`, `user_profiles` |
| Spray arm position Lady (`profile_1_lady_spray_arm_position`) | `lady_shower`, `user_profiles` |
| Water temperature (`profile_1_water_temperature`) | `water_temperature`, `user_profiles` |
| Oscillating spray (`profile_1_oscillating_spray`) | `oscillation`, `user_profiles` |
| Seat heating (`profile_1_seat_heating`) | `seat_heating`, `user_profiles` |
| Dryer temperature (`profile_1_dryer_temperature`) | `dryer`, `user_profiles` |
| Dryer intensity (`profile_1_dryer_intensity`) | `dryer_fan_power`, `user_profiles` |
| Dryer (`profile_1_dryer`) | `dryer`, `user_profiles` |
| Odor extraction (`profile_1_odor_extraction`) | `odor_extraction`, `user_profiles` |
| Seat heating on (`profile_1_seat_heating_on`) | `seat_heating_switch`, `user_profiles` |
| Pre-flush (`profile_1_preflush`) | `system_flush`, `user_profiles` |
| WC water heating (`profile_1_wc_water_heating`) | `water_heating_switch`, `user_profiles` |
| Activate profile 1 settings (`profile_1_activate`) | `user_profiles` |
| Store settings in profile 1 (`profile_1_store`) | `user_profiles` |
| Clear profile (`profile_1_delete`) | `user_profiles` |
| Capture IRK (`profile_1_irk_capture`) | `user_profiles` |
| Show IRK (`profile_1_irk_show`) | `user_profiles` |
| User (`profile_1_user_name`) | `user_profiles` |
| Home Assistant device (`profile_1_user_ha_device`) | `user_profiles` |
| Welcome light (`profile_1_welcome_light`) | `user_profiles` |
| Recognized (`profile_1_user_recognized`) | `user_profiles` |
| Seated (`profile_1_user_active`) | `user_profiles` |
| Device signal user profile 1 (`profile_1_user_rssi`) | `user_profiles` |
| IRK (`profile_1_user_irk`) | `user_profiles` |
| Activate profile 2 settings (`profile_2_activate`) | `user_profiles` |
| Store settings in profile 2 (`profile_2_store`) | `user_profiles` |
| Activate profile 3 settings (`profile_3_activate`) | `user_profiles` |
| Store settings in profile 3 (`profile_3_store`) | `user_profiles` |
| Orientation light (`orientation_light`) | `orientation_light` |
| Orientation light activation (`light_activation`) | `light_mode` |
| Orientation light color (`light_color`) | `orientation_light` |
| Orientation light brightness (`light_brightness`) | `orientation_light` |
| Orientation light run-on time (`light_run_on_time`) | `light_run_on` |
| Orientation light window 1 start (`light_window_1_start`) | `light_time_slots` |
| Orientation light window 1 end (`light_window_1_end`) | `light_time_slots` |
| Orientation light window 2 start (`light_window_2_start`) | `light_time_slots` |
| Orientation light window 2 end (`light_window_2_end`) | `light_time_slots` |
| Demo mode (`demo_mode`) | `demo_mode` |
| Open lid automatically (`lid_auto_open`) | `lid` |
| Close lid automatically (`lid_auto_close`) | `lid` |
| Lid sensor range (`lid_sensor_range`) | `lid` |
| Odor extraction run-on (`odor_extraction_run_on`) | `odor_extraction` |
| Next descaling in (`days_until_descaling`) | `descaling_counters` |
| Descaling status (`descaling_state`) | `descaling` |
| Descaling time left (`descaling_time_left`) | `descaling` |
| Start descaling (`descaling_start`) | `descaling` |
| Continue descaling (`descaling_continue`) | `descaling` |
| Cancel descaling (`descaling_cancel`) | `descaling` |
| Unlock shower (`shower_unlock`) | `descaling_counters` |
| Showers until unlock (`showers_until_unlock`) | `descaling_counters` |
| Descaling requested since (`descaling_requested`) | `descaling_counters` |
| Without descaling the shower stops in (`shower_restricted_in`) | `descaling_counters` |
| Showers today (`shower_count`) | `descaling_counters` |
| Last descaling (`last_descaling`) | `descaling` |
| Number of descalings (`descaling_count`) | `descaling_counters` |
| Days until filter change (`days_until_filter_change`) | `filter` |
| Filter replaced (`filter_reset`) | `filter` |
| Toggle odor extraction (`odor_toggle`) | `odor_extraction` |
| Toggle odor extraction run-on (`odor_run_on_toggle`) | `odor_extraction` |
| Arm cleaning status (`service_state`) | `service_status` |
| Start arm cleaning (`arm_cleaning_start`) | `arm_cleaning` |
| Arm cleaning next step (`arm_cleaning_next`) | `arm_cleaning` |
| Spray calibration (`spray_calibration`) | `spray_calibration` |
| Open or close lid (`lid_toggle`) | `lid` |
| Calibrate lid (`lid_calibration`) | `lid` |
| Adjust lid opening angle (`lid_angle_start`) | `lid` |
| Lid opening angle larger (`lid_angle_up`) | `lid` |
| Lid opening angle smaller (`lid_angle_down`) | `lid` |
| Save lid opening angle (`lid_angle_save`) | `lid` |
| Automatic flush (`auto_flush`) | `auto_flush` |
| Light sensor sensitivity (`light_sensor_sensitivity`) | `light_sensor` |
| Accessibility mode (`accessibility_mode`) | `care_mode` |
| Water hardness (`water_hardness`) | `water_hardness` |
| Remote control connected (`remote_connected`) | `remote_control` |
| Remote control pass-through to toilet active (`remote_passthrough`) | `remote_control` |
| Remote control last button (`remote_last_button`) | `remote_control` |
| Remote control button (`remote_button`) | `remote_control` |
| Remote control firmware (`remote_firmware`) | `remote_control` |
| Remote control address (`remote_address`) | `remote_control` |
| Remote control forward to toilet (`remote_forward`) | `remote_control` |
| Pair remote control (`remote_pairing`) | `remote_control` |
| Unpair remote control (`remote_unpair`) | `remote_control` |
| Pair with toilet (`pair_as_remote`) | `remote_control` |
| Registration (`registration`) | `registration` |
| Draining (`draining`) | `draining` |
| Drain pump and pipe (`drain`) | `draining` |
| Flush (`flush`) | `flush_module` |
| Connected modules (`ssm_modules`) | `flush_module` |
| Lid offset (`lid_offset`) | `lid` |
| Shower arm offset (`shower_arm_offset`) | `shower_arm_adjust` |
| Dryer arm offset (`dryer_arm_offset`) | `dryer_arm_adjust` |
| Adjust shower arm position (`shower_arm_adjust_start`) | `shower_arm_adjust` |
| Shower arm position higher (`shower_arm_adjust_up`) | `shower_arm_adjust` |
| Shower arm position lower (`shower_arm_adjust_down`) | `shower_arm_adjust` |
| Save shower arm position (`shower_arm_adjust_save`) | `shower_arm_adjust` |
| Adjust dryer arm position (`dryer_arm_adjust_start`) | `dryer_arm_adjust` |
| Dryer arm position higher (`dryer_arm_adjust_up`) | `dryer_arm_adjust` |
| Dryer arm position lower (`dryer_arm_adjust_down`) | `dryer_arm_adjust` |
| Save dryer arm position (`dryer_arm_adjust_save`) | `dryer_arm_adjust` |
| Trial period end (`trial_end`) | `trial` |
| Trial period remaining (`trial_remaining`) | `trial` |
| Extended trial remaining (`trial_extended_remaining`) | `trial` |
| Toilet operating time (`stat_uptime`) | `statistics` |
| Active operating time (`stat_run_time`) | `statistics` |
| Showers total (`stat_showers`) | `statistics`, `shower` |
| Last shower duration (`stat_last_shower`) | `statistics`, `shower` |
| Average shower duration (`stat_avg_shower`) | `statistics`, `shower` |
| Lady showers total (`stat_lady_showers`) | `statistics`, `lady_shower` |
| Last lady shower duration (`stat_last_lady`) | `statistics`, `lady_shower` |
| Average lady shower duration (`stat_avg_lady`) | `statistics`, `lady_shower` |
| Showers with oscillation (`stat_oscillation`) | `statistics`, `oscillation` |
| Lady showers with oscillation (`stat_lady_oscillation`) | `statistics`, `lady_shower`, `oscillation` |
| Dryer uses total (`stat_dryer`) | `statistics`, `dryer` |
| Odor extraction uses (`stat_odor`) | `statistics`, `odor_extraction` |
| Seat heating uses (`stat_seat_heating`) | `statistics`, `seat_heating` |
| Lid lifter uses (`stat_lid`) | `statistics`, `lid` |
| Cleanings (`stat_cleaning`) | `statistics`, `arm_cleaning` |
| Toilet visits total (`stat_users`) | `statistics`, `user_detection` |
| Last toilet visit duration (`stat_last_sitting`) | `statistics`, `user_detection` |
| Average toilet visit duration (`stat_avg_sitting`) | `statistics`, `user_detection` |
| System flushes (`stat_system_flush`) | `statistics`, `flush_module` |
| Profile 1-4 uses (`stat_profile_1..4`) | `statistics`, `user_profiles` |

Entities of the ESP itself (connection, language, user recognition, diagnostics) and the identification of the toilet (model, article and serial number, firmware, error code) are always there.
