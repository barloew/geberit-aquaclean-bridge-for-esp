#!/usr/bin/env python3
"""Generate the README overview infographic (SVG + PNG) from overview.yaml.

Usage (from the repository root):
    python3 tools/overview/generate.py            write the SVG, the PNG and the README snippet
    python3 tools/overview/generate.py --no-png   write the SVG and the README snippet only
    python3 tools/overview/generate.py --check    write nothing; exit 1 if the SVG or README is stale
    python3 tools/overview/generate.py --lang nl  only the Dutch version (en = the README image)

Reads tools/overview/overview.yaml (layout and wording) and support/matrix.yaml (every support claim),
writes images/overview/aquaclean-bridge-overview.svg, renders the PNG the README shows at 3x, and puts
the <img> snippet between <!-- overview:begin --> and <!-- overview:end --> in README.md.

Translated versions (source material for articles, not shown in the README) come from
tools/overview/i18n/<lang>.yaml: every English string of overview.yaml maps to its translation, or is listed
under `same:` when it stays as it is (product and brand names). A missing or unused entry fails the run, so a
change to the English text can't leave a translation silently stale; text that doesn't fit its box fails too.

Same pattern as tools/steps/generate.py: paths relative to the repository root, icons embedded straight
into the SVG (images/ui, or Lucide from tools/steps/node_modules), nothing loaded from the web. Brand logos
from images/brands are embedded unmodified.

Deterministic: text is measured from the Inter fonts bundled in tools/overview/fonts (OFL-1.1), numbers are
rounded to one decimal, and there are no timestamps, so --check can compare byte for byte. The PNG is
rendered with rsvg-convert against those same bundled fonts only (no system fonts), so it looks the same on
every machine; it is not part of --check because rasterizers differ slightly between versions.

Needs: Python 3.11+, PyYAML, fontTools; rsvg-convert (librsvg) for the PNG.
"""

from __future__ import annotations

import argparse
import ast
import base64
import html
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
from functools import lru_cache
from pathlib import Path

import yaml
from fontTools.ttLib import TTFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]  # paths in overview.yaml are relative to the repository root
FONT_DIR = HERE / "fonts"
UI_DIR = ROOT / "images" / "ui"
BRAND_DIR = ROOT / "images" / "brands"
LUCIDE_DIR = ROOT / "tools" / "steps" / "node_modules" / "lucide-static" / "icons"
MATRIX = ROOT / "support" / "matrix.yaml"
README = ROOT / "README.md"
BEGIN, END = "<!-- overview:begin -->", "<!-- overview:end -->"

FONT_FILES = {
    (400, False): "Inter-Regular.otf",
    (500, False): "Inter-Medium.otf",
    (600, False): "Inter-SemiBold.otf",
    (700, False): "Inter-Bold.otf",
    (800, False): "Inter-ExtraBold.otf",
    (800, True): "Inter-ExtraBoldItalic.otf",
}

# Repository rules (CLAUDE.md) the image must follow; checked on every run.
BANNED_LOGOS = re.compile(r"bluetooth|wi-?fi|usb|apple|geberit", re.I)
BANNED_WORDS = re.compile(r"\b(prox[a-z]*|anal|anale|anaal|analny|analna|analne)\b", re.I)
# Reverse-engineering internals never go into a published image (CLAUDE.md, secrecy guardrails).
INTERNALS = re.compile(r"\b0x[0-9a-f]{2,}\b|\bdp-?id\b|\bpsk\b|\bcrc\b|\bgatt\b", re.I)
MATRIX_OK = {"validated", "supported"}


class GenError(SystemExit):
    def __init__(self, msg: str):
        super().__init__(f"overview: {msg}")


# --------------------------------------------------------------------------------------------- numbers / text


def n(v: float) -> str:
    """Stable number formatting: one decimal, no '-0.0'."""
    s = f"{round(float(v) + 0.0, 1):.1f}"
    return "0.0" if s == "-0.0" else s


def esc(text: str) -> str:
    return html.escape(text, quote=False)


def _kern_pairs(font: TTFont):
    """Pair kerning from the font's GPOS 'kern' feature, as a lookup (left glyph, right glyph) -> units."""
    gpos = font["GPOS"].table if "GPOS" in font else None
    singles: dict[tuple[str, str], int] = {}
    classed = []  # (coverage set, classDef1, classDef2, Class1Record)
    if gpos is None:
        return singles, classed
    idx = {i for fr in gpos.FeatureList.FeatureRecord if fr.FeatureTag == "kern" for i in fr.Feature.LookupListIndex}
    for i in sorted(idx):
        lookup = gpos.LookupList.Lookup[i]
        for st in lookup.SubTable:
            if lookup.LookupType == 9:
                st = st.ExtSubTable
            if getattr(st, "LookupType", 2) != 2:
                continue
            cov = st.Coverage.glyphs
            if st.Format == 1:
                for first, ps in zip(cov, st.PairSet):
                    for pvr in ps.PairValueRecord:
                        v = getattr(pvr.Value1, "XAdvance", 0) if pvr.Value1 else 0
                        singles.setdefault((first, pvr.SecondGlyph), v)
            elif st.Format == 2:
                classed.append((set(cov), st.ClassDef1.classDefs, st.ClassDef2.classDefs, st.Class1Record))
    return singles, classed


