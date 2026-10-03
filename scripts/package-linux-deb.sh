#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf 'Usage: %s BUILD_DIR OUTPUT_DIR VERSION\n' "$0" >&2
    exit 2
fi

repo_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=$(realpath "$1")
mkdir -p "$2"
output_dir=$(realpath "$2")
version=$3

if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'Invalid version: %s\n' "$version" >&2
    exit 2
fi

binary="$build_dir/QtNetworkChat"
test -x "$binary"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
stage="$work_dir/package"
app_dir="$stage/opt/qtnetworkchat"
mkdir -p "$app_dir/ui" "$stage/usr/bin" "$stage/usr/share/applications" \
    "$stage/usr/share/icons/hicolor/scalable/apps" \
    "$stage/usr/share/doc/qtnetworkchat" "$stage/DEBIAN" "$work_dir/debian"

install -m 0755 "$binary" "$app_dir/QtNetworkChat"
install -m 0644 "$repo_root/ui/style-qqnt.qss" "$app_dir/ui/"
install -m 0644 "$repo_root/ui/style-qqnt-dark.qss" "$app_dir/ui/"
install -m 0644 "$repo_root/packaging/linux/io.github.ziyue67.qtnetworkchat.desktop" \
    "$stage/usr/share/applications/"
install -m 0644 "$repo_root/packaging/linux/io.github.ziyue67.qtnetworkchat.svg" \
    "$stage/usr/share/icons/hicolor/scalable/apps/"
install -m 0644 "$repo_root/LICENSE" "$stage/usr/share/doc/qtnetworkchat/copyright"
ln -s /opt/qtnetworkchat/QtNetworkChat "$stage/usr/bin/QtNetworkChat"

printf 'Source: qtnetworkchat\nSection: net\nPriority: optional\nMaintainer: ziyue67 <ziyue67@users.noreply.github.com>\nStandards-Version: 4.6.2\n\nPackage: qtnetworkchat\nArchitecture: amd64\nDescription: Self-hosted Qt desktop chat client\n' > "$work_dir/debian/control"
depends=$(cd "$work_dir" && dpkg-shlibdeps -O "$app_dir/QtNetworkChat")
depends=${depends#shlibs:Depends=}
printf 'Package: qtnetworkchat\nVersion: %s\nSection: net\nPriority: optional\nArchitecture: amd64\nMaintainer: ziyue67 <ziyue67@users.noreply.github.com>\nDepends: %s, libqt6sql6-sqlite\nDescription: Self-hosted Qt desktop chat client\n QtNetworkChat connects to a self-hosted chat service over WSS.\n' \
    "$version" "$depends" > "$stage/DEBIAN/control"

dpkg-deb --build --root-owner-group "$stage" \
    "$output_dir/QtNetworkChat-$version-linux-amd64.deb"
