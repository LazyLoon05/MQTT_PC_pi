#!/usr/bin/env bash
# Tái tạo Qt 5.12.8 / Ubuntu 20.04 x86_64. Không thay thế kiểm thử ARM/Windows.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mounts=(--mount "type=bind,src=$root,dst=/src,readonly")
# Codex có thể cung cấp CA công khai của proxy; giữ xác minh TLS trong container.
proxy_ca=${MQTT_PROXY_CA_FILE:-${CODEX_PROXY_CERT:-}}
if [[ -n $proxy_ca && -f $proxy_ca ]]; then
    proxy_ca=$(readlink -f -- "$proxy_ca")
    mounts+=(--mount "type=bind,src=$proxy_ca,dst=/tmp/mqtt-proxy-ca.crt,readonly")
fi
docker run --rm "${mounts[@]}" ubuntu:20.04 \
    bash -c 'set -euo pipefail
        apt-get -o Acquire::Retries=3 update
        DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=3 install -y --no-install-recommends git ca-certificates
        if [[ -f /tmp/mqtt-proxy-ca.crt ]]; then
            install -m 644 /tmp/mqtt-proxy-ca.crt /usr/local/share/ca-certificates/mqtt-proxy-ca.crt
            update-ca-certificates
        fi
        /src/scripts/install-deps-debian.sh --gui
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMQTT_QT_MAJOR=5
        cmake --build /build --parallel 2
        cd /build && ctest --output-on-failure
        cmake -S /src -B /build-pi -G Ninja -DBUILD_PC_GUI=OFF -DBUILD_TESTING=OFF -DMQTT_QT_MAJOR=5
        cmake --build /build-pi --parallel 2'
