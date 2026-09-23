#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../.."

BUILD_DIR="${1:-build/clang-tidy}"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "Error: compile_commands.json not found in '$BUILD_DIR'. Pass the build directory as the first argument."
    exit 1
fi

EXPECTED_TIDY_VERSION=23
TIDY_VERSION="$(clang-tidy --version | grep -oE 'version [0-9]+' | cut -d' ' -f2)"
echo "Using $(command -v clang-tidy) (LLVM ${TIDY_VERSION:-unknown})"
if [[ "$TIDY_VERSION" != "$EXPECTED_TIDY_VERSION" ]]; then
    echo "Error: expected clang-tidy $EXPECTED_TIDY_VERSION, found ${TIDY_VERSION:-unknown}. Run inside 'nix develop'."
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

