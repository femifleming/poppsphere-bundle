#!/usr/bin/env bash
# Collects the built plug-ins, apps and manuals into dist/poppsphere-bundle-<os>/<product>/
# usage: ci/package.sh <macOS|Windows|Linux> <build dir>
set -euo pipefail
OS="$1"; B="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist/poppsphere-bundle-$OS"
rm -rf "$OUT"; mkdir -p "$OUT"

art() {   # artefact folder of a target (single- or multi-config generators)
    local sub="$1" tgt="$2"
    for d in "$B/$sub/${tgt}_artefacts/Release" "$B/$sub/${tgt}_artefacts"; do
        [ -d "$d/VST3" ] && { echo "$d"; return; }
    done
    echo "missing artefacts for $tgt" >&2; exit 1
}

product() {   # product sub-dir target manual-pdf
    local name="$1" sub="$2" tgt="$3" pdf="$4"
    local A; A="$(art "$sub" "$tgt")"
    local P="$OUT/$name"; mkdir -p "$P"
    cp -R "$A/VST3/$tgt.vst3" "$P/"
    case "$OS" in
        macOS)
            cp -R "$A/AU/$tgt.component" "$A/Standalone/$tgt.app" "$P/"
            [ -d "$A/VST/$tgt.vst" ] && cp -R "$A/VST/$tgt.vst" "$P/"
            for f in "$P"/*.app "$P"/*.component "$P"/*.vst3 "$P"/*.vst; do
                [ -e "$f" ] && codesign --force --deep -s - "$f"
            done ;;
        Windows)
            cp "$A/Standalone/$tgt.exe" "$P/"
            [ -f "$A/VST/$tgt.dll" ] && cp "$A/VST/$tgt.dll" "$P/" ;;
        Linux)
            cp "$A/Standalone/$tgt" "$P/"
            [ -d "$A/LV2/$tgt.lv2" ] && cp -R "$A/LV2/$tgt.lv2" "$P/"
            [ -f "$A/VST/lib$tgt.so" ] && cp "$A/VST/lib$tgt.so" "$P/$tgt.so"
            [ -f "$A/VST/$tgt.so" ] && cp "$A/VST/$tgt.so" "$P/" ;;
    esac
    cp "$ROOT/$pdf" "$P/$name Manual.pdf"
}

product openprocess openprocess openprocess "openprocess/docs/manual/openprocess Manual.pdf"
product openfield  openprocess openfield  "openprocess/docs/manual-field/openfield Manual.pdf"
product Quell       quell       Quell       "quell/docs/manual/manual.pdf"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$OUT/"
cp "$ROOT/INSTALL.txt" "$OUT/" 2>/dev/null || true

cd "$ROOT/dist"
case "$OS" in
    macOS)   ditto -c -k --keepParent "poppsphere-bundle-$OS" "poppsphere-bundle-$OS.zip" ;;
    Windows) 7z a -tzip -r "poppsphere-bundle-$OS.zip" "poppsphere-bundle-$OS" > /dev/null ;;
    Linux)   tar -czf "poppsphere-bundle-$OS.tar.gz" "poppsphere-bundle-$OS" ;;
esac
ls -la "$ROOT/dist"
