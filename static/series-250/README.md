# static/series-250 — Series 250 (Alba) browser installer

This folder is published to GitHub Pages at `/series-250/` by `build.yml`. It is the browser flasher for the
experimental Series 250 bridge (the firmware source lives on the [`series-250-bridge`
branch](https://github.com/barloew/geberit-aquaclean-bridge-for-esp/tree/series-250-bridge)).

- `index.html` — the install page (ESP Web Tools "Install" button + setup instructions).
- `manifest.json` — points ESP Web Tools at `firmware.bin` at flash offset `0x0`.
- `firmware.bin` — **you add this.** The merged, flashable image built from the `series-250-bridge` branch.

## Producing `firmware.bin`

The image is generic (the toilet address is entered at runtime on the device's own web page), so one build
serves every tester. On the `series-250-bridge` branch:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
# merge bootloader + partition table + app into one image using the build's own offsets:
cd build && esptool.py --chip esp32s3 merge_bin -o firmware.bin @flash_args
```

Copy the resulting `firmware.bin` into this folder and commit it. Re-run whenever the firmware changes.
Keep it a **merged** image (offset `0x0`); the manifest flashes it as a single part.
