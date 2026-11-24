#!/usr/bin/env bash
# Copy the ioBroker adapter from iobroker/ in this repository to its own repository (ioBroker.aquaclean-bridge),
# which ioBroker needs for the adapter list on iobroker.com.
#
# iobroker/ here is the source: edit the adapter only here, never in the other repository (its changes would be
# overwritten by the next sync). The script only copies files; committing and pushing stay with you.
#
# Usage:
#   tools/sync_iobroker.sh [--generate] [path to the ioBroker.aquaclean-bridge checkout]
#
#   --generate   first regenerate lib/entities.json and the entity reference from the component
#                (needs a python3 with ESPHome installed, or set PYTHON=/path/to/python)
#   path         default: ioBroker.aquaclean-bridge next to this repository
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/iobroker"
GENERATE=0
TARGET=""
for arg in "$@"; do
    case "$arg" in
        --generate) GENERATE=1 ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) TARGET="$arg" ;;
    esac
done
TARGET="${TARGET:-$ROOT/../ioBroker.aquaclean-bridge}"

fail() { echo "sync_iobroker: $*" >&2; exit 1; }

[ -f "$SRC/io-package.json" ] || fail "no adapter in $SRC"
[ -d "$TARGET/.git" ] || fail "$TARGET is not a git checkout of ioBroker.aquaclean-bridge"
TARGET="$(cd "$TARGET" && pwd)"
case "$TARGET/" in "$ROOT"/*) fail "the target must not lie inside this repository" ;; esac

origin="$(git -C "$TARGET" config --get remote.origin.url || true)"
case "$(printf '%s' "$origin" | tr '[:upper:]' '[:lower:]')" in
    *iobroker.aquaclean-bridge|*iobroker.aquaclean-bridge.git) ;;
    *) fail "the origin of $TARGET is '$origin', not ioBroker.aquaclean-bridge" ;;
esac

# Refuse a target with uncommitted work: the copy would mix with it, and deletions could lose it.
if [ -n "$(git -C "$TARGET" status --porcelain)" ]; then
    git -C "$TARGET" status --short >&2
    fail "$TARGET has uncommitted changes: commit or discard them first"
fi

if [ "$GENERATE" = 1 ]; then
    echo "Regenerating the entity table and the entity reference…"
    (cd "$SRC" && "${PYTHON:-python3}" tools/generate.py --bridge "$ROOT")
fi

ver_src="$(node -p "require('$SRC/package.json').version" 2>/dev/null || echo '?')"
ver_dst="$(node -p "require('$TARGET/package.json').version" 2>/dev/null || echo '?')"

# Mirror iobroker/ into the target: empty it (the target is clean, so git can restore anything), then copy.
# Never touched in the target: git data, installed packages, the dev server.
KEEP_RE='^(\.git|node_modules|\.dev-server)$'
for entry in "$TARGET"/* "$TARGET"/.[!.]* "$TARGET"/..?*; do
    [ -e "$entry" ] || continue
    printf '%s\n' "$(basename "$entry")" | grep -Eq "$KEEP_RE" && continue
    rm -rf "$entry"
done
(cd "$SRC" && tar -cf - \
    --exclude './node_modules' --exclude 'node_modules' \
    --exclude '.dev-server' --exclude '__pycache__' --exclude '.DS_Store' .) | (cd "$TARGET" && tar -xf -)

echo
echo "Copied iobroker/ (version $ver_src) to $TARGET (was $ver_dst)."
if [ -z "$(git -C "$TARGET" status --porcelain)" ]; then
    echo "Nothing changed: the repositories are in sync."
    exit 0
fi
git -C "$TARGET" status --short
echo
echo "Next, in $TARGET:"
echo "  npm ci && npm test            # check before committing"
echo "  git diff                      # review; 'git restore .' undoes the copy"
echo "  git add -A && git commit -m \"…\" && git push"