@lru_cache(maxsize=None)
def _font(weight: int, italic: bool):
    name = FONT_FILES.get((weight, italic))
    if not name:
        raise GenError(f"no bundled font for weight {weight}{' italic' if italic else ''}")
    f = TTFont(FONT_DIR / name)
    return f.getBestCmap(), f["hmtx"].metrics, f["head"].unitsPerEm, _kern_pairs(f)


def _kern(pairs, left: str, right: str) -> int:
    singles, classed = pairs
    if (left, right) in singles:
        return singles[(left, right)]
    for cov, cd1, cd2, rec in classed:
        if left in cov:
            vr = rec[cd1.get(left, 0)].Class2Record[cd2.get(right, 0)].Value1
            return getattr(vr, "XAdvance", 0) if vr else 0
    return 0


def text_width(text: str, size: float, weight: int = 400, spacing: float = 0.0, italic: bool = False) -> float:
    """Advance width of `text` in the bundled Inter, with pair kerning (as browsers and librsvg render it)."""
    cmap, hmtx, upem, pairs = _font(weight, italic)
    total, prev = 0, None
    for ch in text:
        glyph = cmap.get(ord(ch))
        if glyph is None:
            raise GenError(f"character {ch!r} (U+{ord(ch):04X}) in {text!r} is not in the bundled Inter subset")
        total += hmtx[glyph][0] + (_kern(pairs, prev, glyph) if prev else 0)
        prev = glyph
    return total * size / upem + spacing * len(text)


def wrap(text: str, size: float, width: float, weight: int = 400) -> list[str]:
    """Break text into lines that fit `width` (measured, unlike the estimate in tools/steps)."""
    lines, line = [], ""
    for word in text.split():
        cand = f"{line} {word}".strip()
        if not line or text_width(cand, size, weight) <= width:
            line = cand
        else:
            lines.append(line)
            line = word
    return lines + [line] if line else lines


def lines_of(value, size: float, width: float, weight: int = 400) -> list[str]:
    """A list in the data is taken as explicit lines; a string is wrapped to `width`."""
    return list(value) if isinstance(value, list) else wrap(value, size, width, weight)


# --------------------------------------------------------------------------------------------- matrix


def load_matrix() -> dict:
    return yaml.safe_load(MATRIX.read_text(encoding="utf-8"))


def language_count(matrix: dict) -> int:
    """LANGUAGES from the component, cross-checked with lang/*.yaml and the matrix (never hardcoded)."""
    init = ROOT / "components" / "geberit_aquaclean" / "__init__.py"
    tree = ast.parse(init.read_text(encoding="utf-8"))
    langs = next(
        (
            ast.literal_eval(node.value)
            for node in tree.body
            if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "LANGUAGES" for t in node.targets)
        ),
        None,
    )
    if not langs:
        raise GenError("LANGUAGES not found in components/geberit_aquaclean/__init__.py")
    missing = [c for c in langs if not (init.parent / "lang" / f"{c}.yaml").exists()]
    if missing:
        raise GenError(f"LANGUAGES lists codes without a lang file: {missing}")
    claimed = ((matrix.get("build_checks") or {}).get("languages") or {}).get("count")
    if claimed is not None and claimed != len(langs):
        raise GenError(f"matrix says {claimed} languages, the component has {len(langs)}; fix support/matrix.yaml")
    return len(langs)


def model_families(matrix: dict) -> str:
    fams: list[str] = []
    for m in matrix["models"].values():
        if m["status"] in MATRIX_OK:
            fam = m["name"].replace("AquaClean ", "").split()[0]
            if fam not in fams:
                fams.append(fam)
    if not fams:
        raise GenError("the matrix has no validated or supported model")
    return " · ".join(fams)


def recommended_board(matrix: dict, names: dict) -> str:
    for key, b in (matrix.get("boards") or {}).items():
        if b.get("status") == "validated" and str(b.get("note", "")).lower().startswith("recommended"):
            if key not in names:
                raise GenError(f"board '{key}' has no display name in overview.yaml board_names")
            return names[key]
    raise GenError("the matrix has no validated board marked 'recommended'")


def facts(cfg: dict, matrix: dict) -> dict:
    return {
        "models": model_families(matrix),
        "languages": language_count(matrix),
        "recommended_board": recommended_board(matrix, cfg.get("board_names", {})),
    }


def check_routes(cfg: dict, matrix: dict) -> None:
    status = matrix.get("routes_status", {})
    items = cfg["routes"]["items"]
    drawn = [it["route"] for it in items if it.get("route")]
    for r in drawn:
        st = (status.get(r) or {}).get("status")
        if st not in MATRIX_OK:
            raise GenError(f"route '{r}' is drawn but support/matrix.yaml marks it {st or 'absent'}")
    for r in ("native_api", "mqtt"):
        if r not in drawn:
            raise GenError(f"the native API and MQTT are two equal routes; '{r}' is missing from the image")
    for it in items:
        names = [c["text"] for c in it.get("clients", [])]
        if it.get("route") and names and all(c == "Home Assistant" for c in names):
            raise GenError(f"route '{it['route']}' lists only Home Assistant; name other systems too")


