#!/usr/bin/env bash
# Packages a built emerald-isle for a release: the executable, its assets folder, README.txt,
# LICENSE and CREDITS.md, as emerald-isle-<version>-<platform>.zip (macOS, Windows) or .tar.gz
# (Linux) in the output directory.
#
# Usage: tools/release/package.sh <version> <macos|windows|linux> <dir with the executable> <output dir>
set -euo pipefail

version=$1
platform=$2
bin_dir=$3
out_dir=$4

root=$(cd "$(dirname "$0")/../.." && pwd)
name="emerald-isle-${version}-${platform}"
exe=emerald-isle
[ "$platform" = windows ] && exe=emerald-isle.exe

[ -f "$bin_dir/$exe" ] || { echo "No $exe in $bin_dir" >&2; exit 1; }
[ -d "$bin_dir/assets/shaders" ] || { echo "No assets next to $exe in $bin_dir" >&2; exit 1; }

mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/$name"
cp "$bin_dir/$exe" "$stage/$name/"
cp -R "$bin_dir/assets" "$stage/$name/assets"
cp "$root/LICENSE" "$root/CREDITS.md" "$stage/$name/"
sed "s/@VERSION@/${version}/g" "$root/tools/release/README.txt" > "$stage/$name/README.txt"

cd "$stage"
case "$platform" in
    macos) zip -qry "$out_dir/$name.zip" "$name" ;;          # -y keeps symlinks, -r the folder
    windows) 7z a -tzip -bso0 "$out_dir/$name.zip" "$name" ;;
    linux) tar -czf "$out_dir/$name.tar.gz" "$name" ;;
    *) echo "Unknown platform: $platform" >&2; exit 1 ;;
esac
echo "Packaged $(ls "$out_dir"/"$name".*)"
