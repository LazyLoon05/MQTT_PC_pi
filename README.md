# MQTT Communication System (PC GUI & Orange Pi Console)

Hệ thống giao tiếp MQTT giữa máy tính PC (Giao diện đồ họa Qt Widgets) và bo mạch Orange Pi (Ứng dụng dòng lệnh Qt Console).

---

## 1. Cấu trúc thư mục

```text
├── Mqtt_test.pro          # File dự án Qt GUI Client cho PC
├── main.cpp               # Điểm bắt đầu PC GUI
├── mainwindow.h           # Header xử lý MQTT Client trên PC
├── mainwindow.cpp         # Xử lý logic MQTT & thăm dò mạng TCP Ping
├── mainwindow.ui          # Giao diện đồ họa thiết kế qua Qt Designer
│
├── Mqtt_cons/             # Ứng dụng Qt Console Client cho Orange Pi
│   ├── Mqtt_cons.pro      # File dự án Console Client
│   ├── main.cpp           # Điểm bắt đầu và phân tích tham số dòng lệnh
│   ├── mqttconsoleclient.h
│   └── mqttconsoleclient.cpp
│
└── README.md
```

---

## 2. Ứng dụng PC GUI (`Mqtt_test`)

### Chức năng chính:
- **Cấu hình kết nối**: Nhập Broker IP, Port (1883), Username, Password, tùy chọn Encryption (SSL/TLS).
- **Kết nối / Hủy**: Kết nối (`Connect`) và ngắt kết nối an toàn (`Cancel`).
- **Topic**: Đăng ký và xuất bản trên Topic chỉ định (mặc định `test/topic`).
- **Send & Recv**: Gửi tin nhắn (`Send`) và nhận tin nhắn hiển thị theo thời gian thực (`Recv`).
- **Ping**:
  - Khi đã kết nối: Gửi MQTT `PINGREQ` kiểm tra Broker keep-alive.
  - Khi chưa kết nối: Thăm dò cổng TCP Socket để kiểm tra máy Broker (Orange Pi / PC khác) có online và mở cổng 1883 không.
- **Trạng thái**: Tự động thông báo và dịch mã lỗi mạng/Broker.

### Biên dịch & Chạy trên PC:
1. Mở `Mqtt_test.pro` bằng **Qt Creator**.
2. Chọn Kit `Desktop Qt 5.14.2 MSVC2017 64bit` (hoặc MinGW tương đương có module `QtMqtt`).
3. Nhấn **Ctrl + R** để chạy.

---

## 3. Ứng dụng Console trên Orange Pi (`Mqtt_cons`)

### Chức năng chính:
- Chạy nền / không cần giao diện đồ họa (Headless) qua SSH terminal.
- Đọc lệnh và tin nhắn từ bàn phím không bị nghẽn (Non-blocking CLI worker).
- Tự động kết nối lại (Auto-reconnect) sau mỗi 5 giây nếu mất kết nối hoặc Broker khởi động lại.
- Hỗ trợ gửi tin nhắn tự động (Heartbeat báo sống định kỳ).
- Các lệnh điều khiển nhanh trong terminal:
  - `<tin_nhắn>` : Gửi tin nhắn trực tiếp lên topic mặc định.
  - `:ping` : Gửi yêu cầu MQTT Ping.
  - `:pub <topic> <nội_dung>` : Gửi tin nhắn đến topic riêng.
  - `:sub <topic>` : Đăng ký nhận tin từ topic mới.
  - `:topic [topic_mới]` : Xem hoặc đổi topic mặc định.
  - `:status` : Xem trạng thái kết nối hiện tại.
  - `:reconnect` : Ép kết nối lại Broker.
  - `:quit` (hoặc `:q`) : Thoát ứng dụng.

### Cài đặt & Biên dịch trên Orange Pi (Debian/Ubuntu):
```bash
# 1. Cập nhật và cài đặt công cụ biên dịch Qt
sudo apt update
sudo apt install -y build-essential qt5-qmake qtbase5-dev libqt5mqtt5-dev

# 2. Chuyển vào thư mục Mqtt_cons
cd Mqtt_cons

# 3. Biên dịch
qmake Mqtt_cons.pro
make

# 4. Khởi chạy
./Mqtt_cons -h 192.168.0.104 -p 1883 -t test/topic
```

### Các tham số dòng lệnh tùy chọn:
- `-h, --host <IP>`: Địa chỉ Broker (mặc định: `192.168.0.104`).
- `-p, --port <Port>`: Cổng Broker (mặc định: `1883`).
- `-t, --topic <Topic>`: Topic mặc định (mặc định: `test/topic`).
- `-i, --id <ClientID>`: Tên định danh thiết bị (mặc định: `OrangePi_Client`).
- `--heartbeat <giây>`: Tự động gửi gói tin JSON báo sống định kỳ (ví dụ: `--heartbeat 10`).

---

## 4. Thiết lập Broker Mosquitto (trên PC hoặc Orange Pi)

Nếu chạy Mosquitto 2.0+ trên Orange Pi hoặc PC làm Broker trung gian, hãy đảm bảo cấu hình `/etc/mosquitto/mosquitto.conf`:
```text
listener 1883
allow_anonymous true
```
Sau đó khởi động lại service:
```bash
sudo systemctl restart mosquitto
```