# --------------------------------------------------------------------------------------------- data access


class Ctx:
    """Resolves colours, placeholders and assets, and collects <defs>."""

    def __init__(self, cfg: dict, fact: dict, i18n: dict | None = None):
        self.cfg = cfg
        self.fact = fact
        self.font = cfg["font"]
        self.colors = cfg["colors"]
        self.symbols: dict[str, str] = {}
        self.texts: list[str] = []
        self.i18n = i18n
        self.strings = (i18n or {}).get("strings") or {}
        self.same = set((i18n or {}).get("same") or [])
        self.used: set[str] = set()

    def c(self, name: str) -> str:
        if name.startswith("#"):
            return name
        if name not in self.colors:
            raise GenError(f"unknown colour '{name}'")
        return self.colors[name]

    def _tr(self, key: str):
        """The translation of an English string (unchanged when no translation is active)."""
        if self.i18n is None or key in self.same:
            self.used.add(key)
            return key
        if key not in self.strings:
            raise GenError(f"[{self.i18n['lang']}] no translation for {key!r}; add it to strings: (or same:)")
        self.used.add(key)
        val = self.strings[key]
        flat = " ".join(val) if isinstance(val, list) else val
        if set(re.findall(r"{\w+}", key)) != set(re.findall(r"{\w+}", flat)):
            raise GenError(f"[{self.i18n['lang']}] {flat!r} must keep the placeholders of {key!r}")
        return val

    def _fmt(self, text: str) -> str:
        try:
            out = text.format_map(self.fact)
        except KeyError as e:
            raise GenError(f"unknown placeholder {e} in {text!r}") from None
        self.texts.append(out)
        return out

    def t(self, text: str) -> str:
        val = self._tr(text)
        if isinstance(val, list):
            raise GenError(f"{text!r} is a single line; translate it as a string, not a list")
        return self._fmt(val)

    def tl(self, value):
        """A text that may be explicit lines (a list) or a string to wrap; translated as a whole."""
        val = self._tr(" ".join(value) if isinstance(value, list) else value)
        if self.i18n is None and isinstance(value, list):
            val = value
        return [self._fmt(v) for v in val] if isinstance(val, list) else self._fmt(val)

    def fit(self, s: str, size: float, weight: int, room: float, where: str, spacing: float = 0.0) -> str:
        if text_width(s, size, weight, spacing) > room + 0.05:
            lang = f"[{self.i18n['lang']}] " if self.i18n else ""
            raise GenError(f"{lang}{s!r} ({where}) is wider than its {room:.0f}px; shorten it")
        return s

    # ----- icons and logos

    def icon(self, ref: str, x: float, y: float, size: float, color: str, stroke: str = "2") -> str:
        kind, _, name = ref.partition(":")
        if kind == "ui":
            path = UI_DIR / f"{name}.svg"
        elif kind == "lucide":
            path = LUCIDE_DIR / f"{name}.svg"
            if not LUCIDE_DIR.exists():
                raise GenError("Lucide icons need `npm install` in tools/steps")
        else:
            raise GenError(f"icon '{ref}' must start with ui: or lucide:")
        if not path.exists():
            raise GenError(f"unknown icon '{ref}'")
        src = re.sub(r"<!--.*?-->", "", path.read_text(encoding="utf-8"), flags=re.S)
        body = re.search(r"<svg[^>]*>(.*)</svg>", src, re.S).group(1).strip()
        body = re.sub(r"\s*\n\s*", "", body)
        col = self.c(color)
        scale = size / 24
        tf = f"translate({n(x)},{n(y)})" + ("" if abs(scale - 1) < 1e-9 else f" scale({scale:.4f})")
        return (
            f'<g transform="{tf}" fill="none" stroke="{col}" stroke-width="{stroke}" stroke-linecap="round" '
            f'stroke-linejoin="round" color="{col}">{body}</g>'
        )

    def logo(self, ref: str, x: float, y: float, size: float) -> str:
        kind, _, name = ref.partition(":")
        if kind != "logo":
            raise GenError(f"logo '{ref}' must start with logo:")
        if BANNED_LOGOS.search(name):
            raise GenError(f"'{name}': Bluetooth, Wi-Fi, USB, Apple and Geberit logos are not used (CLAUDE.md)")
        path = BRAND_DIR / name
        if not path.exists():
            raise GenError(f"unknown logo '{ref}' (expected in images/brands/)")
        sid = "logo-" + re.sub(r"[^a-z0-9]+", "-", path.stem.lower())
        if sid not in self.symbols:
            mime = "image/png" if path.suffix == ".png" else "image/svg+xml"
            uri = f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode()
            self.symbols[sid] = (
                f'<symbol id="{sid}" viewBox="0 0 100 100"><image width="100" height="100" '
                f'preserveAspectRatio="xMidYMid meet" href="{uri}"/></symbol>'
            )
        return f'<use href="#{sid}" x="{n(x)}" y="{n(y)}" width="{n(size)}" height="{n(size)}"/>'

    def mark(self, item: dict, cx: float, cy: float, size: float, color: str) -> str:
        """An item's icon or logo, centred on (cx, cy)."""
        if "logo" in item:
            lg = item["logo"]
            ref, sz = (lg["file"], lg.get("size", size)) if isinstance(lg, dict) else (lg, size)
            return self.logo(ref, cx - sz / 2, cy - sz / 2, sz)
        return self.icon(item["icon"], cx - size / 2, cy - size / 2, size, color)

    # ----- primitives

    def text(self, x, y, s, size, weight=400, color="ink", anchor="start", spacing=None, extra="") -> str:
        ls = f' letter-spacing="{spacing:g}"' if spacing else ""
        return (
            f'<text x="{n(x)}" y="{n(y)}" font-family="{self.font}" font-size="{size:g}" font-weight="{weight}" '
            f'fill="{self.c(color)}" text-anchor="{anchor}"{ls}{extra}>{esc(s)}</text>'
        )


