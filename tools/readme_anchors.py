#!/usr/bin/env python3
"""Make links to collapsed README sections open the section on arrival.

Usage:  python3 tools/readme_anchors.py [--check] [README.md]

GitHub renders an anchor `<a id="x">` as `id="user-content-x"`. A browser opens a closed
<details> by itself when the link's fragment is the exact id of an element hidden inside it, so:

1. A section anchor that stands just before `<details>` is moved to right after `</summary>`
   (inside the collapsed body, as `<a id="...">`), so it is hidden while the section is closed.
2. Every in-page link to an anchor or heading inside a collapsed body is rewritten from
   `#x` to `#user-content-x`.

Links from elsewhere that still use the plain `#x` keep working: GitHub scrolls to the section,
it just stays closed. Back links (`#installation`, `#contents`) point outside any <details> and are
left alone. Idempotent; tools/steps/generate.py runs it after splicing the step sections.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

ANCHOR = re.compile(r'^<a (?:name|id)="([^"]+)"></a>\s*$')
HEADING = re.compile(r"^#{1,6}\s+(.*?)\s*$")


def slug(heading):
    """GitHub's heading id: lowercase, drop punctuation, spaces -> '-'."""
    s = re.sub(r"<[^>]+>", "", heading).strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def move_anchors(lines):
    """Move `<a name>` before `<details>` to just after its `</summary>`."""
    out, i = [], 0
    while i < len(lines):
        m = ANCHOR.match(lines[i])
        if m and i + 1 < len(lines) and lines[i + 1].strip() == "<details>":
            name = m.group(1)
            j = i + 1
            while j < len(lines) and not lines[j].lstrip().startswith("</summary>"):
                j += 1
            if j < len(lines):
                out.extend(lines[i + 1:j])
                out.append(f'</summary><a id="{name}"></a>')
                i = j + 1
                continue
        out.append(lines[i])
        i += 1
    return out


def hidden_targets(lines):
    """Anchors and headings that sit in the body of a <details> (hidden while it is closed)."""
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
            for name in re.findall(r'<a (?:name|id)="([^"]+)"></a>', s.split("</summary>", 1)[1]):
                targets.add(name)
            continue
        if depth > 0 and not in_summary:
            for name in re.findall(r'<a (?:name|id)="([^"]+)"></a>', s):
                targets.add(name)
            h = HEADING.match(s)
            if h:
                targets.add(slug(h.group(1)))
    return targets


def rewrite_links(text, targets):
    def md(m):
        return f"](#user-content-{m.group(1)})" if m.group(1) in targets else m.group(0)

    def html(m):
        return f'href="#user-content-{m.group(1)}"' if m.group(1) in targets else m.group(0)

    text = re.sub(r"\]\(#([^)\s]+)\)", md, text)
    return re.sub(r'href="#([^"]+)"', html, text)


def fix(text):
    lines = move_anchors(text.split("\n"))
    return rewrite_links("\n".join(lines), hidden_targets(lines))


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
