#!/usr/bin/env python3
"""Generate the README platform badges (SVG) from the logos in images/.

Each badge is a small two-part label in the style of shields.io: the platform's own logo and name
on the left, the integration route on the right, coloured like the route in the overview image.
The logos are embedded in the SVG (as data URIs), so the badges need no external service and
render on GitHub as plain images.

    python3 tools/badges/generate.py

Writes images/badges/*.svg. Needs Pillow (text measurement, PNG scaling) and, for logos that
are only usable as a raster (Homey), rsvg-convert.
"""

import base64
import io
import shutil
import subprocess
from html import escape
from pathlib import Path

from PIL import Image, ImageFont

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "images"
OUT = IMAGES / "badges"

# Route colours follow the overview image (images/overview/).
ROUTE_API = ("native API", "#0E7490")
ROUTE_MQTT = ("MQTT", "#C2410C")
ROUTE_HOMEKIT = ("optional · also stand-alone", "#BE123C")

# file name, label, logo (relative to images/), (value, colour)
BADGES = [
    ("esphome", "ESPHome", "brands/esphome.svg", ("2026.10+", "#1F2937")),
    ("home-assistant-api", "Home Assistant", "brands/home-assistant.svg", ROUTE_API),
    ("homey-api", "Homey", "brands/homey.svg", ROUTE_API),
    ("iobroker-api", "ioBroker", "brands/iobroker.svg", ROUTE_API),
    ("home-assistant-mqtt", "Home Assistant", "brands/home-assistant.svg", ROUTE_MQTT),
    ("openhab-mqtt", "openHAB", "brands/openhab.svg", ROUTE_MQTT),
    ("node-red-mqtt", "Node-RED", "brands/node-red.svg", ROUTE_MQTT),
    ("domoticz-mqtt", "Domoticz", "brands/domoticz.png", ROUTE_MQTT),
    # No Apple mark (that needs an MFi license): the project's own generic house icon instead.
    ("apple-home", "Apple Home", "ui/home.svg", ROUTE_HOMEKIT),
]

# Logos whose file has a wide empty margin: (canvas width, canvas height, visible box x, y, w, h).
# Only the empty margin is cut away; the logo itself is shown unmodified.
CROP = {
    "brands/esphome.svg": (400, 400, 70, 70, 260, 260),
}

# Logos above this size are embedded as a scaled-down PNG instead of the original file.
MAX_EMBED_BYTES = 32_000
RASTER_PX = 64

HEIGHT = 20
LOGO = 14
PAD = 6
GAP = 4
FONT_SIZE = 11
FONT_FAMILY = "Verdana,Geneva,DejaVu Sans,sans-serif"
LABEL_BG = "#F6F8FA"
LABEL_FG = "#24292F"
BORDER = "#D0D7DE"

_FONT = None


def text_width(text: str) -> float:
    """Width of text at FONT_SIZE, measured with DejaVu Sans (close to Verdana's metrics)."""
    global _FONT
    if _FONT is None:
        for cand in ("DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "Verdana.ttf"):
            try:
                _FONT = ImageFont.truetype(cand, FONT_SIZE * 10)
                break
            except OSError:
                continue
        else:
            raise SystemExit("No DejaVu Sans or Verdana font found for text measurement")
    return round(_FONT.getlength(text) / 10, 1)


def png_data_uri(img: Image.Image) -> str:
    img = img.convert("RGBA")
    img.thumbnail((RASTER_PX, RASTER_PX), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def logo_data_uri(rel: str) -> str:
    path = IMAGES / rel
    raw = path.read_bytes()
    if path.suffix == ".png":
        return png_data_uri(Image.open(path))
    if len(raw) <= MAX_EMBED_BYTES:
        return "data:image/svg+xml;base64," + base64.b64encode(raw).decode()
    if not shutil.which("rsvg-convert"):
        raise SystemExit(f"{rel} is large and needs rsvg-convert to be scaled down")
    png = subprocess.run(
        ["rsvg-convert", "-w", str(RASTER_PX * 2), "-h", str(RASTER_PX * 2), str(path)],
        check=True,
        capture_output=True,
    ).stdout
    return png_data_uri(Image.open(io.BytesIO(png)))


def logo_element(rel: str, uri: str) -> str:
    y = (HEIGHT - LOGO) / 2
    if rel in CROP:
        cw, ch, bx, by, bw, bh = CROP[rel]
        return (
            f'<svg x="{PAD}" y="{y:g}" width="{LOGO}" height="{LOGO}" viewBox="{bx} {by} {bw} {bh}">'
            f'<image width="{cw}" height="{ch}" href="{uri}"/></svg>'
        )
    return (
        f'<image x="{PAD}" y="{y:g}" width="{LOGO}" height="{LOGO}" preserveAspectRatio="xMidYMid meet" href="{uri}"/>'
    )


def badge(label: str, logo: str, value: str, colour: str) -> str:
    lw = text_width(label)
    vw = text_width(value)
    left = PAD + LOGO + GAP + lw + PAD
    right = PAD + vw + PAD
    total = left + right
    ty = 14
    lx = PAD + LOGO + GAP
    vx = left + PAD
    title = escape(f"{label}: {value}")
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{total:g}" height="{HEIGHT}" viewBox="0 0 {total:g} {HEIGHT}" role="img" aria-label="{title}">
<title>{title}</title>
<clipPath id="r"><rect width="{total:g}" height="{HEIGHT}" rx="3"/></clipPath>
<g clip-path="url(#r)">
<rect width="{left:g}" height="{HEIGHT}" fill="{LABEL_BG}"/>
<rect x="{left:g}" width="{right:g}" height="{HEIGHT}" fill="{colour}"/>
</g>
<rect x="0.5" y="0.5" width="{total - 1:g}" height="{HEIGHT - 1}" rx="2.5" fill="none" stroke="{BORDER}"/>
{logo}
<g font-family="{FONT_FAMILY}" font-size="{FONT_SIZE}">
<text x="{lx:g}" y="{ty}" fill="{LABEL_FG}" textLength="{lw:g}" lengthAdjust="spacingAndGlyphs">{escape(label)}</text>
<text x="{vx:g}" y="{ty}" fill="#FFFFFF" textLength="{vw:g}" lengthAdjust="spacingAndGlyphs">{escape(value)}</text>
</g>
</svg>
"""


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    cache: dict[str, str] = {}
    for name, label, logo, (value, colour) in BADGES:
        uri = cache.setdefault(logo, logo_data_uri(logo))
        (OUT / f"{name}.svg").write_text(badge(label, logo_element(logo, uri), value, colour))
        print(f"images/badges/{name}.svg")


if __name__ == "__main__":
    main()
