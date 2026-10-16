#!/usr/bin/env bash
# Debian/Ubuntu/Armbian: Qt 5 mặc định; MQTT_QT_MAJOR=6 để chọn Qt 6.
set -euo pipefail
mode=${1:---headless}
case "$mode" in --headless|--gui) ;; *) echo "Usage: $0 [--headless|--gui]" >&2; exit 2;; esac
major=${MQTT_QT_MAJOR:-5}
case "$major" in 5|6) ;; *) echo 'MQTT_QT_MAJOR phải là 5 hoặc 6.' >&2; exit 2;; esac
privilege=()
if (( EUID != 0 )); then
    command -v sudo >/dev/null || { echo 'Cần sudo để cài gói; xem docs/cloud.md cho cloud không có root.' >&2; exit 1; }
    privilege=(sudo)
fi
packages=(build-essential cmake ninja-build git ca-certificates mosquitto mosquitto-clients)
if [[ $major == 5 ]]; then
    packages+=(qtbase5-dev qtbase5-private-dev qt5-qmake)
    if [[ $mode == --gui ]]; then
        packages+=(qtdeclarative5-dev qtquickcontrols2-5-dev qml-module-qtquick2
            qml-module-qtquick-controls2 qml-module-qtquick-layouts
            qml-module-qtquick-window2 qml-module-qtquick-templates2)
    fi
    qmake=${QT5_QMAKE:-qmake}
else
    packages+=(qt6-base-dev qt6-base-private-dev)
    if [[ $mode == --gui ]]; then
        packages+=(qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls
            qml6-module-qtquick-layouts qml6-module-qtquick-window
            qml6-module-qtquick-templates qml6-module-qtqml-workerscript)
    fi
    qmake=qmake6
fi
"${privilege[@]}" apt-get -o Acquire::Retries=3 update
"${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=3 install -y --no-install-recommends "${packages[@]}"
qt_version=$("$qmake" -query QT_VERSION)
[[ $qt_version == "$major".* ]] || { echo "Sai Qt kit: $qt_version" >&2; exit 1; }
cache=${XDG_CACHE_HOME:-"$HOME/.cache"}/mqtt-pc-pi
mkdir -p "$cache"
source_dir="$cache/qtmqtt-$qt_version"
if [[ ! -d $source_dir ]]; then
    git clone --depth 1 --branch "v$qt_version" https://github.com/qt/qtmqtt.git "$source_dir"
fi
[[ $(git -C "$source_dir" describe --tags --exact-match HEAD) == "v$qt_version" ]]
if [[ $major == 5 ]]; then
    # Qt 5 dùng qmake và cài module vào cùng Qt kit (Ubuntu Focal: /usr).
    # Không dùng cmake để build qtmqtt 5.x; không cần thay Qt 5.14.2 trên PC.
    prefix=$("$qmake" -query QT_INSTALL_PREFIX)
    if [[ -n ${MQTT_QT_PREFIX:-} && $MQTT_QT_PREFIX != "$prefix" ]]; then
        echo "Qt 5 cài MQTT vào Qt kit $prefix; không đặt MQTT_QT_PREFIX. Xem README." >&2
        exit 1
    fi
    mkdir -p "$source_dir/build"
    (cd "$source_dir/build" && "$qmake" "$source_dir/qtmqtt.pro" 'QT_BUILD_PARTS=libs' && make -j"${BUILD_JOBS:-2}")
    "${privilege[@]}" make -C "$source_dir/build" install
    "${privilege[@]}" ldconfig
else
    prefix=${MQTT_QT_PREFIX:-"$HOME/.local/qt-mqtt/$qt_version"}
    cmake -S "$source_dir" -B "$source_dir/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
    cmake --build "$source_dir/build" --parallel "${BUILD_JOBS:-2}"
    cmake --install "$source_dir/build"
fi
printf '\nQt MQTT %s đã cài tại %s\n' "$qt_version" "$prefix"
