# MQTT: Windows 10 / Qt 5.14.2 ↔ Orange Pi 3B / Ubuntu 20.04

Dự án triển khai [thiết kế MQTT v2](docs/design-v2.md), điều chỉnh cho thiết bị của bạn:

| Thiết bị | Phần mềm | Qt / công cụ |
|---|---|---|
| PC Windows 10 | `pc_gui` (Qt Quick) | **Qt 5.14.2**, compiler đúng kit đã cài |
| Orange Pi 3B | Mosquitto + `pi_node` (Qt Core, không GUI) | Orange Pi OS 1.0.8 Focal / Ubuntu 20.04, kernel `5.10.160-rockchup-rk356x`; Qt **5.12.8 từ apt** nếu chưa cài Qt riêng |

Không cần nâng PC lên Qt 6 hoặc nâng hệ điều hành Orange Pi. Hai máy không cần cùng phiên bản Qt: giao tiếp qua MQTT 3.1.1 và JSON. **Qt MQTT phải khớp phiên bản Qt và compiler trên chính máy cài nó** (PC: tag `v5.14.2`; Pi: kiểm tra bằng `qmake -query QT_VERSION`).

```text
Windows 10: pc_gui ── MQTT / TCP 1883 ── Orange Pi 3B
                                       ├── Mosquitto (xác thực + ACL)
                                       └── pi_node (telemetry + heartbeat + ACK)
```

Có reconnect/backoff, status/LWT, heartbeat 5 giây, phát hiện stale sau 15 giây, nhiệt độ CPU/SoC RK3566, lệnh ping/đổi chu kỳ. Lệnh có request ID, timeout 3 giây, gửi lại một lần cùng ID, cache 64 ACK để chống thực thi lặp. GUI theo dõi một node mỗi lần.

## 1. Orange Pi: cài và build native

Chạy các lệnh trên **Orange Pi**, không chép binary x86_64 từ cloud sang ARM64:

```bash
uname -m                            # thường là aarch64
cat /etc/os-release
sudo apt-get update
sudo apt-get install -y git
git clone https://github.com/LazyLoon05/MQTT_PC_pi.git
cd MQTT_PC_pi
./scripts/install-deps-debian.sh --headless
qmake -query QT_VERSION             # Focal thường là 5.12.8
cmake --version                    # CMake 3.16 trên Focal đủ dùng
cmake -S . -B build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMQTT_QT_MAJOR=5 -DBUILD_PC_GUI=OFF -DBUILD_TESTING=OFF
cmake --build build-pi --parallel 2
ldd build-pi/pi_node                # không được có "not found"
```

Script cần sudo; tự cài Qt 5 base/private headers, Mosquitto, CMake, rồi build Qt MQTT bằng **qmake + make**, cài vào cùng Qt kit (gói Focal là `/usr`) và chạy `ldconfig`. Không dùng `qt6-base-dev` hoặc CMake để build mã `qtmqtt` 5.x. Nếu báo không tìm thấy gói Qt 5, kiểm tra nguồn apt Focal và thành phần `universe` đang bật trên image Orange Pi.

Script chọn `qmake` hệ thống mặc định. Nếu Pi đã có Qt riêng, kiểm tra trước và dùng `QT5_QMAKE=/đường/dẫn/qmake ./scripts/install-deps-debian.sh --headless`; không trộn Qt system và Qt tự cài. CMake khi đó cũng cần `-DCMAKE_PREFIX_PATH=/đường/dẫn/Qt-kit`.

Cài broker, tạo hai tài khoản MQTT và bật node tự khởi động:

```bash
sudo ./scripts/setup-orange-pi.sh "$PWD/build-pi/pi_node"
sudo journalctl -u mosquitto -u pi-node -n 50 --no-pager
hostname -I                         # lấy IP LAN của Pi để nhập trên PC
```

Script hỏi mật khẩu mới cho `pi_user` và `pc_user` qua terminal, tạo tài khoản Linux `mqtt-node` riêng (không giả định user `pi`), cài binary vào `/usr/local/bin/pi_node`. Mật khẩu node nằm ở `/etc/mqtt_qt/pi_node.pass`, quyền `root:mqtt-node` 0640. Broker dùng [cấu hình và ACL](deploy/mosquitto); node dùng [systemd unit](deploy/systemd/pi-node.service).

Script từ chối ghi đè file cấu hình đã có. Nếu Mosquitto có listener 1883 trong file khác, hợp nhất cấu hình trước khi chạy để tránh trùng listener. Node ID mặc định là `pi`, khớp ACL. Không đổi `--id` nếu chưa cập nhật ACL tương ứng.

Khi cập nhật mã:

```bash
git pull --ff-only
cmake --build build-pi --parallel 2
sudo systemctl stop pi-node
sudo install -m 755 build-pi/pi_node /usr/local/bin/pi_node
sudo systemctl start pi-node
```

## 2. Windows 10: chuẩn bị Qt MQTT 5.14.2

