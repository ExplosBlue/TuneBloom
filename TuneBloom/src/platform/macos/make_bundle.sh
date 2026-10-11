#!/bin/sh
# Usage: make_bundle.sh <output-dir> <executable>
set -eu

outdir=$1
executable=$2

here=$(cd "$(dirname "$0")" && pwd)
content="$here/../../../workdir/content"
app="$outdir/TuneBloom.app"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
mkdir -p "$app/Contents/Resources"

cp "$executable" "$app/Contents/MacOS/TuneBloom"
cp "$here/Info.plist" "$app/Contents/Info.plist"
cp -R "$content" "$app/Contents/Resources/content"

if [ -f "$content/icon.png" ]; then
    staging=$(mktemp -d)
    iconset="$staging/icon.iconset"
    mkdir -p "$iconset"

    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$content/icon.png" \
            --out "$iconset/icon_${size}x${size}.png" >/dev/null
        sips -z "$((size * 2))" "$((size * 2))" "$content/icon.png" \
            --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done

    iconutil -c icns "$iconset" -o "$app/Contents/Resources/icon.icns"
    rm -rf "$staging"
fi

codesign --force --sign - "$app" >/dev/null 2>&1 || \
    echo "warning: could not ad-hoc sign $app"

echo "Built $app"
