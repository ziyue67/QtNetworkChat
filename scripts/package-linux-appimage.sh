#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--build-dir DIR] [--output-dir DIR] [--version VERSION]" >&2
}

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build"
output_dir="$repo_root/dist"
version=""

while (($#)); do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            build_dir=$2
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            output_dir=$2
            shift 2
            ;;
        --version)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            version=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

[[ $(uname -s) == Linux ]] || { echo "AppImage packaging requires Linux" >&2; exit 1; }
[[ $(uname -m) == x86_64 ]] || { echo "Only x86_64 AppImage packaging is supported" >&2; exit 1; }

build_dir=$(realpath "$build_dir")
mkdir -p "$output_dir"
output_dir=$(realpath "$output_dir")

executable="$build_dir/QtNetworkChat"
[[ -x $executable ]] || { echo "Missing executable: $executable" >&2; exit 1; }

if [[ -z $version ]]; then
    version=$(sed -nE 's/^project\(QtNetworkChat VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$repo_root/CMakeLists.txt" | head -n 1)
fi
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]] || {
    echo "Invalid version: $version" >&2
    exit 1
}

linuxdeploy=${LINUXDEPLOY:-linuxdeploy-x86_64.AppImage}
qt_plugin=${LINUXDEPLOY_PLUGIN_QT:-linuxdeploy-plugin-qt-x86_64.AppImage}
command -v "$linuxdeploy" >/dev/null 2>&1 || [[ -x $linuxdeploy ]] || {
    echo "linuxdeploy not found: $linuxdeploy" >&2
    exit 1
}
command -v "$qt_plugin" >/dev/null 2>&1 || [[ -x $qt_plugin ]] || {
    echo "linuxdeploy Qt plugin not found: $qt_plugin" >&2
    exit 1
}

appdir="$output_dir/QtNetworkChat.AppDir"
cmake -E remove_directory "$appdir"
install -d "$appdir/usr/bin/ui"
install -m 0755 "$executable" "$appdir/usr/bin/QtNetworkChat"
install -m 0644 "$repo_root/ui/style-qqnt.qss" "$appdir/usr/bin/ui/style-qqnt.qss"
install -m 0644 "$repo_root/ui/style-qqnt-dark.qss" "$appdir/usr/bin/ui/style-qqnt-dark.qss"
install -d "$appdir/usr/share/metainfo"
install -m 0644 \
    "$repo_root/packaging/linux/io.github.ziyue67.qtnetworkchat.metainfo.xml" \
    "$appdir/usr/share/metainfo/io.github.ziyue67.qtnetworkchat.appdata.xml"

export QML_SOURCES_PATHS=${QML_SOURCES_PATHS:-}
export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}
export LINUXDEPLOY_OUTPUT_VERSION=$version
export LDAI_OUTPUT="$output_dir/QtNetworkChat-$version-linux-x86_64.AppImage"

"$linuxdeploy" \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/QtNetworkChat" \
    --desktop-file "$repo_root/packaging/linux/io.github.ziyue67.qtnetworkchat.desktop" \
    --icon-file "$repo_root/packaging/linux/io.github.ziyue67.qtnetworkchat.svg"
"$qt_plugin" --appdir "$appdir"
"$linuxdeploy" --appdir "$appdir" --output appimage

[[ -x $LDAI_OUTPUT ]] || { echo "AppImage was not created: $LDAI_OUTPUT" >&2; exit 1; }
"$LDAI_OUTPUT" --appimage-version >/dev/null
(
    cd "$output_dir"
    sha256sum "$(basename "$LDAI_OUTPUT")" > "$(basename "$LDAI_OUTPUT").sha256"
)
echo "$LDAI_OUTPUT"