def illustration(il: dict) -> str:
    """Embed one of images/illustrations as a nested <svg>, scaled to width `w`."""
    rel, x, y, w = il["file"], il["x"], il["y"], il["w"]
    src = (ROOT / rel).read_text(encoding="utf-8")
    root = re.search(r"<svg\b([^>]*)>", src)
    vb = re.search(r'viewBox="([^"]+)"', root.group(1))
    if not vb:
        raise GenError(f"{rel} has no viewBox")
    vx, vy, vw, vh = (float(v) for v in vb.group(1).split())
    inner = src[root.end() : src.rindex("</svg>")].strip("\n")
    h = w * vh / vw
    ov = ' overflow="visible"' if il.get("overflow") == "visible" else ""
    return f'<svg x="{n(x)}" y="{n(y)}" width="{n(w)}" height="{n(h)}" viewBox="{vb.group(1)}"{ov}>\n{inner}\n</svg>'


# --------------------------------------------------------------------------------------------- sections


def panel(cx: Ctx, p: dict, fill="white", stroke_width="1.5", rx=18) -> str:
    return (
        f'<rect x="{n(p["x"])}" y="{n(p["y"])}" width="{n(p["w"])}" height="{n(p["h"])}" rx="{rx}" '
        f'fill="{cx.c(fill)}" stroke="{cx.c(p["border"])}" stroke-width="{stroke_width}" filter="url(#shadow)"/>'
    )


def tag(cx: Ctx, t: dict, x: float, y: float) -> list[str]:
    """Coloured label sitting on the top edge of a panel."""
    label = cx.t(t["text"])
    w = text_width(label, 14, 700, 1.2) + 28
    if w > t.get("max_w", 1e9):
        raise GenError(f"tag {label!r} is wider than its panel; shorten it")
    return [
        f'<rect x="{n(x)}" y="{n(y - 15)}" width="{n(w)}" height="30" rx="8" fill="{cx.c(t["color"])}"/>',
        cx.text(x + w / 2, y + 5, label, 14, 700, "white", "middle", 1.2),
    ]


def header(cx: Ctx, cfg: dict) -> list[str]:
    W = cfg["canvas"]["width"]
    out = []
    ti = cfg["title"]
    parts = [(cx.t(p["text"]), p["color"]) for p in ti["parts"]]
    widths = [text_width(s, ti["size"], 700) for s, _ in parts]
    x = (W - sum(widths)) / 2
    for (s, col), w in zip(parts, widths):
        out.append(cx.text(x, ti["y"], s, ti["size"], 700, col))
        x += w
    st = cfg["subtitle"]
    sub = cx.fit(cx.t(st["text"]), st["size"], 400, W - 80, "subtitle")
    out.append(cx.text(W / 2, st["y"], sub, st["size"], 400, st["color"], "middle"))

    pl = cfg["pills"]
    items = [(it, cx.t(it["text"])) for it in pl["items"]]
    widths = [76 + text_width(s, 18, 500) for _, s in items]
    row = sum(widths) + pl["gap"] * (len(items) - 1)
    if row > W - 80:
        raise GenError(f"the pills are {row:.0f}px wide, more than the {W - 80}px available; shorten them")
    x = (W - row) / 2
    for (it, s), w in zip(items, widths):
        out.append(
            f'<rect x="{n(x)}" y="{pl["y"]}" width="{n(w)}" height="50" rx="12" fill="{cx.c("white")}" '
            f'stroke="{cx.c("line")}" filter="url(#shadow)"/>'
        )
        out.append(cx.icon(it["icon"], x + 20, pl["y"] + 13, 24, it["color"]))
        out.append(cx.text(x + 56, pl["y"] + 31.5, s, 18, 500))
        x += w + pl["gap"]
    return out


