#!/usr/bin/env bash
set -euo pipefail

mapfile -t files < <(find engine \
	\( -path "engine/generated" -o -path "engine/generated/*" \
	-o -path "engine/external" -o -path "engine/external/*" \
	-o -path "engine/ffi" -o -path "engine/ffi/*" \) -prune \
	-o -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.inl" \) -print | sort)

if [[ ${#files[@]} -eq 0 ]]; then
	echo "No engine files matched formatting filters."
	exit 0
fi

printf '%s\0' "${files[@]}" | xargs -0 -n 1 -P "$(nproc)" clang-format -i --style=file:.clang-format

if git diff --quiet -- "${files[@]}"; then
	echo "Formatting check passed."
	exit 0
fi

echo "::error::Formatting changes detected. Run clang-format locally and push the results."
exit 1
