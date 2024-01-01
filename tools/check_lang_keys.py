#!/usr/bin/env python3
"""Verify every translation file has exactly the same keys as en.yaml.

Values are translations and may differ; the *key structure* must not. This catches a key
renamed in one file but not the others, a typo, or a missing/extra entry — the kind of drift
that silently breaks entity translations.

Usage: python3 tools/check_lang_keys.py
Exit code 1 if any file diverges from en.yaml.
"""
from __future__ import annotations

import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("PyYAML is required: pip install pyyaml")

LANG_DIR = Path(__file__).resolve().parent.parent / "components" / "geberit_aquaclean" / "lang"
REFERENCE = "en.yaml"


def key_paths(node, prefix=""):
    """Collect a comparable set of structural paths (dict keys and list lengths)."""
    paths = set()
    if isinstance(node, dict):
        for k, v in node.items():
            p = f"{prefix}.{k}" if prefix else str(k)
            paths.add(p)
            paths |= key_paths(v, p)
    elif isinstance(node, list):
        paths.add(f"{prefix}[len={len(node)}]")
    return paths


def main() -> int:
    ref_file = LANG_DIR / REFERENCE
    if not ref_file.exists():
        print(f"reference {ref_file} not found", file=sys.stderr)
        return 1
    ref = key_paths(yaml.safe_load(ref_file.read_text(encoding="utf-8")))

    failures = 0
    for f in sorted(LANG_DIR.glob("*.yaml")):
        if f.name == REFERENCE:
            continue
        keys = key_paths(yaml.safe_load(f.read_text(encoding="utf-8")))
        missing = ref - keys
        extra = keys - ref
        if missing or extra:
            failures += 1
            print(f"\n{f.name}: DIVERGES from {REFERENCE}")
            for m in sorted(missing):
                print(f"  missing: {m}")
            for e in sorted(extra):
                print(f"  extra:   {e}")

    if failures:
        print(f"\n{failures} language file(s) diverge from {REFERENCE}.", file=sys.stderr)
        return 1
    print(f"OK: all language files match {REFERENCE} ({len(ref)} keys).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
