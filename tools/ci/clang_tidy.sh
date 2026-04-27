#!/usr/bin/env bash

# Exit on any error
set -e

echo "Running clang-tidy check..."

find engine/ \
    \( -path "engine/external/*" -o -path "engine/generated/*" -o -path "engine/ffi/*" \) -prune \
    -o -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.inl" \) -print0 \
    | xargs -0 clang-tidy --warnings-as-errors='*'

echo "Style check passed!"

