# MQTT Communication System (PC GUI & Orange Pi Console)

Hệ thống giao tiếp MQTT hai chiều hoàn chỉnh giữa máy tính **PC (Giao diện đồ họa Qt Widgets)** và bo mạch **Orange Pi 3B (Ứng dụng dòng lệnh Qt Console)** qua Broker Mosquitto.

---

## 1. Cấu trúc thư mục & Kiến trúc Topic

```text
├── Mqtt_test.pro          # File dự án Qt GUI Client cho PC
├── main.cpp               # Điểm bắt đầu PC GUI
├── mainwindow.h           # Header xử lý MQTT Client trên PC
├── mainwindow.cpp         # Xử lý logic MQTT, QoS/Retain & đo độ trễ TCP Ping (ms)
├── mainwindow.ui          # Giao diện đồ họa thiết kế qua Qt Designer
│
├── Mqtt_cons/             # Ứng dụng Qt Console Client cho Orange Pi
│   ├── Mqtt_cons.pro      # File dự án Console Client
│   ├── main.cpp           # Điểm bắt đầu, CLI parser và bắt tín hiệu POSIX (SIGINT/SIGTERM)
│   ├── mqttconsoleclient.h
│   └── mqttconsoleclient.cpp
│
└── README.md
```

### Quy hoạch Topic bất đối xứng (Chống vọng tin Echo):
| Kênh truyền | PC GUI (`Mqtt_test`) | Orange Pi (`Mqtt_cons`) | Ý nghĩa |
| :--- | :--- | :--- | :--- |
| **`orangepi/cmd`** | **Pub Topic** (Mặc định) | **Sub Topic** (Mặc định) | PC gửi lệnh điều khiển xuống Orange Pi |
| **`orangepi/data`** | **Sub Topic** (Mặc định) | **Pub Topic** (Mặc định) | Orange Pi gửi dữ liệu cảm biến/phản hồi lên PC |
| **`orangepi/status`** | Có thể bấm `Sub` để theo dõi | **Status Topic** (LWT + Retain) | Trạng thái Online/Offline tự động của Orange Pi |

---

## 2. Ứng dụng PC GUI (`Mqtt_test`)

### Tính năng nổi bật (Phase 1):
- **Cấu hình kết nối & SSL/TLS thông minh**:
  - Hỗ trợ Broker IP/Hostname, Port, Username, Password.
  - Tự động chuyển cổng `1883` <-> `8883` khi bật/tắt **SSL Encryption** và kiểm tra thư viện OpenSSL hệ thống trước khi kết nối.
- **Tách biệt Sub Topic & Pub Topic**:
  - Cho phép đăng ký nhận dữ liệu (`Sub` / `Unsub`) trên nhiều topic cùng lúc mà không cần ngắt kết nối.
- **Tùy chọn nâng cao QoS & Retain**:
  - Chọn mức **QoS (0, 1, 2)** riêng cho cả chiều Subscribe và Publish.
  - Checkbox **Retain** để lưu giữ bản tin quan trọng trên Broker.
- **Truyền File 2 chiều có kiểm tra toàn vẹn (Chunked File Transfer + SHA-256)**:
  - Nút **`Send File...`** cho phép chọn bất kỳ file nào (văn bản, hình ảnh, nhị phân, script, nén...) tối đa 50 MB để gửi xuống Orange Pi.
  - Tự động chia nhỏ file thành từng mảnh `32 KB` (Base64 chunking), hiển thị tiến độ thời gian thực trên thanh **`FileProgressBar` (`0% -> 100%`)** mà không làm đơ giao diện.
  - Khi nhận file từ Orange Pi, tự động kiểm tra mã băm **`SHA-256`**, lưu vào thư mục `received_files/` và có nút **`Open Recv Folder`** để mở nhanh thư mục chứa file.
- **Tách riêng 2 khung hiển thị**:
  - **Recv Data (Incoming MQTT Messages)**: Chỉ hiển thị dữ liệu thực tế nhận từ các topic kèm nút `Clear Recv`.
  - **System Log (Status & Diagnostics)**: Hiển thị nhật ký kết nối, trạng thái, lỗi mạng và kết quả đo Ping kèm nút `Clear Log`.
- **Đo độ trễ mạng thực tế (Ping ms)**:
  - Sử dụng `QTcpSocket` kết hợp `QElapsedTimer` để đo chính xác thời gian phản hồi TCP (ms) tới Broker ngay cả khi đã kết nối hoặc chưa kết nối.

### Biên dịch & Chạy trên PC:
1. Mở `Mqtt_test.pro` bằng **Qt Creator**.
2. Chọn Kit `Desktop Qt 5.14.2 MSVC2017 64bit` (hoặc MinGW tương đương có module `QtMqtt`).
3. Nhấn **Ctrl + R** để biên dịch và chạy.

---

## 3. Ứng dụng Console trên Orange Pi (`Mqtt_cons`)

### Tính năng nổi bật (Phase 2):
- **Last Will and Testament (LWT) & Retained Status**:
  - Ngay khi kết nối, tự động phát bản tin Retained `{"device":"OrangePi_Client","status":"online"}` lên `orangepi/status`.
  - Nếu mất nguồn hoặc đứt mạng đột ngột, Broker tự động phát di chúc `{"device":"OrangePi_Client","status":"offline"}`.
  - Khi tắt ứng dụng bằng `:quit` hoặc nhấn `Ctrl+C` (`SIGINT`/`SIGTERM`), ứng dụng chủ động báo `offline` (Graceful Shutdown) trước khi đóng kết nối.
