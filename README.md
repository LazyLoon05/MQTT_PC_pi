# MQTT: PC ↔ Orange Pi 3B bằng Qt 6

Ứng dụng theo [thiết kế v2](docs/design-v2.md): Orange Pi chạy broker Mosquitto và `pi_node`; PC chạy `pc_gui` (Qt Quick). PC xem nhiệt độ CPU, trạng thái node và gửi `ping`/`set_interval` có ACK. Một GUI theo dõi **một node** tại một thời điểm.

```text
PC: pc_gui ── MQTT 3.1.1 / TCP 1883 ── Orange Pi 3B
                                      ├── Mosquitto (xác thực + ACL)
                                      └── pi_node (Qt Core, CPU nhiệt độ + heartbeat)
```

- Trạng thái broker: Disconnected / Connecting / Connected / Reconnecting; backoff 1–30 giây + jitter.
- Node: unknown / online / stale (heartbeat quá 15 giây) / offline (status/LWT).
- Lệnh: request ID, chờ 3 giây rồi gửi lại **cùng ID đúng 1 lần**; kết quả success / error / timeout / not_sent. Pi nhớ 64 ACK gần nhất trong RAM.
- Nhiệt độ: quét thermal zone, ưu tiên CPU/SoC của RK3566; nếu không đọc được, gửi `valid:false,value:null`.
- Mật khẩu do người dùng thiết lập; GUI không lưu mật khẩu. Cổng 1883 chưa có TLS, chỉ dùng trong LAN tin cậy.

## 1. Cài trên Orange Pi 3B

Dùng Armbian/Debian/Ubuntu có Qt 6.2 trở lên cho node; GUI cần Qt ≥ 6.5. Đã build và kiểm thử trên **Debian 13 x86_64, Qt 6.8.2**; chưa chạy trực tiếp trên bo ARM64 hoặc Windows. Không chép binary x86_64 từ cloud sang Orange Pi: build native trên Pi.

```bash
git clone https://github.com/LazyLoon05/MQTT_PC_pi.git
cd MQTT_PC_pi
# Dùng prefix toàn hệ thống để dịch vụ systemd không cần đọc /home hoặc /root.
sudo env MQTT_QT_PREFIX=/opt/qt-mqtt ./scripts/install-deps-debian.sh --headless
cmake -S . -B build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_PC_GUI=OFF -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH=/opt/qt-mqtt
cmake --build build-pi --parallel 2
```

Script dùng gói Qt từ apt và build Qt MQTT từ **tag khớp đúng phiên bản Qt**. Không đổi sang nhánh gần giống khi không có tag; cần chọn bản Qt được hỗ trợ. Có `qt6-base-private-dev` cho private headers của Qt MQTT. Qt MQTT dùng GPLv3 hoặc giấy phép thương mại; xem giấy phép Qt trước khi phân phối.

Đăng ký thư viện Qt MQTT để systemd tìm được sau reboot (thực hiện sau khi build/cài Qt MQTT):

```bash
find /opt/qt-mqtt -type f -name 'libQt6Mqtt.so.*'
# Thêm thư mục chứa thư viện thực tế; lệnh tự tìm đúng lib/lib hoặc lib/<arch>.
find /opt/qt-mqtt -type f -name 'libQt6Mqtt.so.*' -printf '%h\n' | sort -u \
  | sudo tee /etc/ld.so.conf.d/mqtt-qt.conf
sudo ldconfig
ldd build-pi/pi_node                 # không được có "not found"
```

Cài broker, tạo user riêng cho dịch vụ, nhập hai mật khẩu ở terminal rồi bật systemd:

```bash
sudo ./scripts/setup-orange-pi.sh "$PWD/build-pi/pi_node"
sudo journalctl -u mosquitto -u pi-node -n 50 --no-pager
hostname -I                         # IP LAN của Orange Pi để nhập trên PC
```

Script tạo tài khoản Linux `mqtt-node` riêng, không giả định user `pi`; mật khẩu Pi nằm ở `/etc/mqtt_qt/pi_node.pass` (root:mqtt-node, 0640). Broker dùng user MQTT `pi_user`, `pc_user` và ACL [deploy/mosquitto](deploy/mosquitto). Script từ chối ghi đè cấu hình đã có. Nếu broker đã có listener 1883 trong file khác, hãy hợp nhất cấu hình trước để tránh trùng listener; không chạy script lên broker đang phục vụ hệ thống khác khi chưa rà soát.

Khi cập nhật binary sau này:

```bash
sudo systemctl stop pi-node
sudo install -m 755 build-pi/pi_node /usr/local/bin/pi_node
sudo systemctl start pi-node
```

Nếu dùng UFW, chỉ cho subnet LAN thật của bạn (ví dụ dưới là minh họa):

```bash
sudo ufw allow from 192.168.1.0/24 to any port 1883 proto tcp
```

Cố định IP Orange Pi bằng DHCP reservation. Không port-forward 1883 ra Internet. Broker nằm trên Pi nên khi Pi tắt, cả broker và dữ liệu đều mất kết nối.

## 2. PC Linux

Trên Debian 13 hoặc hệ có gói Qt ≥ 6.5:

```bash
git clone https://github.com/LazyLoon05/MQTT_PC_pi.git
cd MQTT_PC_pi
./scripts/install-deps-debian.sh --gui
cmake -S . -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_PI_NODE=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$HOME/.local/qt-mqtt/$(qmake6 -query QT_VERSION)"
cmake --build build-pc --parallel 2
./build-pc/pc_gui
```

Trong GUI nhập **IP LAN của Orange Pi**, port `1883`, user `pc_user`, mật khẩu vừa tạo và Node ID `pi`. Mặc định `localhost` chỉ phù hợp khi chạy thử broker trên cùng PC. Bấm Kết nối, chờ Online, thử Ping rồi đặt chu kỳ `500` ms. Nút Ngắt cũng dừng các lần reconnect.

