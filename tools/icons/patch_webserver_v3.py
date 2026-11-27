#!/usr/bin/env python3
"""Patch ESPHome's embedded web_server v3 page so it ships the aquaclean icons — keeping local: true.

The ESP's own web page (web_server version 3, `local: true`) is the compressed byte blob in ESPHome's
``server_index_v3.h``. It draws icons with Iconify, which reads ``window.IconifyPreload`` on start-up
but doesn't know the ``aquaclean:`` prefix. This build-time step decodes that blob, injects a small
``<script>`` that sets ``window.IconifyPreload`` with the aquaclean collection, re-compresses, and
writes the header back. ``local: true`` keeps working; the device renders the icons offline.

Robust on purpose (ESPHome may change things between versions):
  * it patches *every* PROGMEM page array it finds, by name-agnostic regex (not just ``INDEX_GZ``);
  * it sniffs gzip vs brotli from the bytes and re-compresses with the same codec;
  * it is idempotent — a previous injection (marked ``data-aquaclean``) is stripped before re-adding,
    so re-running after an icon change or an ESPHome upgrade just refreshes it.

It edits the *installed* ESPHome package in place, so run it with the ESPHome you build with, before
compiling (and again after an ESPHome upgrade / icon change). CI should run it in the build step.

    python3 tools/icons/patch_webserver_v3.py            # patch the installed header
    python3 tools/icons/patch_webserver_v3.py --check     # fail if it isn't patched / is stale

Needs picosvg (pip); brotli (pip) only if ESPHome ships a brotli page array.
"""
import argparse
import gzip
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate  # noqa: E402 — shared ICONS, normalise(), PREFIX, UI

MARKER = "data-aquaclean"
_ARRAY = re.compile(r"\b([A-Za-z_]\w*)\s*\[\s*\d*\s*\]\s*PROGMEM\s*=\s*\{([^}]*)\}", re.S)
_SIZE = "constexpr size_t {name}_SIZE = {size}"
_SCRIPT_MODULE = re.compile(r"<script\b[^>]*\btype\s*=\s*[\"']?module", re.I)
_PRIOR = re.compile(r"<script\b[^>]*" + MARKER + r"[^>]*>.*?</script>", re.S | re.I)


def _preload_tag() -> str:
    collection = generate.iconify_collection(generate.entries())
    return f"<script {MARKER}>window.IconifyPreload=(window.IconifyPreload||[]);window.IconifyPreload.push({json.dumps(collection)});</script>"


def _inflate(data: bytes) -> tuple[bytes, str] | None:
    """Return (decompressed, codec) or None. codec is 'gzip' or 'brotli'."""
    if data[:2] == b"\x1f\x8b":
        try:
            return gzip.decompress(data), "gzip"
        except OSError:
            return None
    try:
        import brotli

        return brotli.decompress(data), "brotli"
    except Exception:
        return None


def _deflate(data: bytes, codec: str) -> bytes:
    if codec == "gzip":
        return gzip.compress(data, 9, mtime=0)
    import brotli

    return brotli.compress(data, quality=11)


def _inject(html: str) -> str:
    html = _PRIOR.sub("", html)  # idempotent: drop any earlier injection
    m = _SCRIPT_MODULE.search(html)
    if not m:
        raise SystemExit("v3 page: no <script type=module> to inject before")
    return html[: m.start()] + _preload_tag() + html[m.start() :]


def _as_c_array(data: bytes) -> str:
    return ", ".join(f"0x{b:02x}" for b in data)


def _patched_header(text: str) -> tuple[str, int]:
    """Return (new header text, number of page arrays patched)."""
    count = 0

    def repl(m: "re.Match[str]") -> str:
        nonlocal count
        body = m.group(2)
        nums = re.findall(r"0x[0-9a-fA-F]+", body)
        if len(nums) < 64:
            return m.group(0)
        raw = bytes(int(x, 16) for x in nums)
        got = _inflate(raw)
        if not got:
            return m.group(0)
        html_bytes, codec = got
        if b"<esp-app" not in html_bytes:
            return m.group(0)
        new = _deflate(_inject(html_bytes.decode("utf-8", "replace")).encode("utf-8"), codec)
        count += 1
        return m.group(0).replace(body, "\n    " + _as_c_array(new) + "\n")

    new_text = _ARRAY.sub(repl, text)
    # fix the *_SIZE constants to the new lengths
    for m in _ARRAY.finditer(new_text):
        name, body = m.group(1), m.group(2)
        n = len(re.findall(r"0x[0-9a-fA-F]+", body))
        new_text = re.sub(
            rf"constexpr\s+size_t\s+{re.escape(name)}_SIZE\s*=\s*\d+",
            f"constexpr size_t {name}_SIZE = {n}",
            new_text,
        )
    return new_text, count


def _header_path() -> Path:
    import esphome

    return Path(esphome.__file__).resolve().parent / "components" / "web_server" / "server_index_v3.h"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if the installed header isn't current")
    args = ap.parse_args()
    header = _header_path()
    text = header.read_text(encoding="utf-8", errors="replace")
    new_text, count = _patched_header(text)
    if count == 0:
        raise SystemExit(f"{header.name}: found no decodable v3 page array to patch")
    if args.check:
        if new_text != text:
            print(f"STALE: {header} is not patched with the current icons — run patch_webserver_v3.py",
                  file=sys.stderr)
            return 1
        print(f"server_index_v3.h patched and current ({count} page array(s))")
        return 0
    header.write_text(new_text, encoding="utf-8")
    print(f"patched {header} ({count} page array(s), local: true preserved)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
