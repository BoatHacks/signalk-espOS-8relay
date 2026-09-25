#!/bin/sh
# Print the body of one version's CHANGELOG.md entry: everything between its
# "## [x.y.z]" heading and the next "## [" heading, without link references.
# Usage: changelog_section.sh <version, with or without a leading v> [file]
# Exits 1 if the version has no entry, or an empty one.
set -eu
v="${1#v}"
file="${2:-CHANGELOG.md}"
body=$(awk -v h="## [$v]" '
    index($0, h) == 1 { found = 1; next }
    found && /^## \[/ { exit }
    found && /^\[[^]]+\]: / { next }
    found { print }
' "$file")
# Trim blank lines at both ends.
body=$(printf '%s\n' "$body" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')
if [ -z "$body" ]; then
    echo "no CHANGELOG entry for $v in $file" >&2
    exit 1
fi
printf '%s\n' "$body"
