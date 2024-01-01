#!/usr/bin/env python3
"""Generate clickable step cards (SVG) for a GitHub README.

Usage:  npm install && python3 generate.py [steps.json]   (from tools/steps)

Reads steps.json, writes one SVG per step to output_dir and prints a
README snippet: a row of cards that link to the matching step headings. The snippet also replaces
what stands between <!-- steps:begin --> and <!-- steps:end --> in README.md.
Icons are Lucide icon names (https://lucide.dev/icons) and are embedded
straight into the SVG, so nothing is loaded from the web.
"""
import json, re, sys, html
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent.parent  # paths in steps.json are relative to the repository root
ICON_DIR = HERE / "node_modules" / "lucide-static" / "icons"

W, H, PAD, R = 280, 230, 24, 16
FONT = "-apple-system,BlinkMacSystemFont,'Segoe UI','Noto Sans',Helvetica,Arial,sans-serif"


def wrap(text, size, width, bold=False):
    """Rough line wrapping based on average glyph width."""
    max_chars = int(width / (size * (0.58 if bold else 0.53)))
    lines, line = [], ""
    for word in text.split():
        cand = f"{line} {word}".strip()
        if len(cand) <= max_chars:
            line = cand
        else:
            lines.append(line)
            line = word
    return lines + [line] if line else lines


def icon_paths(name):
    f = ICON_DIR / f"{name}.svg"
    if not f.exists():
        sys.exit(f"Unknown icon '{name}' – pick one from https://lucide.dev/icons")
    src = re.sub(r"<!--.*?-->", "", f.read_text(), flags=re.S)
    body = re.search(r"<svg[^>]*>(.*)</svg>", src, re.S).group(1)
    return re.sub(r"\s*\n\s*", "\n    ", body.strip())


def slug(heading):
    """GitHub's anchor rules: lowercase, drop punctuation, spaces -> '-'."""
    s = heading.strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def card(n, step, accent):
    title_lines = wrap(step["title"], 18, W - 2 * PAD, bold=True)
    text_lines = wrap(step["text"], 13, W - 2 * PAD)
    y = PAD + 44 + 26  # icon block + gap -> baseline of "Step n"
    parts = [f'<text x="{PAD}" y="{y}" class="sub">STEP {n}</text>']
    y += 26
    for ln in title_lines:
        parts.append(f'<text x="{PAD}" y="{y}" class="title">{html.escape(ln)}</text>')
        y += 23
    y += 2
    for ln in text_lines:
        parts.append(f'<text x="{PAD}" y="{y}" class="text">{html.escape(ln)}</text>')
        y += 18
    h = max(H, y + PAD - 10)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" viewBox="0 0 {W} {h}" role="img" aria-label="Step {n}: {html.escape(step["title"])}">
  <style>
    .box   {{ fill:#ffffff; stroke:#d0d7de; }}
    .chip  {{ fill:{accent}; fill-opacity:.12; }}
    .icon  {{ stroke:{accent}; }}
    .sub   {{ font:600 12px {FONT}; letter-spacing:.08em; fill:{accent}; }}
    .title {{ font:700 18px {FONT}; fill:#1f2328; }}
    .text  {{ font:400 13px {FONT}; fill:#59636e; }}
    @media (prefers-color-scheme: dark) {{
      .box   {{ fill:#0d1117; stroke:#3d444d; }}
      .chip  {{ fill-opacity:.22; }}
      .title {{ fill:#f0f6fc; }}
      .text  {{ fill:#9198a1; }}
    }}
  </style>
  <rect class="box" x="1" y="1" width="{W-2}" height="{h-2}" rx="{R}" stroke-width="1.5"/>
  <rect class="chip" x="{PAD}" y="{PAD}" width="44" height="44" rx="11"/>
  <g class="icon" transform="translate({PAD+10} {PAD+10})" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    {icon_paths(step["icon"])}
  </g>
  {chr(10).join("  " + p for p in parts).strip()}
</svg>
'''


def main():
    cfg = json.loads(Path(sys.argv[1] if len(sys.argv) > 1 else HERE / "steps.json").read_text())
    out = ROOT / cfg["output_dir"]
    out.mkdir(parents=True, exist_ok=True)
    hashes = "#" * cfg.get("heading_level", 3)
    per_row = cfg.get("per_row", len(cfg["steps"]))
    width = f"{100 // per_row - 1}%"

    links, sections = [], []
    for n, step in enumerate(cfg["steps"], 1):
        (out / f"step-{n}.svg").write_text(card(n, step, cfg.get("accent", "#2f81f7")))
        heading = f"Step {n}: {step['title']}"
        links.append(f'  <a href="#{slug(heading)}"><img src="{cfg["output_dir"]}/step-{n}.svg" '
                     f'width="{width}" alt="{html.escape(heading)}"></a>')
        details = step.get("details", f"{step['text']}\n\n<!-- more details here -->")
        sections.append(f"{hashes} {heading}\n\n{details}\n")

    rows = [links[i:i + per_row] for i in range(0, len(links), per_row)]
    cards = "\n\n".join('<p align="center">\n' + "\n".join(r) + "\n</p>" for r in rows)
    snippet = cards + "\n\n" + "\n".join(sections)
    (HERE / "README-snippet.md").write_text(snippet)
    _splice(snippet)
    print(snippet)


def _splice(snippet):
    """Put the cards and step sections into README.md between the two markers."""
    readme = ROOT / "README.md"
    begin, end = "<!-- steps:begin -->", "<!-- steps:end -->"
    text = readme.read_text()
    if begin in text and end in text:
        head, rest = text.split(begin, 1)
        tail = rest.split(end, 1)[1]
        readme.write_text(f"{head}{begin}\n{snippet}{end}{tail}")


if __name__ == "__main__":
    main()