def toilet(cx: Ctx, cfg: dict) -> list[str]:
    d = cfg["toilet"]
    p = d["panel"]
    mid = p["x"] + p["w"] / 2
    out = [panel(cx, p), *tag(cx, {**d["tag"], "max_w": p["w"] - 44}, p["x"] + 22, p["y"])]
    il = d["illustration"]
    out.append(illustration(il))
    out.append("")
    out.append(cx.text(mid, d["name"]["y"], cx.t(d["name"]["text"]), 22, 700, "ink", "middle"))
    out.append(cx.text(mid, d["models"]["y"], cx.t(d["models"]["text"]), 15, 400, "muted", "middle"))
    nt = d["note"]
    bx, by, bw = p["x"] + 18, nt["box"]["y"], p["w"] - 36
    out.append(
        f'<rect x="{n(bx)}" y="{n(by)}" width="{n(bw)}" height="{n(nt["box"]["h"])}" rx="12" '
        f'fill="{cx.c(nt["box"]["fill"])}"/>'
    )
    out.append(cx.icon(nt["icon"], bx + 14, by + 14, 22, nt["color"], "2.00"))
    tx = bx + 46
    note = lines_of(cx.tl(nt["text"]), 15, bx + bw - tx - 12, 500)
    if len(note) > 3:
        raise GenError(f"the toilet note needs {len(note)} lines, 3 fit: {note}")
    for i, ln in enumerate(note):
        out.append(cx.text(tx, by + 29 + 20 * i, cx.fit(ln, 15, 500, bx + bw - tx - 8, "toilet note"), 15, 500))
    return out


def link(cx: Ctx, cfg: dict) -> list[str]:
    lk = cfg["link"]
    col = cx.c(lk["color"])
    return [
        f'<line x1="{n(lk["x1"])}" y1="{n(lk["y"])}" x2="{n(lk["x2"])}" y2="{n(lk["y"])}" stroke="{col}" '
        f'stroke-width="4" stroke-linecap="round"/>',
        f'<circle cx="{n(lk["x1"])}" cy="{n(lk["y"])}" r="6" fill="{col}"/>',
        cx.text(lk["label_x"], lk["y"] - 18, cx.fit(cx.t(lk["label"]), 15, 700, lk["max_w"], "link"), 15, 700,
                lk["color"], "middle"),
    ] + [
        cx.text(lk["label_x"], lk["y"] + 23 + 16 * i, ln, 12.5, 500, "muted", "middle")
        for i, ln in enumerate(wrap(cx.t(lk["sublabel"]), 12.5, lk["max_w"], 500))
    ]


def bridge(cx: Ctx, cfg: dict) -> list[str]:
    d = cfg["bridge"]
    p = d["panel"]
    mid = p["x"] + p["w"] / 2
    lk = cfg["link"]
    out = [panel(cx, p), f'<circle cx="{n(lk["x2"])}" cy="{n(lk["y"])}" r="6" fill="{cx.c(lk["color"])}"/>']
    out += tag(cx, {**d["tag"], "max_w": p["w"] - 44 - 160}, p["x"] + 22, p["y"])

    bd = d["badge"]
    label = cx.t(bd["text"])
    bw = 32 + text_width(label, 13.5, 600) + 12
    bx, by = p["x"] + p["w"] - 16 - bw, p["y"] + 17
    out.append(
        f'<rect x="{n(bx)}" y="{n(by)}" width="{n(bw)}" height="28" rx="14" fill="{cx.c("soft")}" '
        f'stroke="{cx.c("line")}"/>'
        + cx.icon(bd["icon"], bx + 10, by + 6, 16, "body")
        + cx.text(bx + 32, by + 19, label, 13.5, 600, "slate")
    )

    il = d["illustration"]
    out.append(illustration(il))
    rd = d["radio"]
    col = cx.c(rd["color"])
    for x0, sweep, sign in ((rd["left_x"], 0, -1), (rd["right_x"], 1, 1)):
        for r, op, shift in ((12, "0.9", 0), (22, "0.55", 10)):
            out.append(
                f'<path d="M{n(x0 + sign * shift)} {n(rd["cy"] - r)} a{r} {r} 0 0 {sweep} 0 {2 * r}" fill="none" '
                f'stroke="{col}" stroke-width="2.5" stroke-linecap="round" opacity="{op}"/>'
            )
    out.append(cx.text(mid, d["name"]["y"], cx.t(d["name"]["text"]), 26, 700, "ink", "middle"))
    out.append(cx.text(mid, d["sub"]["y"], cx.fit(cx.t(d["sub"]["text"]), 15, 400, p["w"] - 32, "bridge"), 15, 400,
                       "muted", "middle"))

    cl = d["checklist"]
    for i, item in enumerate(cl["items"]):
        y = cl["y"] + cl["step"] * i
        ccx, ccy = p["x"] + 42, y - 5
        out.append(f'<circle cx="{n(ccx)}" cy="{n(ccy)}" r="12" fill="{cx.c(cl["color"] + "_tint")}"/>')
        out.append(cx.icon("ui:check", ccx - 9, ccy - 9, 18, cl["color"], "2.60"))
        out.append(cx.text(p["x"] + 66, y, cx.fit(cx.t(item), 16, 500, p["w"] - 66 - 14, "checklist"), 16, 500))
    return out


