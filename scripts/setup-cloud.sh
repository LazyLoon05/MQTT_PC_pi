#!/usr/bin/env bash
# Rootless development tools for this Debian 13 Codex image. No global installation.
set -euo pipefail
source /etc/os-release
[[ $ID == debian && $VERSION_ID == 13 ]] || {
    echo 'Script cloud này dành cho Debian 13; máy PC/Pi dùng install-deps-debian.sh.' >&2; exit 1;
}
dev=/workspace/.mqtt-dev
mkdir -p "$dev/apt/lists/partial" "$dev/apt/archives/partial" "$dev/apt/empty"
cat > "$dev/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://security.debian.org/debian-security trixie-security main
SOURCES
cat > "$dev/apt/apt.conf" <<CONFIG
Dir::Etc::main "/dev/null";
Dir::Etc::parts "$dev/apt/empty";
Dir::Etc::sourcelist "$dev/apt/sources.list";
Dir::Etc::sourceparts "$dev/apt/empty";
Dir::State::lists "$dev/apt/lists";
Dir::Cache::archives "$dev/apt/archives";
Dir::Cache::pkgcache "";
Dir::Cache::srcpkgcache "";
CONFIG
export APT_CONFIG="$dev/apt/apt.conf"
/usr/bin/apt-get update
/usr/bin/apt-get -o Debug::NoLocking=1 --download-only --no-install-recommends -y install \
    cmake ninja-build qt6-base-dev qt6-base-private-dev qt6-declarative-dev \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-window qml6-module-qtquick-templates \
    qml6-module-qtqml-workerscript mosquitto mosquitto-clients
stage=$(mktemp -d "$dev/sysroot-stage.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
for package in "$dev"/apt/archives/*.deb; do dpkg-deb -x "$package" "$stage"; done
# Merge existing image dependencies without changing system files.
python3 - "$stage" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])
def merge(source, dest):
    dest.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        target = dest / item.name
        if not target.exists() and not target.is_symlink():
            target.symlink_to(item, target_is_directory=item.is_dir())
        elif item.is_dir() and target.is_dir() and not target.is_symlink():
            merge(item, target)
for name in ('usr/lib', 'usr/include', 'usr/share'):
    merge(Path('/') / name, root / name)
PY
rm -rf -- "$dev/sysroot-previous"
if [[ -d $dev/sysroot ]]; then mv -- "$dev/sysroot" "$dev/sysroot-previous"; fi
mv -- "$stage" "$dev/sysroot"
trap - EXIT
cat > "$dev/activate.sh" <<'ACTIVATE'
export PATH="/workspace/.mqtt-dev/sysroot/usr/bin:/workspace/.mqtt-dev/sysroot/usr/sbin:$PATH"
export LD_LIBRARY_PATH="/workspace/.mqtt-dev/sysroot/usr/lib/x86_64-linux-gnu:/workspace/.mqtt-dev/qt-mqtt/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CMAKE_PREFIX_PATH="/workspace/.mqtt-dev/qt-mqtt:/workspace/.mqtt-dev/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export QT_PLUGIN_PATH="/workspace/.mqtt-dev/sysroot/usr/lib/x86_64-linux-gnu/qt6/plugins"
export QML_IMPORT_PATH="/workspace/.mqtt-dev/sysroot/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspace/.mqtt-dev/cache}"
mkdir -p "$XDG_CACHE_HOME"
ACTIVATE
source "$dev/activate.sh"
qt_version=$(qmake6 -query QT_VERSION)
if [[ ! -d $dev/qtmqtt ]]; then
    git clone --depth 1 --branch "v$qt_version" https://github.com/qt/qtmqtt.git "$dev/qtmqtt"
fi
[[ $(git -C "$dev/qtmqtt" describe --tags --exact-match HEAD) == "v$qt_version" ]]
cmake -S "$dev/qtmqtt" -B "$dev/qtmqtt-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$dev/qt-mqtt" \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build "$dev/qtmqtt-build" --parallel "${BUILD_JOBS:-3}"
cmake --install "$dev/qtmqtt-build"
printf '\nTools ready. Run: source %s/activate.sh\n' "$dev"
