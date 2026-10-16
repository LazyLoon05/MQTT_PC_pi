#!/usr/bin/env bash
# Debian/Armbian: build Qt MQTT đúng phiên bản Qt, không sửa mã dự án.
set -euo pipefail
mode=${1:---headless}
case "$mode" in --headless|--gui) ;; *) echo "Usage: $0 [--headless|--gui]" >&2; exit 2;; esac
privilege=()
if (( EUID != 0 )); then
    command -v sudo >/dev/null || { echo 'Cần sudo để cài gói; xem docs/cloud.md cho cloud không có root.' >&2; exit 1; }
    privilege=(sudo)
fi
packages=(build-essential cmake ninja-build git ca-certificates qt6-base-dev qt6-base-private-dev mosquitto mosquitto-clients)
if [[ $mode == --gui ]]; then
    packages+=(qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls
        qml6-module-qtquick-layouts qml6-module-qtquick-window
        qml6-module-qtquick-templates qml6-module-qtqml-workerscript)
fi
"${privilege[@]}" apt-get update
"${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"
qt_version=$(qmake6 -query QT_VERSION)
if [[ $mode == --gui ]] && ! dpkg --compare-versions "$qt_version" ge 6.5; then
    echo "GUI cần Qt >= 6.5; Qt hiện tại là $qt_version. Xem README.md (Qt Online Installer)." >&2
    exit 1
fi
prefix=${MQTT_QT_PREFIX:-"$HOME/.local/qt-mqtt/$qt_version"}
cache=${XDG_CACHE_HOME:-"$HOME/.cache"}/mqtt-pc-pi
mkdir -p "$cache"
source_dir="$cache/qtmqtt-$qt_version"
if [[ ! -d $source_dir ]]; then
    git clone --depth 1 --branch "v$qt_version" https://github.com/qt/qtmqtt.git "$source_dir"
fi
[[ $(git -C "$source_dir" describe --tags --exact-match HEAD) == "v$qt_version" ]]
cmake -S "$source_dir" -B "$source_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build "$source_dir/build" --parallel "${BUILD_JOBS:-2}"
cmake --install "$source_dir/build"
printf '\nQt MQTT đã cài tại %s\nDùng: cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="%s"\n' "$prefix" "$prefix"
