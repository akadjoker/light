#!/usr/bin/env sh
# Fails if libLight/ includes forbidden STL headers, or stdio outside libLight/platform/.
set -eu

cd "$(dirname "$0")/.."

fail=0

STL_PATTERN='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](vector|string|unordered_map|unordered_set|map|set|list|deque|queue|stack|array|tuple|memory|functional|algorithm|optional|variant|iostream|ostream|istream|sstream|fstream|iomanip|regex|thread|mutex|future|filesystem)[>"]'

hits=$(grep -rnE "$STL_PATTERN" libLight/ 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "ERROR: forbidden STL header in libLight/:"
    echo "$hits"
    fail=1
fi

STDIO_PATTERN='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](stdio\.h|cstdio)[>"]'
hits=$(grep -rnE "$STDIO_PATTERN" libLight/ 2>/dev/null | grep -v '^libLight/src/platform/' || true)
if [ -n "$hits" ]; then
    echo "ERROR: stdio outside libLight/platform/:"
    echo "$hits"
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "check_includes: OK"
fi
exit "$fail"
