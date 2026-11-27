#!/usr/bin/env python3
"""Optional/CI helper: patch (or check) ESPHome's embedded web_server v3 page with the aquaclean icons.

You normally do **not** need this. The component applies the same injection itself during codegen (see
``components/geberit_aquaclean/web_icons.py``, called from ``to_code``), so a normal ESPHome build —
including the ESPHome dashboard pulling the component over ``github://`` — already ships the icons on the
device's own page while keeping ``web_server: local: true``. No manual step, no ``docker exec``.

This script is a thin wrapper over that same code, for two cases:

    python3 tools/icons/patch_webserver_v3.py            # patch the installed ESPHome now (manual override)
    python3 tools/icons/patch_webserver_v3.py --check     # CI: fail if the installed page isn't current

Both reuse the component's implementation and the committed ``web_icons.json``, so there is one source of
truth. (The icon geometry itself is regenerated from ``images/ui/*.svg`` by ``tools/icons/generate.py``,
which also refreshes ``web_icons.json``; that step needs picosvg, this one does not.)
"""
import argparse
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_WEB_ICONS = ROOT / "components" / "geberit_aquaclean" / "web_icons.py"


def _load_web_icons():
    spec = importlib.util.spec_from_file_location("geberit_web_icons", _WEB_ICONS)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="fail if the installed page isn't current")
    args = ap.parse_args()
    web_icons = _load_web_icons()

    header = web_icons._header_path()
    text = header.read_text(encoding="utf-8", errors="replace")
    new_text, changed, skipped, found = web_icons._patched_header(text, enable=True)
    if found == 0:
        # no decodable esp-app page array at all → surface it rather than silently passing
        raise SystemExit(f"{header.name}: found no decodable v3 page array to patch")

    if args.check:
        if new_text != text:
            print(f"STALE: {header} is not patched with the current icons — "
                  "the component patches this at build time; run this only for a manual/CI check",
                  file=sys.stderr)
            return 1
        print("server_index_v3.h is patched and current")
        if skipped:
            print(f"note: {', '.join(sorted(skipped))} array(s) left unpatched (codec unavailable)")
        return 0

    n = web_icons.apply(enable=True)
    print(f"patched {header} ({n} page array(s) changed, local: true preserved)")
    if skipped:
        print(f"note: {', '.join(sorted(skipped))} array(s) left unpatched (codec unavailable)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
