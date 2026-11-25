#!/usr/bin/env python3
"""Make links to collapsed README sections open the section on arrival.

Usage:  python3 tools/readme_anchors.py [--check] [README.md]

A browser opens a closed <details> by itself when the link's fragment is the exact id of an element
hidden inside it. GitHub renders `<a id="x">` as `id="user-content-x"`, so for every collapsed
section whose <summary> holds a Markdown heading:

1. The heading stays plain (`## Hardware`), so GitHub's outline (the list icon above the README)
   shows clean titles. A back link (`<sub>[↑ back to top](#contents)</sub>`) goes on its own line
   under the heading, still inside the <summary>.
2. A hidden anchor `<a id="open-<heading slug>">` sits right after `</summary>`. It can't share
   the heading's own id, or the browser would target the always-visible heading and not open
   anything.
3. In-page links to a collapsed section point to `#user-content-open-<slug>`; links to an anchor
   or heading inside a collapsed body point to `#user-content-<name>`.

Plain links from elsewhere (`#hardware`) still land on the heading, just with the section closed.
Back links (`#installation`, `#contents`) point outside any <details> and are left alone.
Idempotent; tools/steps/generate.py runs it after splicing the step sections, CI runs --check.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LONE_ANCHOR = re.compile(r'^<a (?:name|id)="([^"]+)"></a>\s*$')
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*$")
HEADING_WITH_BACK = re.compile(r"^(#{1,6}\s+.*?)\s*(<sub>\[[^\]]*\]\(#[^)]+\)</sub>)\s*$")
ANCHOR_IN = re.compile(r'<a (?:name|id)="([^"]+)"></a>')
PREFIX = "open-"


def slug(heading):
    """GitHub's heading id: lowercase, drop tags and punctuation, spaces -> '-'."""
    s = re.sub(r"<[^>]+>", "", heading).strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def restructure(lines):
    """Rebuild each collapsed section: plain heading, back link below it, hidden open- anchor."""
    out, i, sections = [], 0, set()
    while i < len(lines):
        line = lines[i]
        # A section anchor in front of <details> is replaced by the heading's own id.
        if LONE_ANCHOR.match(line) and i + 1 < len(lines) and lines[i + 1].strip() == "<details>":
            i += 1
            continue
        if line.strip() == "<details>" and i + 1 < len(lines) and lines[i + 1].strip() == "<summary>":
            j = i + 2
            while j < len(lines) and not lines[j].lstrip().startswith("</summary>"):
                j += 1
            if j < len(lines):
                summary, title = [], None
                for s in lines[i + 2:j]:
                    m = HEADING_WITH_BACK.match(s)
                    if m:
                        summary += [m.group(1), "", m.group(2)]
                        s = m.group(1)
                    else:
                        summary.append(s)
                    h = HEADING.match(s)
                    if h and title is None:
                        title = h.group(2)
                if title is not None:
                    sl = slug(title)
                    sections.add(sl)
                    out += ["<details>", "<summary>", *summary, f'</summary><a id="{PREFIX}{sl}"></a>']
                    i = j + 1
                    continue
        out.append(line)
        i += 1
    return out, sections


def hidden_targets(lines):
    """Anchors and headings in the body of a <details> (hidden while it is closed)."""
    targets, depth, in_summary, fence = set(), 0, False, False
    for line in lines:
        s = line.strip()
        if s.startswith("```"):
            fence = not fence
            continue
        if fence:
            continue
        depth += s.count("<details>") - s.count("</details>")
        if "<summary" in s:
            in_summary = True
        if "</summary>" in s:
            in_summary = False
            targets.update(ANCHOR_IN.findall(s.split("</summary>", 1)[1]))
            continue
        if depth > 0 and not in_summary:
            targets.update(ANCHOR_IN.findall(s))
            h = HEADING.match(s)
            if h:
                targets.add(slug(h.group(2)))
    return targets


def retarget(frag, sections, hidden):
    base = frag.removeprefix("user-content-")
    if base.startswith(PREFIX) and base[len(PREFIX):] in sections:
        base = base[len(PREFIX):]
    if base in sections:
        return f"user-content-{PREFIX}{base}"
    if base in hidden:
        return f"user-content-{base}"
    return frag


def fix(text):
    lines, sections = restructure(text.split("\n"))
    hidden = hidden_targets(lines)
    text = "\n".join(lines)
    text = re.sub(r"\]\(#([^)\s]+)\)", lambda m: f"](#{retarget(m.group(1), sections, hidden)})", text)
    return re.sub(r'href="#([^"]+)"', lambda m: f'href="#{retarget(m.group(1), sections, hidden)}"', text)


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    readme = Path(args[0]) if args else ROOT / "README.md"
    text = readme.read_text()
    new = fix(text)
    if "--check" in sys.argv:
        if new != text:
            sys.exit(f"{readme}: links to collapsed sections are not up to date; run tools/readme_anchors.py")
        return
    if new != text:
        readme.write_text(new)


if __name__ == "__main__":
    main()