- **Đọc thông số phần cứng thực tế trên Linux (Hardware Telemetry)**:
  - Đọc trực tiếp nhiệt độ CPU SoC (`/sys/class/thermal/thermal_zone0/temp`), dung lượng RAM (`/proc/meminfo`), tải CPU (`/proc/loadavg`) và thời gian hoạt động (`/proc/uptime`).
- **Xử lý lệnh điều khiển từ xa (Command & Control từ PC)**:
  - Khi PC gửi lệnh qua `orangepi/cmd`, Orange Pi tự động thực thi và trả kết quả JSON về `orangepi/data`:
    - `temp` (hoặc `get_temp`): Trả về nhiệt độ CPU hiện tại (°C).
    - `ram` (hoặc `get_ram`): Trả về RAM đã dùng / tổng RAM (MB & %).
    - `sysinfo` (hoặc `status`): Trả về toàn bộ thông số phần cứng của Orange Pi.
    - `ping`: Phản hồi gói JSON `pong`.
    - `heartbeat <giây>`: Bật/tắt hoặc đổi chu kỳ tự động gửi thông số phần cứng từ xa (ví dụ `heartbeat 5` hoặc `heartbeat 0`).
    - `getfile <đường_dẫn_file>`: Yêu cầu Orange Pi gửi một file bất kỳ về PC (ví dụ: `getfile Mqtt_cons.pro` hoặc `getfile /etc/hostname`).
    - Hỗ trợ cả lệnh JSON: `{"cmd":"get_status"}`, `{"cmd":"set_heartbeat","interval":5}`, `{"cmd":"get_file","path":"..."}`.
- **Truyền & Nhận File 2 chiều trên Orange Pi**:
  - Khi PC gửi file xuống, Orange Pi tự động ghép các mảnh, đối chiếu mã băm `SHA-256`, lưu vào `./received_files/<tên_file>` và gửi gói xác nhận `file_ack` về PC.
  - Từ Terminal của Orange Pi, gõ `:sendfile <đường_dẫn_file>` để chủ động bắn bất kỳ file nào lên PC.
- **Các lệnh điều khiển trực tiếp tại Terminal của Orange Pi**:
  - `<tin_nhắn>` : Gửi tin nhắn trực tiếp lên Pub Topic (`orangepi/data`).
  - `:sendfile <đường_dẫn>` (hoặc `:send <đường_dẫn>`) : Gửi file từ Orange Pi lên PC.
  - `:sysinfo` (hoặc `:hw`) : Xem thông số phần cứng tại chỗ và đẩy gói JSON lên PC.
  - `:heartbeat <giây>` : Đặt chu kỳ tự động báo cáo thông số phần cứng (0 để tắt).
  - `:ping` : Đo độ trễ TCP (ms) tới Broker.
  - `:pub <topic> <nội_dung>` : Gửi tin nhắn đến một topic bất kỳ.
  - `:sub <topic>` / `:unsub <topic>` : Đăng ký thêm hoặc hủy đăng ký topic.
  - `:pubtopic [topic_mới]` / `:subtopic [topic_mới]` : Xem hoặc đổi topic mặc định.
  - `:status` : Xem trạng thái kết nối, cấu hình LWT và danh sách topic đang sub.
  - `:reconnect` : Ép kết nối lại Broker.
  - `:quit` (hoặc `:q`, hoặc nhấn `Ctrl+C`) : Báo offline và thoát ứng dụng an toàn.

### Cài đặt & Biên dịch trên Orange Pi (Ubuntu/Debian/Armbian):
```bash
# 1. Cập nhật code mới nhất từ GitHub
cd ~/Projects/MQTT_PC_pi/Mqtt_cons
git pull

# 2. Biên dịch
qmake Mqtt_cons.pro
make

# 3. Khởi chạy (Ví dụ kết nối Broker có xác thực mật khẩu và bật heartbeat 10s)
./Mqtt_cons -h 192.168.0.104 -p 1883 -u <username> -P <password> --heartbeat 10
```

### Các tham số dòng lệnh tùy chọn:
- `-h, --host <IP>`: Địa chỉ Broker (mặc định: `192.168.0.104`).
- `-p, --port <Port>`: Cổng Broker (mặc định: `1883`).
- `-s, --sub-topic <Topic>`: Topic nhận lệnh từ PC (mặc định: `orangepi/cmd`).
- `-d, --pub-topic <Topic>`: Topic gửi dữ liệu lên PC (mặc định: `orangepi/data`).
- `--status-topic <Topic>`: Topic báo trạng thái LWT (mặc định: `orangepi/status`).
- `-u, --user <Username>`: Tài khoản MQTT.
- `-P, --password <Password>`: Mật khẩu MQTT.
- `-i, --id <ClientID>`: Tên định danh thiết bị (mặc định: `OrangePi_Client`).
- `--heartbeat <giây>`: Chu kỳ gửi telemetry tự động tính bằng giây (mặc định: `0` - tắt).
