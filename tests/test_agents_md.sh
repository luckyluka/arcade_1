#!/bin/sh
# ROADMAP_V4 card 4.9: AGENTS.md — the map for a fresh agent.
#
# Acceptance (pinned):
#   1. every relative link in AGENTS.md resolves to a file that exists;
#   2. the file stays within the card's budget of 80 lines;
#   3. it names the canonical documents (CHEATSHEET = guest API, SDK = CLI,
#      GAME_DEV_GUIDE = workflow, SPEC = machine) and where the goldens and
#      the tests live.
#
# Host-side only: it reads a markdown file and the file system.
#
# Usage: test_agents_md.sh [repo-root]
set -eu

ROOT=${1:-$(cd "$(dirname "$0")/.." && pwd)}
AGENTS="$ROOT/AGENTS.md"

fail() { echo "test-agents-md: FAILED — $*" >&2; exit 1; }
CHECKS=0

[ -f "$AGENTS" ] || fail "missing $AGENTS"

# --- 1) every relative link resolves --------------------------------------
links=$(sed -n 's/.*](\([^)]*\)).*/\1/p' "$AGENTS" | sort -u)
[ -n "$links" ] || fail "AGENTS.md has no links at all"
for l in $links; do
    case "$l" in
        http:*|https:*|mailto:*|'#'*) continue ;;   # not a file link
    esac
    [ -e "$ROOT/$l" ] || fail "AGENTS.md links to '$l', which does not exist"
    CHECKS=$((CHECKS + 1))
done
echo "  ok: every relative link in AGENTS.md resolves"

# --- 2) the line budget ----------------------------------------------------
lines=$(wc -l < "$AGENTS" | tr -d ' ')
[ "$lines" -le 80 ] || fail "AGENTS.md is $lines lines, the budget is 80"
CHECKS=$((CHECKS + 1))
echo "  ok: AGENTS.md is $lines lines (budget 80)"

# --- 3) the canonical documents are named ----------------------------------
for doc in CHEATSHEET.md SDK.md GAME_DEV_GUIDE.md SPEC.md; do
    grep -q "\[$doc\]" "$AGENTS" || fail "AGENTS.md does not link $doc"
    CHECKS=$((CHECKS + 1))
done
grep -q 'tests/golden/' "$AGENTS" || fail "AGENTS.md does not say where the goldens live"
grep -q 'tests/roms/' "$AGENTS" || fail "AGENTS.md does not say where the ROMs live"
grep -q 'docs/experiments/' "$AGENTS" || fail "AGENTS.md does not point at the findings"
CHECKS=$((CHECKS + 3))
echo "  ok: the canonical docs, the goldens and the findings are all named"

echo "test-agents-md: ok ($CHECKS checks, card 4.9)"
