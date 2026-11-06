#!/bin/sh
# bump_version.sh — increment version.xcconfig after each successful build.
#
#   MODULE_VERSION: minor version +1, patch resets to 0   (0.1.0 -> 0.2.0)
#   BUILD_COUNT:    +1                                    (1 -> 2 -> 3 ...)
#
# Runs as an Xcode post-build phase (see project.yml) and is safe to run
# manually.  See docs/versioning.md for the policy.

set -e

CFG="$(dirname "$0")/../version.xcconfig"

[ -f "$CFG" ] || { echo "version.xcconfig not found at $CFG" >&2; exit 1; }

VER=$(grep '^MODULE_VERSION' "$CFG" | sed 's/.*= *//')
CNT=$(grep '^BUILD_COUNT' "$CFG" | sed 's/.*= *//')

MAJOR=$(echo "$VER" | cut -d. -f1)
MINOR=$(echo "$VER" | cut -d. -f2)
PATCH=$(echo "$VER" | cut -d. -f3)

NEW_MINOR=$((MINOR + 1))
NEW_VER="${MAJOR}.${NEW_MINOR}.0"
NEW_CNT=$((CNT + 1))

cat > "$CFG" <<EOF
// version.xcconfig — single source of truth for mt76wm versioning.
//
// Policy (see docs/versioning.md):
//   - MODULE_VERSION: bumped 0.1.0 -> 0.2.0 -> 0.3.0 ... on every build
//   - BUILD_COUNT:    +1 on every build (exposed as CFBundleVersion)
//
// The bump happens in a post-build step (scripts/bump_version.sh), so the
// values here always describe the NEXT build.  Do not edit by hand.

MODULE_VERSION = ${NEW_VER}
BUILD_COUNT = ${NEW_CNT}
EOF

echo "bump_version: ${VER} (build ${CNT}) -> ${NEW_VER} (build ${NEW_CNT})"