def routes(cx: Ctx, cfg: dict) -> list[str]:
    R = cfg["routes"]
    box, pn = R["box"], R["panel"]
    bridge_right = cfg["bridge"]["panel"]["x"] + cfg["bridge"]["panel"]["w"]
    box_mid = box["x"] + box["w"] / 2
    out = []
    trans = {t["id"]: t for t in R["transports"]}
    # Column headers and the lines from the bridge to the transport boxes.
    for t in R["transports"]:
        col = cx.c(t["color"])
        hd = t["header"]
        label = cx.t(hd["text"])
        w = 18 + 6 + text_width(label, 13, 700, 1.5)
        x = box_mid - w / 2
        out.append(cx.icon(hd["icon"], x, hd["y"] - 14, 18, t["color"]))
        out.append(cx.text(x + 24, hd["y"], label, 13, 700, t["color"], spacing=1.5))
        mine = [it["cy"] for it in R["items"] if it["transport"] == t["id"]]
        if "trunk_x" in t:
            tx = t["trunk_x"]
            out.append(
                f'<line x1="{n(bridge_right)}" y1="{n(t["exit_y"])}" x2="{n(tx)}" y2="{n(t["exit_y"])}" '
                f'stroke="{col}" stroke-width="4"/>'
            )
            out.append(
                f'<line x1="{n(tx)}" y1="{n(min(mine))}" x2="{n(tx)}" y2="{n(max(mine))}" stroke="{col}" '
                f'stroke-width="4" stroke-linecap="round"/>'
            )
        out.append(f'<circle cx="{n(bridge_right)}" cy="{n(t["exit_y"])}" r="6" fill="{col}"/>')

    tops = []
    for it in R["items"]:
        t = trans[it["transport"]]
        cy = it["cy"]
        tcol, col = cx.c(t["color"]), cx.c(it["color"])
        start = t.get("trunk_x", bridge_right)
        out.append(
            f'<line x1="{n(start)}" y1="{n(cy)}" x2="{n(box["x"])}" y2="{n(cy)}" stroke="{tcol}" stroke-width="4" '
            f'stroke-linecap="round"/>'
        )
        out.append(
            f'<line x1="{n(box["x"] + box["w"])}" y1="{n(cy)}" x2="{n(pn["x"])}" y2="{n(cy)}" stroke="{col}" '
            f'stroke-width="3" stroke-dasharray="2 7" stroke-linecap="round"/>'
        )
        # transport box
        bt = cy - box["h"] / 2
        out.append(
            f'<rect x="{n(box["x"])}" y="{n(bt)}" width="{n(box["w"])}" height="{n(box["h"])}" rx="14" '
            f'fill="{cx.c(it["color"] + "_tint")}" stroke="{cx.c(it["color"] + "_border")}" stroke-width="1.5"/>'
        )
        out.append(cx.mark(it, box["x"] + 25, cy, 26, it["color"]))
        room = box["w"] - 48 - 6
        title, tsize = cx.t(it["title"]), it.get("title_size", box.get("title_size", 16))
        sub = cx.t(it["sub"]) if it.get("sub") else ""
        ssize = it.get("sub_size", box.get("sub_size", 12.5))
        for s, size, weight in ((title, tsize, 700), (sub, ssize, 500)):
            if s and text_width(s, size, weight) > room:
                raise GenError(f"{s!r} is wider than its route box ({room}px); shorten it or widen routes.box")
        # Without a subtitle the title sits on the box's centre line.
        out.append(cx.text(box["x"] + 48, cy - 3 if sub else cy + tsize * 0.36, title, tsize, 700))
        if sub:
            out.append(cx.text(box["x"] + 48, cy + 17, sub, ssize, 500, "body"))
        # client panel
        top = cy - pn["h"] / 2
        tops.append(top)
        out.append(
            f'<rect x="{n(pn["x"])}" y="{n(top)}" width="{n(pn["w"])}" height="{n(pn["h"])}" rx="16" '
            f'fill="{cx.c("white")}" stroke="{cx.c(it["color"] + "_border")}" stroke-width="1.5" '
            f'filter="url(#shadow)"/>'
        )
        out.append(
            f'<rect x="{n(pn["x"])}" y="{n(top + 14)}" width="5" height="{n(pn["h"] - 28)}" rx="2.5" fill="{col}"/>'
        )
        out.append(f'<circle cx="{n(pn["x"])}" cy="{n(cy)}" r="5" fill="{col}"/>')
        dy = it.get("items_dy", 21)
        x = pn["x"] + 24
        for c in it["clients"]:
            label = cx.t(c["text"])
            out.append(cx.mark(c, x + 10, top + dy + 10, 20, it["color"]))
            out.append(cx.text(x + 26, top + dy + 16, label, 14.25, 600))
            x += 26 + text_width(label, 14.25, 600) + 16
        if x - 16 > pn["x"] + pn["w"] - 16:
            raise GenError(f"clients of '{it['title']}' don't fit their panel; shorten or widen it")
        cap = cx.fit(cx.t(it["caption"]), 14, 400, pn["w"] - 40, "client panel caption")
        out.append(cx.text(pn["x"] + 24, top + it.get("caption_dy", 76), cap, 14, 400, "body"))

    out += tag(cx, {**R["tag"], "max_w": pn["w"] - 44}, pn["x"] + 22, tops[0])
    bk = R["bracket"]
    y1, y2 = tops[0] + 8, tops[-1] + pn["h"] - 8
    out.append(
        f'<path d="M{n(bk["x"])} {n(y1)} q6 0 6 6 V{n(y2 - 6)} q0 6 -6 6" fill="none" stroke="{cx.c(bk["color"])}" '
        f'stroke-width="2.5" stroke-linecap="round"/>'
    )
    lx, ly = bk["x"] + 30, (y1 + y2) / 2
    out.append(
        cx.text(lx, ly, cx.fit(cx.t(bk["label"]), 15, 700, y2 - y1 - 40, "bracket", 0.5), 15, 700, bk["color"], "middle",
                0.5, f' transform="rotate(90 {n(lx)} {n(ly)})"')
    )
    return out