Giữ Qt **5.14.2** hiện có. Trong Qt Creator → Kits, xem compiler của kit: ví dụ **MinGW 7.3 64-bit** hoặc **MSVC**. Thư viện Qt MQTT cần build bằng đúng compiler/kiến trúc đó. Mở terminal của kit (MSVC cần terminal Developer Command Prompt / Developer PowerShell với môi trường compiler đã nạp), rồi mở PowerShell trong môi trường ấy.

Trước khi build Qt MQTT, cài **[Strawberry Perl 64-bit](https://strawberryperl.com/)** (bản Windows MSI, thư mục mặc định `C:\Strawberry`). Qt dùng Perl chạy `syncqt.pl` để tạo các header của module. Kiểm tra `perl -v` phải chạy được. Script Windows kiểm tra Perl trước khi bắt đầu build.

Nếu đang dùng CMD/MSVC và vừa cài Perl, có thể bổ sung PATH cho chính cửa sổ hiện tại:

```bat
set "PATH=%PATH%;C:\Strawberry\perl\bin;C:\Strawberry\c\bin"
perl -v
```

Các đường dẫn Perl được thêm cuối PATH để giữ compiler/linker MSVC đang dùng. Nếu đã build dở và gặp `'perl' is not recognized`, giữ nguyên mã nguồn và thư mục build; sau khi `perl -v` chạy được, chạy lại qmake, `nmake`, rồi `nmake install`. Không cần clone hoặc xóa thư mục build.

```powershell
git clone https://github.com/LazyLoon05/MQTT_PC_pi.git
cd MQTT_PC_pi
# Sửa đường dẫn cho khớp Qt kit trên máy bạn:
$qtKit = 'C:\Qt\5.14.2\mingw73_64'
& "$qtKit\bin\qmake.exe" -query QT_VERSION
.\scripts\install-qtmqtt-windows.ps1 -QtKit $qtKit -MakeTool mingw32-make
```

Với MSVC, thay `$qtKit` bằng kit MSVC 5.14.2 và `-MakeTool nmake`. Nếu `mingw32-make` không có trong PATH, thêm thư mục `bin` của compiler MinGW đi kèm Qt kit (không lấy MinGW từ bộ khác).

Script clone Qt MQTT tag **v5.14.2**, build Debug + Release bằng qmake, rồi cài module vào kit Qt. Cần Git, Perl, compiler và quyền ghi vào thư mục kit. Không đặt mật khẩu MQTT trong script. Nếu module đã được cài đúng từ trước, có thể bỏ qua bước này.

Kiểm tra:

```powershell
Test-Path "$qtKit\lib\cmake\Qt5Mqtt\Qt5MqttConfig.cmake"
Test-Path "$qtKit\bin\Qt5Mqtt.dll"
```

Qt MQTT dùng GPLv3 hoặc giấy phép thương mại; xem giấy phép Qt trước khi phân phối ứng dụng.

## 3. Windows 10: build và chạy GUI

Cách dùng Qt Creator:

1. Mở `CMakeLists.txt`, chọn **kit Qt 5.14.2** đúng compiler.
2. Trong CMake Configuration đặt `MQTT_QT_MAJOR=5`, `BUILD_PI_NODE=OFF`, `BUILD_PC_GUI=ON`, `BUILD_TESTING=OFF`.
3. Chọn Release để bắt đầu; build và chạy target `pc_gui`. Nếu từng configure bằng Qt 6, dùng thư mục build mới hoặc xóa cache CMake cũ.
4. Nhập IP LAN của Orange Pi, port `1883`, user `pc_user`, mật khẩu đã tạo, Node ID `pi`. Bấm Kết nối, chờ Online, thử Ping và chu kỳ `500` ms.

Hoặc terminal của Qt kit với CMake ≥ 3.16 và Ninja trong PATH:

```powershell
$qtKit = 'C:\Qt\5.14.2\mingw73_64'
cmake -S . -B build-pc-qt5 -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DMQTT_QT_MAJOR=5 -DBUILD_PI_NODE=OFF -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH="$qtKit"
cmake --build build-pc-qt5 --parallel 2
$env:PATH = "$qtKit\bin;$env:PATH"
.\build-pc-qt5\pc_gui.exe
```

Mặc định GUI là `localhost`: **đổi thành IP của Pi** khi chạy từ PC. GUI chỉ giữ mật khẩu trong RAM. Khi chép sang Windows khác, chạy từ cùng kit:

```powershell
& "$qtKit\bin\windeployqt.exe" --release --qmldir pc_gui build-pc-qt5\pc_gui.exe
```

## 4. LAN và xác nhận trên thiết bị

PC/Pi cần cùng LAN; nên đặt DHCP reservation cho Pi. Nếu Pi dùng UFW, chỉ mở subnet thật, ví dụ:

```bash
sudo ufw allow from 192.168.1.0/24 to any port 1883 proto tcp
```

Port 1883 chưa có TLS, chỉ dùng trong LAN tin cậy, không port-forward ra Internet. Broker chạy trên Pi nên Pi tắt thì broker cũng mất.

Trên PC có thể kiểm tra TCP trước khi mở GUI:

```powershell
Test-NetConnection -ComputerName <IP_Pi> -Port 1883
```

Sau khi kết nối GUI, kiểm tra nhiệt độ hợp lệ, Ping có ACK, interval đổi sang 500 ms, `sudo systemctl restart mosquitto` vẫn phục hồi, rồi reboot Pi để xác nhận tự khởi động. Xem [14 kịch bản gốc](docs/design-v2.md#10-kịch-bản-kiểm-thử) và [kết quả kiểm thử cloud](docs/validation.md). Chưa có quyền truy cập PC/bo thật từ task này; test x86_64 không thay thế chạy trên ARM64 hoặc Windows.

## 5. Build/test Linux và Qt 6 tùy chọn

Mặc định dự án dùng **Qt 5 ≥ 5.12**, C++17 và CMake ≥ 3.16. Test đầy đủ trên Linux cần thêm GUI dependencies:

```bash
./scripts/install-deps-debian.sh --gui
cmake -S . -B build-qt5 -G Ninja -DCMAKE_BUILD_TYPE=Release -DMQTT_QT_MAJOR=5
cmake --build build-qt5 --parallel 2
(cd build-qt5 && ctest --output-on-failure)
```

Dùng `(cd build && ctest ...)` vì CTest 3.16 của Focal chưa có `--test-dir`. Tests tạo Mosquitto xác thực/ACL trên port loopback ngẫu nhiên, chạy node thật, xác minh ACK/retry/LWT/stale/reconnect, rồi dọn tiến trình. GUI smoke chạy offscreen, không cần desktop. Với Docker có thể tái tạo Ubuntu 20.04 x86_64 bằng `./scripts/test-focal-container.sh`.

Qt 6 vẫn được hỗ trợ bằng **thư mục build riêng** và `-DMQTT_QT_MAJOR=6`. Trên hệ có Qt 6 từ apt:

```bash
MQTT_QT_MAJOR=6 ./scripts/install-deps-debian.sh --gui
cmake -S . -B build-qt6 -G Ninja -DMQTT_QT_MAJOR=6 \
  -DCMAKE_PREFIX_PATH="$HOME/.local/qt-mqtt/$(qmake6 -query QT_VERSION)"
cmake --build build-qt6 --parallel 2
(cd build-qt6 && ctest --output-on-failure)
```

## 6. Giao thức và cấu trúc

| Topic | Hướng | QoS | Retain |
|---|---|---|---|
| `lab/pi/status` (`online` / `offline`) | Pi → PC, LWT | 1 | có |
| `lab/pi/heartbeat` (5 s) | Pi → PC | 0 | không |
| `lab/pi/sensor/cpu_temp` (mặc định 2 s) | Pi → PC | 0 | không |
| `lab/pi/cmd/ping`, `lab/pi/cmd/set_interval` | PC → Pi | 1 | không |
| `lab/pi/reply/<command>` | Pi → PC | 1 | không |
| `lab/pc/status` | PC → broker | 1 | có |

```json
{"request_id":"manual-1","command":"set_interval","value":500,"ts_ms":1760000000123}
```

Interval là số nguyên 200..60000 ms. ACK phải khớp node/topic/command/request ID. Publish thành công chỉ là giao cho MQTT client; ACK ứng dụng mới xác nhận xử lý. Timeout không chứng minh lệnh chưa chạy; dedupe mất khi node restart. CPU thermal zone không hợp lệ được gửi thành `valid:false,value:null`, GUI hiện `--`. RTT dùng đồng hồ đơn điệu của PC.

```text
core/               Config, JSON protocol, MQTT, CommandTracker
pi_node/            Nhiệt độ CPU/SoC, lệnh/ACK, headless executable
pc_gui/             NodeController, Main.qml và Qt resource dùng chung Qt 5/6
scripts/            Cài Linux, build Qt MQTT Windows, triển khai systemd, cloud
deploy/             Mosquitto ACL/config và systemd service
tests/              Unit, MQTT integration, GUI smoke
docs/               Tài liệu gốc, validation và cloud workflow
```

Nếu `Qt5Mqtt` không tìm thấy: kiểm tra Qt MQTT đã build/cài bằng đúng **qmake 5.14.2 của kit PC** hoặc qmake 5.12.8 của Pi; không trỏ tới thư viện Qt 6. `Unknown module(s) in QT: core-private`: cần private headers tương ứng Qt. Lệnh timeout: xem log `journalctl -u pi-node` và ACL `cmd/#`, `reply/#`. GUI stale: kiểm tra heartbeat. Dữ liệu không có trên bo: xem `/sys/class/thermal/thermal_zone*/type` và `temp`.

[Cloud development](docs/cloud.md) dùng bộ Qt 6 sẵn trong cloud với cờ chọn rõ ràng; [GitHub Actions](.github/workflows/build.yml) có hai cấu hình Qt 5/Focal và Qt 6/Debian.
