#!/bin/sh
# Print one version's CHANGELOG.md summary: the single line right under its
# "## [x.y.z] - date" heading, before any "###" subsection or bullet. Boards
# show it as the update's notes, which hold at most 127 bytes.
# Usage: changelog_summary.sh <version, with or without a leading v> [file]
# Exits 1 if there is no summary line or it is longer than 127 bytes.
set -eu
v="${1#v}"
file="${2:-CHANGELOG.md}"
max=127
line=$(scripts/changelog_section.sh "$v" "$file" | head -1)
case "$line" in
    ''|'#'*|'- '*)
        echo "no summary line for $v in $file: put one line (at most $max bytes) right under its heading" >&2
        exit 1 ;;
esac
bytes=$(printf '%s' "$line" | wc -c)
if [ "$bytes" -gt "$max" ]; then
    echo "summary for $v is $bytes bytes; boards show at most $max: $line" >&2
    exit 1
fi
printf '%s\n' "$line"
