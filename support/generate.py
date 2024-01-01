#!/usr/bin/env python3
"""Generate human/CI artifacts from the single source of truth support/matrix.yaml.

Outputs (relative to the repo root that contains support/matrix.yaml):
  - docs/SUPPORT.md            generated support matrix (do not edit by hand)
  - support/TEST_FORM.md       fillable per-model field-test form
  - support/_generated/caption.txt   canonical overview-image caption + README "tested" sentence

Works on both branches: it just reads whatever support/matrix.yaml is next to it. Nothing here is
authored by hand elsewhere — these outputs are derived. Run it; never edit the outputs directly.

Usage: python3 support/generate.py [--check]
  (default) writes the artifacts.
  --check   writes nothing; exits non-zero if any artifact is out of date (for CI, Phase 3).
"""
from __future__ import annotations
import sys
import pathlib
import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
MATRIX = ROOT / "support" / "matrix.yaml"
DISCLAIMER = "Independent open-source project, not affiliated with Geberit AG"
GEN = "<!-- generated from support/matrix.yaml by support/generate.py — do not edit by hand -->"


def load():
    return yaml.safe_load(MATRIX.read_text(encoding="utf-8"))


def _names(m, slugs):
    return [m["models"][s]["name"].replace("AquaClean ", "") for s in slugs]


def models_by_status(m, status):
    return [s for s, mm in m["models"].items() if mm["status"] == status]


def tested_sentence(m) -> str:
    """The one-line claim for the README and the overview image."""
    maturity = m["meta"].get("maturity")
    val = models_by_status(m, "validated")
    sup = models_by_status(m, "supported")
    parts = []
    if val:
        bits = []
        for s in val:
            v = next((v for v in m.get("validations", []) if v["id"] == m["models"][s].get("validation")), None)
            label = m["models"][s]["name"]
            if v:
                label += f" (device firmware {v['device_firmware']}, bridge {v['bridge_release']})"
            bits.append(label)
        parts.append("Validated on " + "; ".join(bits))
    if sup:
        clause = ", ".join(_names(m, sup)) + " supported"
        clause += "" if maturity else " via feature discovery"
        parts.append(clause)
    langs = (m.get("build_checks") or {}).get("languages", {}).get("count")
    if langs:
        parts.append(f"{langs} languages")
    rv = [r for r, rs in m.get("routes_status", {}).items() if rs["status"] == "validated"]
    rs_ = [r for r, rs in m.get("routes_status", {}).items() if rs["status"] == "supported"]
    if rv or rs_:
        seg = []
        if rv:
            seg.append(", ".join(rv) + " validated")
        if rs_:
            seg.append(", ".join(rs_) + " supported")
        parts.append("routes: " + "; ".join(seg))
    parts.append(DISCLAIMER)
    sentence = " · ".join(parts)
    if maturity:
        sentence = f"{maturity.capitalize()} — " + sentence
    return sentence


