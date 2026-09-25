#!/usr/bin/env bash
# Build and run every host-test project under test/host/ on ESP-IDF's linux
# target. Projects are discovered, so a new test directory runs in CI as soon
# as it exists.
#
#   test/host/run_all.sh              # all projects
#   test/host/run_all.sh board_test   # just these
#
# Every project is attempted even after one fails; the exit status is
# non-zero if any failed.
set -uo pipefail

cd "$(dirname "$0")"

if [ $# -gt 0 ]; then
    projects=("$@")
else
    projects=()
    for d in */; do
        [ -f "$d/CMakeLists.txt" ] && projects+=("${d%/}")
    done
fi

failed=()
for p in "${projects[@]}"; do
    echo "=== $p"
    if (cd "$p" && idf.py --preview set-target linux >/dev/null && idf.py build >/dev/null && "./build/$p.elf"); then
        echo "=== $p: passed"
    else
        echo "=== $p: FAILED"
        failed+=("$p")
    fi
done

if [ ${#failed[@]} -gt 0 ]; then
    echo "Failed: ${failed[*]}"
    exit 1
fi
echo "All ${#projects[@]} host-test project(s) passed."
