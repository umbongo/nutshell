#!/usr/bin/env bash
# check-version.sh BASE_SHA
#
# The merge gate for main (CLAUDE.md, "Branches, pull requests and the merge
# gate"). Every change that produces a new binary carries a new version
# number, the number is spelled the same way everywhere, and the committed
# binary was actually built from it:
#
#   1. src/ui/resource.h       APP_VERSION        "X.Y.Z"
#   2. src/ui/resource.h       APP_VERSION_BINARY  X,Y,Z,0
#   3. README.md               **Version**: X.Y.Z      (a leading "v" is allowed)
#   4. build/win/nutshell.exe  FileVersion in its version resource == X.Y.Z
#
# The patch component never goes past 99: after 1.0.99 comes 1.1.0.
#
# If the diff BASE..HEAD touches src/**, the Makefile or nutshell.rc, the
# version must be strictly greater than the one at BASE and the exe must be
# part of the diff (a build input changed, so the binary must have been
# rebuilt). Otherwise no bump is required and the script only checks internal
# consistency -- including 4, so a stale binary fails even a docs-only change.
#
# UPX keeps the version resource uncompressed (icons, manifest and version
# info stay readable by Explorer), which is what lets a hosted Linux runner
# read FileVersion out of the packed exe without a PE parser.

set -u

BASE="${1:-}"
if [ -z "$BASE" ]; then
    echo "check-version: usage: check-version.sh BASE_SHA" >&2
    exit 1
fi

RESOURCE_H="src/ui/resource.h"
README="README.md"
EXE="build/win/nutshell.exe"

fail() { echo "check-version: FAIL: $*" >&2; exit 1; }

# ---- parse ----------------------------------------------------------------

[ -f "$RESOURCE_H" ] || fail "$RESOURCE_H not found"
[ -f "$README" ]     || fail "$README not found"
[ -f "$EXE" ]        || fail "$EXE not found -- the binary is tracked and must be committed"

# APP_VERSION "X.Y.Z"
app_version=$(sed -n 's/^[[:space:]]*#define[[:space:]]\+APP_VERSION[[:space:]]\+"\([0-9]\+\.[0-9]\+\.[0-9]\+\)".*/\1/p' "$RESOURCE_H" | head -n 1)
[ -n "$app_version" ] || fail "no '#define APP_VERSION \"X.Y.Z\"' in $RESOURCE_H"

# APP_VERSION_BINARY X,Y,Z,0
binary_raw=$(sed -n 's/^[[:space:]]*#define[[:space:]]\+APP_VERSION_BINARY[[:space:]]\+\([0-9]\+[[:space:]]*,[[:space:]]*[0-9]\+[[:space:]]*,[[:space:]]*[0-9]\+[[:space:]]*,[[:space:]]*[0-9]\+\).*/\1/p' "$RESOURCE_H" | head -n 1)
[ -n "$binary_raw" ] || fail "no '#define APP_VERSION_BINARY X,Y,Z,0' in $RESOURCE_H"
binary_clean=$(echo "$binary_raw" | tr -d '[:space:]')
binary_version=$(echo "$binary_clean" | cut -d, -f1-3 | tr ',' '.')
binary_fourth=$(echo "$binary_clean" | cut -d, -f4)

# **Version**: X.Y.Z  (optional leading v, anything after)
readme_version=$(sed -n 's/^[[:space:]]*\*\*Version\*\*:[[:space:]]*v\?\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p' "$README" | head -n 1)
[ -n "$readme_version" ] || fail "no '**Version**: X.Y.Z' line in $README"

# FileVersion from the exe's version resource (shared with release.yml).
exe_version=$(bash "$(dirname "$0")/exe-version.sh" "$EXE") || \
    fail "cannot read FileVersion from $EXE"

echo "check-version: $RESOURCE_H APP_VERSION        = $app_version"
echo "check-version: $RESOURCE_H APP_VERSION_BINARY = $binary_clean"
echo "check-version: $README     **Version**        = $readme_version"
echo "check-version: $EXE FileVersion    = $exe_version"

# ---- internal consistency -------------------------------------------------

[ "$binary_version" = "$app_version" ] || \
    fail "APP_VERSION_BINARY ($binary_clean) does not match APP_VERSION ($app_version); expected ${app_version//./,},0"
[ "$binary_fourth" = "0" ] || \
    fail "APP_VERSION_BINARY must end in ,0 (got $binary_clean)"
[ "$readme_version" = "$app_version" ] || \
    fail "$README says $readme_version but $RESOURCE_H says $app_version"
[ "$exe_version" = "$app_version" ] || \
    fail "$EXE was built as $exe_version but $RESOURCE_H says $app_version -- rebuild (make clean && make release) and commit the exe"

patch=$(echo "$app_version" | cut -d. -f3)
if [ "$patch" -gt 99 ]; then
    fail "patch component $patch exceeds 99 in $app_version; after 1.0.99 comes 1.1.0, not 1.0.100"
fi

# ---- does this change produce a new binary? -------------------------------

if ! changed=$(git diff --name-only "$BASE"..HEAD 2>/dev/null); then
    fail "cannot diff $BASE..HEAD (is the checkout shallow? use fetch-depth: 0)"
fi

build_touched=$(echo "$changed" | grep -E '^(src/|Makefile$|nutshell\.rc$)' || true)

if [ -z "$build_touched" ]; then
    echo "check-version: no changes under src/, Makefile or nutshell.rc in $BASE..HEAD -- no bump required."
    echo "check-version: PASS ($app_version, consistent in all four places)"
    exit 0
fi

echo "check-version: build inputs changed in $BASE..HEAD:"
echo "$build_touched" | sed 's/^/check-version:   /'

echo "$changed" | grep -qx "$EXE" || \
    fail "build inputs changed but $EXE did not: rebuild (make clean && make release) and commit the exe"

# ---- compare with the base version ----------------------------------------

base_resource=$(git show "$BASE:$RESOURCE_H" 2>/dev/null) || \
    fail "cannot read $RESOURCE_H at $BASE"
base_version=$(echo "$base_resource" | sed -n 's/^[[:space:]]*#define[[:space:]]\+APP_VERSION[[:space:]]\+"\([0-9]\+\.[0-9]\+\.[0-9]\+\)".*/\1/p' | head -n 1)
[ -n "$base_version" ] || fail "no APP_VERSION in $RESOURCE_H at $BASE"

echo "check-version: base ($BASE) APP_VERSION = $base_version"

greater=0
for i in 1 2 3; do
    h=$(echo "$app_version"  | cut -d. -f$i)
    b=$(echo "$base_version" | cut -d. -f$i)
    if [ "$h" -gt "$b" ]; then greater=1; break; fi
    if [ "$h" -lt "$b" ]; then greater=0; break; fi
done

if [ "$greater" -ne 1 ]; then
    fail "this change touches build inputs, so APP_VERSION must be bumped: $app_version is not greater than the base $base_version. Bump it in $RESOURCE_H (APP_VERSION and APP_VERSION_BINARY) and $README."
fi

echo "check-version: PASS ($base_version -> $app_version)"
exit 0
