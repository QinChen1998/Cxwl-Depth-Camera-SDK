#!/usr/bin/env bash
set -euo pipefail

prefix=$(realpath -m -- "$1")
work=$(mktemp -d "${RUNNER_TEMP:-/tmp}/cxwl-deps.XXXXXXXX")
mkdir -p "$prefix" "$work/qt-build"

fetch_source() {
    local url=$1 hash=$2 archive=${1##*/}
    curl --fail --silent --show-error --location --retry 3 --connect-timeout 15 --max-time 180 \
        --proto '=https' --proto-redir '=https' --output "$work/$archive" "$url"
    printf '%s  %s\n' "$hash" "$work/$archive" | sha256sum --strict --check
    tar -xJf "$work/$archive" -C "$work"
}

fetch_source https://download.qt.io/official_releases/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.tar.xz \
    d9594a31228aa23ad6b531719a29b45f0f3989fe6c136d45767ea179f233c1ac
(
    cd "$work/qt-build"
    "$work/qtbase-everywhere-src-6.11.1/configure" -opensource -confirm-license -release -shared \
        -nomake examples -nomake tests -no-feature-widgets -no-feature-sql -no-feature-xcb \
        -no-opengl -no-feature-vulkan -no-feature-icu -openssl-linked \
        -prefix "$prefix/qt-6.11.1" -- -DCMAKE_INSTALL_LIBDIR=lib
)
cmake --build "$work/qt-build" --parallel 4
cmake --install "$work/qt-build"

fetch_source https://ffmpeg.org/releases/ffmpeg-7.1.3.tar.xz \
    f0bf043299db9e3caacb435a712fc541fbb07df613c4b893e8b77e67baf3adbe
(
    cd "$work/ffmpeg-7.1.3"
    ./configure --prefix="$prefix/ffmpeg-7.1.3" --enable-shared --disable-static --enable-pic \
        --disable-gpl --disable-nonfree --disable-autodetect --disable-debug --disable-doc --disable-programs
    make -j4
    make install
)

test -f "$prefix/qt-6.11.1/plugins/tls/libqopensslbackend.so"
test -f "$prefix/qt-6.11.1/lib/libQt6DBus.so.6"
test -f "$prefix/ffmpeg-7.1.3/lib/libavcodec.so.61"