## 3. PC Windows / Qt Creator

1. Cài Qt ≥ 6.5 bằng Qt Online Installer, chọn cùng kit cho **Qt Quick**, **Qt MQTT** và compiler (MinGW hoặc MSVC). Đừng trộn thư viện MQTT từ kit khác.
2. Clone repo, mở `CMakeLists.txt` trong Qt Creator.
3. Chọn kit đã cài; đặt `BUILD_PI_NODE=OFF`, `BUILD_PC_GUI=ON`, `BUILD_TESTING=OFF` trong CMake Configuration.
4. Build, chạy `pc_gui`, nhập IP LAN/user/mật khẩu như mục 2. Cho phép kết nối TCP tới Orange Pi nếu firewall PC chặn outbound.

Build từ terminal của kit Qt (thay đường dẫn cho đúng máy):

```powershell
cmake -S . -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_PI_NODE=OFF -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH="C:/Qt/6.8.2/mingw_64"
cmake --build build-pc --parallel 2
.\build-pc\pc_gui.exe
```

Qt runtime phải có trong PATH khi chạy bằng terminal. Nếu cần chép ứng dụng sang PC khác, dùng `windeployqt --qmldir pc_gui build-pc/pc_gui.exe` từ **cùng kit**, rồi kiểm tra lại trên máy đích.

## 4. Chạy node thủ công

Thay `sudo setup-orange-pi` bằng cách tự cấu hình broker theo [các mẫu](deploy/mosquitto). Khi broker đã có tài khoản và ACL, chạy node bằng file mật khẩu; không đặt mật khẩu thẳng vào dòng lệnh hoặc Git:

```bash
umask 077
read -r -s -p 'Mật khẩu pi_user: ' mqtt_pi_password; printf '\n'
printf '%s\n' "$mqtt_pi_password" > pi_node.pass
unset mqtt_pi_password
./build-pi/pi_node --host localhost --port 1883 --user pi_user \
  --pass-file ./pi_node.pass --id pi
```

Có thể dùng biến `MQTT_USER`, `MQTT_PASSWORD` nếu đã được cấp an toàn; `--user` và `--pass-file` ưu tiên hơn biến. File chỉ bỏ ký tự kết thúc dòng, giữ nguyên khoảng trắng của mật khẩu. Không có mật khẩu mặc định.

## 5. Giao thức và kiểm thử

| Topic | Hướng | QoS | Retain |
|---|---|---|---|
| `lab/pi/status` (`online`/`offline`) | Pi → PC, LWT | 1 | có |
| `lab/pi/heartbeat` (5 s) | Pi → PC | 0 | không |
| `lab/pi/sensor/cpu_temp` (mặc định 2 s) | Pi → PC | 0 | không |
| `lab/pi/cmd/ping`, `lab/pi/cmd/set_interval` | PC → Pi | 1 | không |
| `lab/pi/reply/<command>` | Pi → PC | 1 | không |
| `lab/pc/status` | PC → broker | 1 | có |

```json
{"request_id":"manual-1","command":"set_interval","value":500,"ts_ms":1760000000123}
```

`value` phải là số nguyên 200..60000. ACK phải khớp node/topic/command/request ID. `success` của MQTT publish chỉ là giao cho client; ACK ứng dụng mới xác nhận xử lý lệnh. Timeout không chứng minh lệnh chưa thực hiện. Dedupe mất khi Pi restart; không dùng cơ chế này để khẳng định exactly-once.

Build cả hai ứng dụng và chạy bộ kiểm thử Linux (cần `mosquitto`, `mosquitto_passwd` trong PATH):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/.local/qt-mqtt/$(qmake6 -query QT_VERSION)"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Tests tạo broker chỉ nghe `127.0.0.1` trên port ngẫu nhiên, dùng tài khoản/mật khẩu ngẫu nhiên và dọn các tiến trình khi xong; không đụng broker hệ thống. Test GUI dùng offscreen. Xem [kết quả kiểm thử](docs/validation.md) và [14 kịch bản thực địa](docs/design-v2.md#10-kịch-bản-kiểm-thử). Thử reboot thật và đo mạng LAN/Wi-Fi trên PC/Pi vẫn cần thực hiện trên thiết bị của bạn.

## 6. Cấu trúc

```text
core/               Config, giao thức, MQTT reconnect/LWT, theo dõi ACK
pi_node/            CPU thermal reader, xử lý lệnh, headless executable
pc_gui/             NodeController, Qt Quick UI
scripts/            Cài phụ thuộc, triển khai Orange Pi, chuẩn bị cloud
deploy/             Mosquitto + ACL, systemd unit
tests/              Unit, MQTT integration, GUI smoke
docs/               Thiết kế gốc, kết quả kiểm thử, cloud workflow
```

[Cloud development](docs/cloud.md) ghi cách kích hoạt bộ công cụ đã cài trong cloud. [GitHub Actions](.github/workflows/build.yml) build và test trên Debian 13; trạng thái CI phải xem kết quả thực tế sau khi push.

Nếu lỗi `Qt6Mqtt` không tìm thấy, kiểm tra prefix chứa `lib/*/cmake/Qt6Mqtt`, dùng đúng `CMAKE_PREFIX_PATH` và cùng phiên bản Qt. GUI stale: xem heartbeat/ACL. Lệnh timeout: xem cả `cmd/#` và `reply/#`, cùng log `journalctl -u pi-node`. Nhiệt độ `--` kèm cảm biến lỗi là trạng thái có chủ đích khi không có thermal zone hợp lệ.