def support_md(m) -> str:
    o = [GEN, "", "# Support matrix", "",
         f"Branch **{m['meta']['branch']}**"
         + (f" · maturity **{m['meta']['maturity']}**" if m["meta"].get("maturity") else ""),
         "", f"> {tested_sentence(m)}", "", "## Models", "",
         "| Model | SAP prefix | Status | Evidence / note |", "|---|---|---|---|"]
    for s, mm in m["models"].items():
        ev = mm.get("note", "")
        if mm["status"] == "validated":
            v = next((v for v in m.get("validations", []) if v["id"] == mm.get("validation")), None)
            if v:
                ev = f"bridge {v['bridge_release']}, device fw {v['device_firmware']}"
        pref = ", ".join(mm.get("sap_prefix") or []) or "—"
        o.append(f"| {mm['name']} | {pref} | `{mm['status']}` | {ev} |")
    # per-model surfaces where enumerated
    for s, mm in m["models"].items():
        surf = mm.get("surfaces")
        if not surf:
            continue
        o += ["", f"### {mm['name']} — surfaces", "", "| Surface | Status |", "|---|---|"]
        for k, st in surf.items():
            o.append(f"| {m['surfaces'].get(k, k)} | `{st}` |")
    # routes
    o += ["", "## Integration routes", "", "| Route | Status |", "|---|---|"]
    for r, rs in m.get("routes_status", {}).items():
        o.append(f"| {m['routes'].get(r, r)} | `{rs['status']}` |")
    # boards
    if m.get("boards"):
        o += ["", "## Boards", "", "| Board | Status | Note |", "|---|---|---|"]
        for b, bs in m["boards"].items():
            o.append(f"| {b} | `{bs['status']}` | {bs.get('note','')} |")
    # device firmware ledger
    if m.get("device_firmware_seen"):
        o += ["", "## AquaClean device firmware seen", "", "| Firmware | Model | Status | Since | Note |",
              "|---|---|---|---|---|"]
        for fw, d in m["device_firmware_seen"].items():
            o.append(f"| `{fw}` | {d.get('model','')} | `{d.get('status','')}` | {d.get('since','')} | {d.get('note','')} |")
    else:
        o += ["", "## AquaClean device firmware seen", "", "_None captured yet._"]
    # build checks
    if m.get("build_checks"):
        o += ["", "## Build checks (device-independent)", "", "| Check | Value | Status | How |", "|---|---|---|---|"]
        for name, c in m["build_checks"].items():
            val = c.get("count", "")
            o.append(f"| {name} | {val} | `{c.get('status','')}` | {c.get('check','')} |")
    return "\n".join(o) + "\n"


def test_form_md(m) -> str:
    o = [GEN, "", "# Field-test form", "",
         "Fill **Result** with PASS / FAIL / N/A, plus the device firmware, bridge release and a log",
         "reference. A FAIL opens or updates the ticket named by **Key**. A PASS on real hardware is what",
         "promotes a surface from `supported` to `validated` in `support/matrix.yaml`.", ""]
    for s, mm in m["models"].items():
        surf = mm.get("surfaces")
        if not surf:
            o += [f"## {mm['name']}  (`{mm['status']}`)", "",
                  f"_{mm.get('note','feature set resolved by feature discovery')}_", ""]
            continue
        o += [f"## {mm['name']}", "",
              "| Key | Surface | Matrix | Result | Device fw | Bridge | Log / notes |",
              "|---|---|---|---|---|---|---|"]
        for k, st in surf.items():
            if st == "unsupported":
                continue
            o.append(f"| `{s}/{k}` | {m['surfaces'].get(k,k)} | `{st}` |  |  |  |  |")
        o.append("")
    # routes
    o += ["## Integration routes", "",
          "| Key | Route | Matrix | Result | Device fw | Bridge | Log / notes |",
          "|---|---|---|---|---|---|---|"]
    for r, rs in m.get("routes_status", {}).items():
        o.append(f"| `route/{r}` | {m['routes'].get(r,r)} | `{rs['status']}` |  |  |  |  |")
    o.append("")
    return "\n".join(o) + "\n"


ARTIFACTS = {
    "docs/SUPPORT.md": support_md,
    "support/TEST_FORM.md": test_form_md,
    "support/_generated/caption.txt": lambda m: tested_sentence(m) + "\n",
}


def main():
    check = "--check" in sys.argv[1:]
    m = load()
    stale = []
    for rel, fn in ARTIFACTS.items():
        p = ROOT / rel
        new = fn(m)
        old = p.read_text(encoding="utf-8") if p.exists() else None
        if check:
            if old != new:
                stale.append(rel)
        else:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(new, encoding="utf-8")
            print(f"wrote {rel}")
    if check:
        if stale:
            print("STALE (run support/generate.py):", ", ".join(stale), file=sys.stderr)
            sys.exit(1)
        print("up to date")
    else:
        print("\n--- overview caption / README tested line ---")
        print(tested_sentence(m))


if __name__ == "__main__":
    main()
