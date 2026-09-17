#!/bin/bash
# by Xein

scales=(
    "1x:16"
    "1.5x:24"
    "2x:32"
    "4x:64"
    "16x:128"
)

for scale in "${scales[@]}"; do
    dir="${scale%%:*}"
    size="${scale##*:}"

    echo "Generating icons for ${dir} (${size}x${size}px)"
    mkdir -p "$dir"

    for svg in *.svg; do
        [ -e "$svg" ] || continue

        filename=$(basename "$svg" .svg)

        magick -background none -density 300 "$svg" \
               -gravity center \
               -resize "${size}x${size}" \
               -extent "${size}x${size}" \
               "${dir}/${filename}.png"
    done
done

echo "Hai!! :3"
