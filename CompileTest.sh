#!/usr/bin/env zsh
set -euo pipefail

COMPILER="./snlc"
TEST_DIR="./tests"
OUT_DIR="./tmp"

if [[ ! -x "$COMPILER" ]]; then
    echo "Error: compiler '$COMPILER' not found or not executable."
    exit 1
fi

if [[ ! -d "$TEST_DIR" ]]; then
    echo "Error: tests directory '$TEST_DIR' not found."
    exit 1
fi

mkdir -p "$OUT_DIR"

SUCCESS=0
FAILED=0

for file in "$TEST_DIR"/*.snl; do
    [[ -e "$file" ]] || continue

    base=$(basename "$file" .snl)
    output="$OUT_DIR/$base.asm"

    echo "Compiling $file -> $output"

    if "$COMPILER" "$file" -o "$output"; then
        echo "OK: $base"
        ((++SUCCESS))
    else
        echo "FAILED: $base"
        ((++FAILED))
    fi
done

echo
echo "Summary:"
echo "  Success: $SUCCESS"
echo "  Failed : $FAILED"
echo "  Output : $OUT_DIR"

if [[ $FAILED -ne 0 ]]; then
    exit 1
fi