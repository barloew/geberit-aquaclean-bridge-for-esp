"""Make the bridge's own web page (web_server v3, ``local: true``) draw the ``aquaclean:`` icons.

Why this exists
---------------
With ``custom_icons: true`` a few entities carry an ``aquaclean:<name>`` icon. web_server v3 renders
icons with Iconify, which reads ``window.IconifyPreload`` at start-up but doesn't know our prefix.
When ``local: true`` the page is a compressed byte blob baked into the firmware from ESPHome's
``server_index_v3.h`` (a ``gzip`` array ``INDEX_GZ`` and a ``brotli`` array ``INDEX_BR``), so there is
no external file to add — the icons have to be injected into that blob at build time.

The component does this itself (see ``to_code`` in ``__init__.py``): it decodes each page array, inserts
one ``<script>`` that pushes the aquaclean collection onto ``window.IconifyPreload``, re-compresses with
the same codec and rewrites the header. Because it runs during codegen, it travels with the component
over ``github://`` and happens on every dashboard build — no manual patch step, no ``docker exec``.

Design notes
------------
* The icon geometry is read from the committed ``web_icons.json`` sibling (an Iconify collection), so no
  ``picosvg`` is needed at build time — a plain ESPHome install is enough. ``tools/icons/generate.py``
  regenerates both that JSON and the Home Assistant frontend from ``images/ui/*.svg``.
* Idempotent: a previous injection (marked ``data-aquaclean``) is stripped before re-adding, so repeated
  builds and ESPHome upgrades just refresh it. ``apply(enable=False)`` removes it again.
* Robust: name-agnostic PROGMEM-array regex, codec sniffed from the bytes. ``brotli`` ships with ESPHome
  on CPython (via ``py7zr``); if a brotli array still can't be processed the gzip array is patched anyway
  and a warning is logged, rather than failing the build over a cosmetic icon.
* Only ESPHome's installed package is touched (never the repo). It affects any web_server v3 ``local``
  page built in the same environment, but only adds an unused ``aquaclean:`` collection to other pages.
"""
from __future__ import annotations

import gzip
import json
import logging
import re
from pathlib import Path

_LOGGER = logging.getLogger(__name__)

MARKER = "data-aquaclean"
_ARRAY = re.compile(r"\b([A-Za-z_]\w*)\s*\[\s*\d*\s*\]\s*PROGMEM\s*=\s*\{([^}]*)\}", re.S)
_SCRIPT_MODULE = re.compile(r"<script\b[^>]*\btype\s*=\s*[\"']?module", re.I)
_PRIOR = re.compile(r"<script\b[^>]*" + MARKER + r"[^>]*>.*?</script>", re.S | re.I)
_MIN_BYTES = 64  # a real page array is far bigger; skips tiny unrelated PROGMEM arrays


def _collection() -> dict:
    data = Path(__file__).resolve().parent / "web_icons.json"
    return json.loads(data.read_text(encoding="utf-8"))


def _preload_tag() -> str:
    return (
        f"<script {MARKER}>window.IconifyPreload=(window.IconifyPreload||[]);"
        f"window.IconifyPreload.push({json.dumps(_collection())});</script>"
    )


def _inflate(data: bytes):
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


def _inject(html: str, enable: bool) -> str:
    html = _PRIOR.sub("", html)  # idempotent / also the disable path
    if not enable:
        return html
    m = _SCRIPT_MODULE.search(html)
    if not m:
        raise ValueError("web_server v3 page: no <script type=module> to inject before")
    return html[: m.start()] + _preload_tag() + html[m.start() :]


def _as_c_array(data: bytes) -> str:
    return ", ".join(f"0x{b:02x}" for b in data)


def _patched_header(text: str, enable: bool):
    """Return (new text, pages_changed, skipped_codecs, pages_found).

    pages_found counts the decodable web_server v3 page arrays (so callers can tell "already current"
    from "no page to patch"); pages_changed counts those whose bytes this call rewrote.
    """
    changed = 0
    found = 0
    skipped: set[str] = set()

    def repl(m: "re.Match[str]") -> str:
        nonlocal changed, found
        body = m.group(2)
        nums = re.findall(r"0x[0-9a-fA-F]+", body)
        if len(nums) < _MIN_BYTES:
            return m.group(0)
        raw = bytes(int(x, 16) for x in nums)
        got = _inflate(raw)
        if not got:
            # couldn't decode (e.g. brotli missing) — leave this array, note the codec
            skipped.add("brotli" if raw[:2] != b"\x1f\x8b" else "gzip")
            return m.group(0)
        html_bytes, codec = got
        if b"<esp-app" not in html_bytes:
            return m.group(0)
        found += 1
        new_html = _inject(html_bytes.decode("utf-8", "replace"), enable)
        new = _deflate(new_html.encode("utf-8"), codec)
        if new == raw:
            return m.group(0)
        changed += 1
        return m.group(0).replace(body, "\n    " + _as_c_array(new) + "\n")

    new_text = _ARRAY.sub(repl, text)
    # keep any *_SIZE constants in step with rewritten arrays (this ESPHome uses sizeof(), so usually none)
    for m in _ARRAY.finditer(new_text):
        name, body = m.group(1), m.group(2)
        n = len(re.findall(r"0x[0-9a-fA-F]+", body))
        new_text = re.sub(
            rf"constexpr\s+size_t\s+{re.escape(name)}_SIZE\s*=\s*\d+",
            f"constexpr size_t {name}_SIZE = {n}",
            new_text,
        )
    return new_text, changed, skipped, found


def _header_path() -> Path:
    import esphome

    return (
        Path(esphome.__file__).resolve().parent
        / "components"
        / "web_server"
        / "server_index_v3.h"
    )


def apply(enable: bool) -> int:
    """Inject (enable) or remove (disable) the aquaclean preload in the installed web_server v3 page.

    Returns the number of page arrays changed. Never raises for a cosmetic failure: on error it logs a
    warning and returns 0, so a build is never lost over an icon.
    """
    try:
        header = _header_path()
    except Exception as exc:  # web_server not installed / unexpected layout
        _LOGGER.warning("custom_icons: cannot locate web_server page to patch (%s)", exc)
        return 0
    try:
        text = header.read_text(encoding="utf-8", errors="replace")
        new_text, changed, skipped, _found = _patched_header(text, enable)
    except Exception as exc:
        _LOGGER.warning("custom_icons: could not process the web page (%s); icons may not show", exc)
        return 0
    if skipped and enable:
        _LOGGER.warning(
            "custom_icons: left %s page array(s) unpatched (codec unavailable); "
            "the device page shows icons for the other encoding(s)",
            ", ".join(sorted(skipped)),
        )
    if new_text == text:
        return 0
    try:
        header.write_text(new_text, encoding="utf-8")
    except Exception as exc:
        _LOGGER.warning("custom_icons: could not write the patched web page (%s)", exc)
        return 0
    return changed
