#!/bin/sh
# Installs Toast Engine for the current user (no root needed)
set -eu

src="$(cd "$(dirname "$0")" && pwd)"
dest="${XDG_DATA_HOME:-$HOME/.local/share}/toast-engine"

if [ "$src" != "$dest" ]; then
	rm -rf "$dest.old"
	[ -d "$dest" ] && mv "$dest" "$dest.old"
	mkdir -p "$(dirname "$dest")"
	cp -r "$src" "$dest"
	rm -rf "$dest.old"
fi

rm -f "$dest/install.sh"
chmod +x "$dest/editor/editor" 2>/dev/null || true
for exe in "$dest"/reflection_generator/reflection_generator "$dest"/packer/* "$dest"/crash_reporter/crash_reporter "$dest"/toast_engine/bin/log_server; do
	[ -f "$exe" ] && chmod +x "$exe" 2>/dev/null || true
done

"$dest/editor/editor" --register

echo "Toast Engine installed to $dest"
echo "Open a new shell (or log in again) for TOAST_PATH to take effect"