def highlights(cx: Ctx, cfg: dict) -> list[str]:
    hl = cfg["highlights"]
    b = hl["band"]
    out = [
        f'<rect x="{n(b["x"])}" y="{n(b["y"])}" width="{n(b["w"])}" height="{n(b["h"])}" rx="18" '
        f'fill="{cx.c("white")}" stroke="{cx.c("line")}" filter="url(#shadow)"/>'
    ]
    colw = b["w"] / len(hl["items"])
    for i, it in enumerate(hl["items"]):
        x0 = b["x"] + colw * i
        if i:
            out.append(
                f'<line x1="{n(x0)}" y1="{n(b["y"] + 24)}" x2="{n(x0)}" y2="{n(b["y"] + 104)}" '
                f'stroke="{cx.c("line")}"/>'
            )
        out.append(
            f'<rect x="{n(x0 + 26)}" y="{n(b["y"] + 26)}" width="48" height="48" rx="12" '
            f'fill="{cx.c(it["color"])}" opacity="0.1"/>'
        )
        out.append(cx.icon(it["icon"], x0 + 38, b["y"] + 38, 24, it["color"]))
        room = colw - 90 - 12
        out.append(cx.text(x0 + 90, b["y"] + 46, cx.fit(cx.t(it["title"]), 17, 700, room, "highlight title"), 17, 700))
        lines = lines_of(cx.tl(it["text"]), 14.5, colw - 90 - 16)
        if len(lines) > 2:
            raise GenError(f"highlight text needs {len(lines)} lines, 2 fit: {lines}")
        for j, ln in enumerate(lines):
            cx.fit(ln, 14.5, 400, room, "highlight text")
            out.append(cx.text(x0 + 90, b["y"] + 72 + 21 * j, ln, 14.5, 400, "body"))
    return out


