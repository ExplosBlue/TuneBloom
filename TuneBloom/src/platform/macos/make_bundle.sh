#!/bin/sh
# Usage: make_bundle.sh <output-dir> <executable> [<executable> ...]
set -eu

output_dir=$1
shift

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_root=$(cd "$script_dir/../../../.." && pwd)
content_dir="$repo_root/workdir/content"

app="$output_dir/TuneBloom.app"
executable="$app/Contents/MacOS/TuneBloom"
resources="$app/Contents/Resources"
info_plist="$app/Contents/Info.plist"

install_executable()
{
    if [ $# -gt 1 ]; then
        lipo -create "$@" -output "$executable"
    else
        cp "$1" "$executable"
    fi
}

install_icon()
{
    staging=$(mktemp -d)
    iconset="$staging/icon.iconset"
    mkdir "$iconset"

    for size in 16 32 128 256 512; do
        double=$((size * 2))
        sips -z "$size" "$size" "$1" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        sips -z "$double" "$double" "$1" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done

    iconutil -c icns "$iconset" -o "$resources/icon.icns"
    rm -rf "$staging"
}

rm -rf "$app"
mkdir -p "$(dirname "$executable")" "$resources"

install_executable "$@"
cp "$script_dir/Info.plist" "$info_plist"
cp -R "$content_dir" "$resources/content"
cp "$repo_root/LICENSE.txt" "$repo_root/THIRD_PARTY_LICENSES.txt" "$resources/"

if [ -n "${BUNDLE_VERSION:-}" ]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BUNDLE_VERSION" "$info_plist"
fi

if [ -f "$content_dir/icon.png" ]; then
    install_icon "$content_dir/icon.png"
fi

codesign --force --sign - "$app" >/dev/null 2>&1 || echo "warning: could not ad-hoc sign $app"

echo "Built $app"
