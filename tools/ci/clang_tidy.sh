#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../.."

BUILD_DIR="${1:-build/Debug}"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "Error: compile_commands.json not found in '$BUILD_DIR'. Pass the build directory as the first argument."
    exit 1
fi

echo "Running clang-tidy check..."

find engine/ \
    \( -path "engine/external" -o -path "engine/external/*" \
       -o -path "engine/generated" -o -path "engine/generated/*" \
       -o -path "engine/ffi" -o -path "engine/ffi/*" \) -prune \
    -o -type f -name "*.cpp" -print0 \
    | xargs -0 -n 1 -P "$(nproc)" clang-tidy -p "$BUILD_DIR" --warnings-as-errors='*' --header-filter='engine/(src|include)/.*'

echo "Style check passed!"