def caption_lines(cx: Ctx, cfg: dict) -> list[str]:
    cp = cfg.get("caption") or {}
    if not cp.get("enabled"):
        return []
    if cx.i18n is not None:
        raise GenError("the matrix caption exists in English only; disable it for translations")
    spec = importlib.util.spec_from_file_location("support_generate", ROOT / "support" / "generate.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    sentence = mod.tested_sentence(load_matrix())
    cx.texts.append(sentence)
    return wrap(sentence, cp.get("size", 14), cfg["canvas"]["width"] - 80)


# --------------------------------------------------------------------------------------------- document


def lint(cx: Ctx) -> None:
    for s in cx.texts:
        m = BANNED_WORDS.search(s)
        if m and m.group(1).lower().startswith("prox"):
            raise GenError(f"{s!r}: our product is a bridge, never a proxy (CLAUDE.md)")
        if m:
            raise GenError(f"{s!r}: use 'shower', keep 'anal' out of the documentation (CLAUDE.md)")
        if INTERNALS.search(s):
            raise GenError(f"{s!r} looks like a protocol internal; images name functions, never internals")


def build(cfg: dict, matrix: dict, i18n: dict | None = None) -> str:
    check_routes(cfg, matrix)
    cx = Ctx(cfg, facts(cfg, matrix), i18n)
    W, H = cfg["canvas"]["width"], cfg["canvas"]["height"]

    body = header(cx, cfg) + toilet(cx, cfg) + link(cx, cfg) + bridge(cx, cfg) + routes(cx, cfg)
    body += highlights(cx, cfg)
    cap = caption_lines(cx, cfg)
    if cap:
        cp = cfg["caption"]
        y = H + 15
        for ln in cap:
            body.append(cx.text(W / 2, y, ln, cp.get("size", 14), 400, cp.get("color", "muted"), "middle"))
            y += 20
        H = int(y + 5)
    alt = cx.t(cfg.get("alt", "overview"))
    lint(cx)
    if i18n is not None:
        unused = sorted((set(cx.strings) | cx.same) - cx.used)
        if unused:
            raise GenError(f"[{i18n['lang']}] entries no longer in overview.yaml: {unused}")

    defs = [
        '<filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">',
        f'  <feDropShadow dx="0" dy="4" stdDeviation="8" flood-color="{cx.c("ink")}" flood-opacity="0.07"/>',
        "</filter>",
        '<linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">',
        f'  <stop offset="0" stop-color="{cx.c("white")}"/><stop offset="1" stop-color="#F1F5F9"/>',
        "</linearGradient>",
        *(cx.symbols[k] for k in sorted(cx.symbols)),
    ]
    lang = i18n["lang"] if i18n else "en"
    head = (
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{W}" '
        f'height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="{esc(alt)}" xml:lang="{lang}">'
    )
    src = f"tools/overview/i18n/{lang}.yaml and " if i18n else ""
    note = f"<!-- Generated by tools/overview/generate.py from {src}tools/overview/overview.yaml. Do not edit by hand. -->"
    return "\n".join(
        [head, note, "<defs>", *defs, "</defs>", f'<rect width="{W}" height="{H}" fill="url(#bg)"/>', *body, "</svg>"]
    ) + "\n"


def readme_snippet(cfg: dict) -> str:
    src = cfg["png"]["path"] if cfg.get("png") else cfg["output"]
    return f'<p align="center">\n  <img src="{src}" alt="{html.escape(cfg["alt"])}" width="100%">\n</p>\n'


def spliced_readme(snippet: str) -> str | None:
    text = README.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        return None
    head, rest = text.split(BEGIN, 1)
    tail = rest.split(END, 1)[1]
    return f"{head}{BEGIN}\n{snippet}{END}{tail}"


def render_png(svg_path: Path, png_path: Path, scale: float) -> None:
    exe = shutil.which("rsvg-convert")
    if not exe:
        raise GenError("rsvg-convert (librsvg) is needed for the PNG; install it or pass --no-png")
    with tempfile.TemporaryDirectory() as tmp:
        conf = Path(tmp) / "fonts.conf"
        # Only the bundled fonts: the PNG never depends on what happens to be installed.
        conf.write_text(
            '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig>'
            f"<dir>{FONT_DIR}</dir><cachedir>{tmp}/cache</cachedir>"
            "<config><rescan><int>0</int></rescan></config></fontconfig>"
        )
        env = {**os.environ, "FONTCONFIG_FILE": str(conf)}
        subprocess.run([exe, "-z", str(scale), "-o", str(png_path), str(svg_path)], check=True, env=env)


def variants(cfg: dict, only: str | None) -> list[tuple[str, dict | None, str, str | None]]:
    """(lang, i18n, svg path, png path) for English and every translation in overview.yaml."""
    png = cfg.get("png") or {}
    out = [("en", None, cfg["output"], png.get("path"))]
    tr = cfg.get("translations") or {}
    stem = Path(cfg["output"]).stem
    for lang in tr.get("languages", []):
        if only and only != lang:
            continue
        src = HERE / "i18n" / f"{lang}.yaml"
        if not src.exists():
            raise GenError(f"translation '{lang}' is listed but {src.relative_to(ROOT)} is missing")
        i18n = {"lang": lang, **(yaml.safe_load(src.read_text(encoding="utf-8")) or {})}
        base = f"{tr['dir']}/{stem}-{lang}"
        out.append((lang, i18n, f"{base}.svg", f"{base}.png" if png else None))
    if only:
        out = [v for v in out if v[0] == only]
        if not out:
            raise GenError(f"no language '{only}' in overview.yaml translations")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("config", nargs="?", default=str(HERE / "overview.yaml"))
    ap.add_argument("--check", action="store_true", help="write nothing; exit 1 if the outputs are stale")
    ap.add_argument("--no-png", action="store_true", help="skip the PNGs")
    ap.add_argument("--lang", help="only this language (en, or one of the translations)")
    args = ap.parse_args()

    cfg = yaml.safe_load(Path(args.config).read_text(encoding="utf-8"))
    matrix = load_matrix()
    built = [(lang, build(cfg, matrix, i18n), svg, png) for lang, i18n, svg, png in variants(cfg, args.lang)]
    snippet = readme_snippet(cfg)
    readme = spliced_readme(snippet)
    with_readme = args.lang in (None, "en")

    if args.check:
        stale = [svg for _, text, svg, _ in built if not (ROOT / svg).exists() or (ROOT / svg).read_text(encoding="utf-8") != text]
        if with_readme:
            if readme is None:
                stale.append(f"README.md (missing {BEGIN} / {END})")
            elif readme != README.read_text(encoding="utf-8"):
                stale.append("README.md")
        if stale:
            print("Out of date: " + ", ".join(stale) + ". Run: python3 tools/overview/generate.py", file=sys.stderr)
            return 1
        print(f"overview: up to date ({', '.join(lang for lang, *_ in built)})")
        return 0

    for _, text, svg, png in built:
        out = ROOT / svg
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
        print(f"wrote {svg}")
        if png and not args.no_png:
            render_png(out, ROOT / png, cfg["png"].get("scale", 3))
            print(f"wrote {png}")
    if with_readme and readme is not None:
        README.write_text(readme, encoding="utf-8")
        print(snippet, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
