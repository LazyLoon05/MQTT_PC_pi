# MQTT trên nền tảng Qt: PC ↔ Pi (bản v2, đã sửa theo review)

> **Trạng thái tài liệu:** mã nguồn trong file này được viết lại theo kết quả review nhưng **chưa được build hoặc chạy thử trên PC/Pi thật**. Khi build lần đầu có thể còn lỗi nhỏ về cú pháp hoặc phiên bản Qt; hãy xem mục 10 và ghi lại kết quả Pass/Fail thật.
> **Tính chất:** dự án thực hành, quy mô nhỏ. Không đặt mục tiêu nghiên cứu khoa học và không cam kết hiệu năng khi chưa có số liệu đo.

---

## 0. Bản v2 sửa gì so với bản đầu

| Mức | Vấn đề trong review | Cách xử lý trong v2 | Mục |
|---|---|---|---|
| P0 | GUI chỉ có cờ `connected` true/false | `MqttService` có trạng thái `Disconnected / Connecting / Connected / Reconnecting`; GUI hiển thị đúng trạng thái | 6.3, 8 |
| P0 | `publish()` bỏ qua im lặng khi mất kết nối | `publish()` trả `bool`, phát `publishFailed`, GUI/CLI ghi log | 6.3 |
| P0 | QoS 1 không chứng minh lệnh đã thực thi | Lệnh JSON có `request_id`; Pi trả ACK; PC có `CommandTracker` (timeout, thử lại 1 lần, báo `success/error/timeout/not_sent`) | 3, 6.4, 7 |
| P0 | Online/offline chỉ dựa vào LWT | Thêm `heartbeat` có `seq`; GUI có 4 trạng thái `online / stale / offline / unknown`, tính từ heartbeat | 3.3, 8 |
| P1 | Chỉ một `piOnline`, topic cứng `lab/pi/#` | GUI chọn **một node theo Node ID**; đa node thực sự cần `NodeManager` (mục 12), v2 không giả vờ đã hỗ trợ | 8, 12 |
| P1 | Trùng `clientId` | `clientId = nodeId + "-" + 8 ký tự ngẫu nhiên`; `nodeId` được kiểm tra hợp lệ | 6.1 |
| P1 | Nhiệt độ không kiểm tra hợp lệ | Quét nhiều `thermal_zone`, kiểm tra khoảng giá trị, payload có `valid` | 7 |
| P1 | Reconnect cố định 3 giây, timer lỏng lẻo | Backoff 1→30 giây có jitter, chỉ kết nối khi đang `Disconnected`, dừng timer khi `stop()` | 6.3 |
| P1 | Mật khẩu mặc định nằm trong code/CLI | Không còn mật khẩu mặc định; Pi đọc từ file/biến môi trường; có ACL theo user | 5, 7, 9 |
| P1 | Hướng dẫn build chưa xác minh | Quy trình xác minh phiên bản Qt, tìm đúng tag `qtmqtt`, kiểm tra module sau khi cài | 4 |
| P2 | Không có test và đo lường | Bộ 14 kịch bản kiểm thử, có cột kết quả để điền | 10 |

---

## 1. Mục tiêu và phạm vi

**Làm trong tài liệu này:**

1. Pi chạy broker Mosquitto và chương trình Qt `pi_node` gửi nhiệt độ CPU, heartbeat, nhận lệnh.
2. PC chạy ứng dụng Qt Quick `pc_gui`: xem trạng thái, dữ liệu, gửi lệnh và biết lệnh có được thực hiện hay không.
3. Ghi nhận hành vi khi mất mạng, restart broker, tắt đột ngột bằng bộ kịch bản kiểm thử.

**Chưa làm:** Pi thứ hai (chỉ chừa chỗ), STM32F407, TLS, lưu dữ liệu lâu dài.

---

## 2. Kiến trúc

```
                 LAN / Wi-Fi
┌───────────────────────────┐          ┌──────────────────────────────┐
│ PC                        │          │ Pi (Linux)                   │
│  pc_gui (Qt Quick)        │          │  Mosquitto :1883 (broker)    │
│   └ NodeController        │   MQTT   │  pi_node (Qt Core)           │
│      ├ CommandTracker     │◄────────►│   ├ telemetry + heartbeat    │
│      └ MqttService        │          │   ├ command handler (dedupe) │
└───────────────────────────┘          │   └ MqttService              │
                                       └──────────────────────────────┘
```

- Hai bên là **client** của broker, không gửi trực tiếp cho nhau.
- Broker nằm cùng Pi, nên khi Pi hỏng hoặc broker dừng thì toàn bộ trao đổi dừng theo. Đây là giới hạn của mô hình, không phải lỗi code.
- Thư mục dự án:

```
mqtt_qt/
├── CMakeLists.txt
├── core/
│   ├── CMakeLists.txt
│   ├── Config.h
│   ├── Protocol.h
│   ├── MqttService.h / .cpp
│   └── CommandTracker.h / .cpp
├── pi_node/
│   └── main.cpp
└── pc_gui/
    ├── main.cpp
    ├── NodeController.h / .cpp
    └── Main.qml
```

---

## 3. Giao thức

### 3.1. Topic

Quy ước `lab/<node>/<loại>/<tên>`; `<node>` chỉ gồm `a-z 0-9 _ -` (tối đa 32 ký tự, để chặn ký tự `+`, `#`, `/` lọt vào topic).

| Topic | Hướng | Payload | QoS | Retain |
|---|---|---|---|---|
| `lab/pi/status` | Pi → PC | `online` / `offline` (LWT dùng chung topic) | 1 | có |
| `lab/pi/heartbeat` | Pi → PC | JSON, mỗi 5 giây | 0 | không |
| `lab/pi/sensor/cpu_temp` | Pi → PC | JSON, chu kỳ chỉnh được | 0 | không |
| `lab/pi/cmd/ping` | PC → Pi | JSON có `request_id` | 1 | không |
| `lab/pi/cmd/set_interval` | PC → Pi | JSON có `request_id`, `value` (ms) | 1 | không |
| `lab/pi/reply/<lệnh>` | Pi → PC | JSON ACK có `request_id` | 1 | không |
| `lab/pc/status` | PC → broker | `online` / `offline` | 1 | có |

> Khác bản đầu: `cmd/interval` đổi thành `cmd/set_interval`; payload chuyển từ chuỗi thô sang JSON.

### 3.2. Payload mẫu

Telemetry:
```json
{"seq":42,"ts_ms":1760000000123,"value":47.8,"valid":true}
{"seq":43,"ts_ms":1760000002123,"value":null,"valid":false,"error":"không đọc được thermal zone"}
```

Heartbeat:
```json
{"seq":7,"ts_ms":1760000000123,"uptime_s":3600,"interval_ms":2000}
```

Lệnh (PC → Pi):
```json
{"request_id":"req-3f9a1c0b7d2e","command":"set_interval","value":500,"ts_ms":1760000000123}
```

ACK (Pi → PC):
```json
{"request_id":"req-3f9a1c0b7d2e","command":"set_interval","status":"success","applied_value":500,"ts_ms":1760000000140}
{"request_id":"req-3f9a1c0b7d2e","command":"set_interval","status":"error","error":"value phải là số nguyên 200..60000","ts_ms":1760000000140}
```

> `ts_ms` lấy theo đồng hồ từng máy. **Không dùng** để tính độ trễ một chiều nếu chưa đồng bộ giờ (NTP). Thời gian phản hồi (RTT) của lệnh được đo hoàn toàn bằng đồng hồ phía PC nên không bị ảnh hưởng.

### 3.3. Mô hình trạng thái node (phía PC)

| Trạng thái | Điều kiện |
|---|---|
| `unknown` | Chưa kết nối broker, hoặc chưa nhận `status` |
| `online` | `status = online` **và** heartbeat gần nhất cách đây ≤ 15 giây |
| `stale` | `status = online` nhưng không có heartbeat > 15 giây (tiến trình treo, mạng chập chờn, broker chưa kịp phát hiện) |
| `offline` | `status = offline` (do LWT hoặc Pi tự báo khi tắt) |

### 3.4. Chính sách lệnh

- Mỗi lệnh có `request_id` duy nhất.
- PC chờ ACK tối đa 3 giây, **gửi lại 1 lần với cùng `request_id`**, sau đó báo `timeout`.
- Pi lưu 64 `request_id` gần nhất cùng ACK. Nếu nhận lại cùng `request_id`, Pi gửi lại ACK cũ và **không thực hiện lại** lệnh.
- Kết quả cuối cùng của một lệnh là một trong: `success`, `error`, `timeout`, `not_sent`.

> QoS 1 chỉ đảm bảo tin MQTT được chuyển ít nhất một lần trong phạm vi giao thức. Việc lệnh đã được thực hiện hay chưa chỉ biết qua ACK ở tầng ứng dụng.

---

## 4. Chuẩn bị môi trường

### 4.1. Gói cơ bản trên Pi

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients \
    build-essential cmake ninja-build git qt6-base-dev
```

Các lệnh dùng được cho Raspberry Pi OS / Debian / Armbian (Orange Pi). Tên gói có thể khác theo bản phân phối; nếu `apt` báo không tìm thấy, dùng `apt search qt6-base` để xem tên thật.

### 4.2. Qt MQTT (xác minh trước khi build)

1. Kiểm tra gói có sẵn:
   ```bash
   apt search qt6 | grep -i mqtt
   ```
   Nếu có gói `-dev` của Qt MQTT thì cài bằng apt và bỏ qua các bước build bên dưới.

2. Nếu không có, xác định **đúng phiên bản Qt** đang cài:
   ```bash
   qmake6 --version        # hoặc: qtpaths6 --qt-version
   ```
3. Lấy mã nguồn và chọn **tag trùng phiên bản** (ví dụ Qt 6.4.2 → `v6.4.2`); chỉ dùng nhánh `6.4` nếu không có tag khớp:
   ```bash
   git clone https://code.qt.io/qt/qtmqtt.git
   cd qtmqtt
   git tag -l 'v6.4*'                 # thay 6.4 bằng phiên bản của bạn
   git checkout v6.4.2
   ```
4. Build bằng `qt-cmake` (script đi kèm Qt, tự dùng đúng prefix):
   ```bash
   mkdir build && cd build
   /usr/lib/qt6/bin/qt-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
   ninja
   sudo ninja install
   ```
   Nếu CMake báo thiếu private headers hoặc module khác (ví dụ WebSockets), cài gói `-dev` tương ứng theo thông báo (thường là `qt6-base-private-dev`).
5. **Xác minh đã cài đúng:**
   ```bash
   find /usr /usr/local -name "Qt6MqttConfig.cmake" 2>/dev/null
   ```
   Phải thấy ít nhất một đường dẫn. Khi build dự án, nếu CMake vẫn không tìm thấy, thêm `-DCMAKE_PREFIX_PATH=<thư mục chứa lib/cmake/Qt6Mqtt>`.

> Qt MQTT theo giấy phép GPLv3 hoặc thương mại. Nếu muốn tránh, có thể thay bằng thư viện khác; khi đó chỉ phải sửa `core/MqttService.*`.

### 4.3. PC

- Qt Online Installer: chọn **Qt 6.5 trở lên** (GUI dùng `loadFromModule`), kèm *Qt Quick* và *Additional Libraries → Qt MQTT*.
- Cài MQTT Explorer (tùy chọn) để quan sát topic.
- Pi **không cần** Qt Quick vì `pi_node` không có giao diện.

### 4.4. Mạng

- PC và Pi cùng mạng; nên cố định IP Pi (DHCP reservation).
- Nếu dùng `ufw`: `sudo ufw allow from 192.168.1.0/24 to any port 1883 proto tcp` (đổi dải mạng cho đúng). Không mở broker ra Internet khi chưa có TLS.

---

## 5. Broker Mosquitto: xác thực và phân quyền

### 5.1. Cấu hình

`/etc/mosquitto/conf.d/local.conf`:

```conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl
```

> Không thêm dòng `persistence ...` vào file này: cấu hình mặc định của gói Debian đã có, thêm trùng có thể gây lỗi khởi động.

### 5.2. Tạo hai tài khoản riêng

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd pi_user     # -c chỉ dùng lần ĐẦU (tạo mới file)
sudo mosquitto_passwd /etc/mosquitto/passwd pc_user        # KHÔNG thêm -c, nếu không sẽ xóa user cũ
```

### 5.3. ACL tối thiểu

`/etc/mosquitto/acl`:

```conf
user pi_user
topic read  lab/pi/cmd/#
topic write lab/pi/status
topic write lab/pi/heartbeat
topic write lab/pi/sensor/#
topic write lab/pi/reply/#

user pc_user
topic read  lab/pi/#
topic write lab/pi/cmd/#
topic write lab/pc/status
```

```bash
sudo systemctl restart mosquitto
sudo journalctl -u mosquitto -n 30 --no-pager      # xem có lỗi cấu hình không
```

> Với MQTT 3.1.1, publish bị ACL từ chối thường **bị bỏ lặng lẽ** (client không nhận lỗi); subscribe bị từ chối trả mã lỗi, `MqttService` ghi log trường hợp này.

### 5.4. Kiểm tra broker (chưa cần Qt)

```bash
# Pi
mosquitto_sub -h localhost -u pc_user -P '<mk_pc>' -t 'lab/#' -v
# PC (hoặc máy khác)
mosquitto_pub -h <IP_Pi> -u pi_user -P '<mk_pi>' -t 'lab/pi/status' -m 'online' -r
```

Chỉ sang phần code Qt khi bước này chạy đúng.

---

## 6. Mã nguồn `core`

### 6.1. `CMakeLists.txt` (gốc)

```cmake
cmake_minimum_required(VERSION 3.21)
project(mqtt_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Mqtt)
add_subdirectory(core)

# Pi:  cmake -S . -B build -G Ninja -DBUILD_PC_GUI=OFF
# PC:  cmake -S . -B build -G Ninja -DBUILD_PI_NODE=OFF
option(BUILD_PI_NODE "Build pi_node" ON)
option(BUILD_PC_GUI  "Build pc_gui"  ON)

if(BUILD_PI_NODE)
    add_executable(pi_node pi_node/main.cpp)
    target_link_libraries(pi_node PRIVATE core)
endif()

if(BUILD_PC_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
    qt_add_executable(pc_gui
        pc_gui/main.cpp pc_gui/NodeController.cpp pc_gui/NodeController.h)
    qt_add_qml_module(pc_gui URI MqttGui VERSION 1.0 QML_FILES pc_gui/Main.qml)
    target_link_libraries(pc_gui PRIVATE core Qt6::Quick)
endif()
```

### 6.2. `core/CMakeLists.txt`

```cmake
add_library(core STATIC
    Config.h Protocol.h
    MqttService.h MqttService.cpp
    CommandTracker.h CommandTracker.cpp)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC Qt6::Core Qt6::Mqtt)
```

### 6.3. `core/Config.h`

```cpp
#pragma once
#include <QRegularExpression>
#include <QString>
#include <QUuid>

struct Config {
    QString nodeId = "pi";        // định danh của chương trình này: "pi", "pc", ...
    QString host   = "localhost";
    quint16 port   = 1883;
    QString user;                 // không có giá trị mặc định
    QString password;             // không có giá trị mặc định
    QString base   = "lab";

    // Chỉ cho phép a-z 0-9 _ - để không lọt ký tự wildcard/phân cấp vào topic.
    static bool isValidId(const QString &id)
    {
        static const QRegularExpression re("^[a-z0-9_-]{1,32}$");
        return re.match(id).hasMatch();
    }

    QString nodeTopic(const QString &node, const QString &suffix) const
    {
        return QString("%1/%2/%3").arg(base, node, suffix);
    }
    QString statusTopic() const { return nodeTopic(nodeId, "status"); }

    // Mỗi lần tạo service dùng một clientId riêng để hai tiến trình cùng nodeId
    // không đá nhau khỏi broker.
    QString clientId() const
    {
        return nodeId + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    }
};
```

### 6.4. `core/Protocol.h`

```cpp
#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace Protocol {

inline qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

inline QByteArray toJson(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

inline bool parseObject(const QByteArray &data, QJsonObject &out)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}

inline QString newRequestId()
{
    return "req-" + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

} // namespace Protocol
```

### 6.5. `core/MqttService.h`

```cpp
#pragma once
#include <QList>
#include <QObject>
#include <QPair>
#include <QTimer>
#include <QtMqtt/QMqttClient>
#include "Config.h"

class MqttService : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Reconnecting };
    Q_ENUM(State)

    explicit MqttService(const Config &cfg, QObject *parent = nullptr);

    void start();
    void stop();

    State state() const { return m_state; }
    bool isConnected() const { return m_client.state() == QMqttClient::Connected; }

    // true = đã giao cho QMqttClient (chưa có nghĩa broker/đích đã nhận).
    bool publish(const QString &topic, const QByteArray &payload,
                 quint8 qos = 0, bool retain = false);
    // Lưu filter và tự đăng ký lại sau mỗi lần kết nối.
    bool subscribe(const QString &filter, quint8 qos = 0);

signals:
    void stateChanged(MqttService::State state);
    void messageReceived(const QString &topic, const QByteArray &payload);
    void publishFailed(const QString &topic, const QString &reason);
    void logMessage(const QString &text);

private slots:
    void onClientState(QMqttClient::ClientState s);
    void onMessage(const QByteArray &msg, const QMqttTopicName &topic);

private:
    void setState(State s);
    void tryConnect();
    void scheduleReconnect();
    bool doSubscribe(const QString &filter, quint8 qos);
    void resubscribeAll();

    Config m_cfg;
    QMqttClient m_client;
    QTimer m_reconnect;
    QList<QPair<QString, quint8>> m_subs;
    State m_state = State::Disconnected;
    int m_attempt = 0;
    bool m_wantRun = false;
};
```

### 6.6. `core/MqttService.cpp`

```cpp
#include "MqttService.h"
#include <QRandomGenerator>
#include <utility>

MqttService::MqttService(const Config &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg)
{
    m_client.setHostname(cfg.host);
    m_client.setPort(cfg.port);
    if (!cfg.user.isEmpty()) m_client.setUsername(cfg.user);
    m_client.setPassword(cfg.password);
    m_client.setClientId(cfg.clientId());
    m_client.setKeepAlive(15);          // broker thường phát hiện mất kết nối sau ~1,5 × keepalive

    // Last Will: broker tự phát "offline" khi client biến mất đột ngột.
    m_client.setWillTopic(cfg.statusTopic());
    m_client.setWillMessage("offline");
    m_client.setWillQoS(1);
    m_client.setWillRetain(true);

    connect(&m_client, &QMqttClient::stateChanged, this, &MqttService::onClientState);
    connect(&m_client, &QMqttClient::messageReceived, this, &MqttService::onMessage);
    connect(&m_client, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError e) {
        if (e != QMqttClient::NoError)
            emit logMessage(QString("Lỗi MQTT (mã %1)").arg(static_cast<int>(e)));
    });

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &MqttService::tryConnect);
}

void MqttService::start()
{
    if (m_wantRun) return;                 // chống gọi start() nhiều lần
    m_wantRun = true;
    m_attempt = 0;
    setState(State::Connecting);
    tryConnect();
}

void MqttService::stop()
{
    m_wantRun = false;
    m_reconnect.stop();
    if (isConnected()) {
        // Ngắt chủ động KHÔNG kích hoạt LWT, nên tự báo offline.
        // Lưu ý: tin này có thể chưa kịp gửi hết trước khi đóng kết nối;
        // phía PC vẫn có heartbeat/stale làm lớp bảo vệ thứ hai.
        m_client.publish(QMqttTopicName(m_cfg.statusTopic()), "offline", 1, true);
    }
    m_client.disconnectFromHost();
    setState(State::Disconnected);
}

void MqttService::tryConnect()
{
    if (!m_wantRun) return;
    if (m_client.state() != QMqttClient::Disconnected) return;   // không gọi chồng
    m_client.connectToHost();
}

void MqttService::scheduleReconnect()
{
    setState(State::Reconnecting);
    // Backoff: 1, 2, 4, 8, 16, 30, 30... giây, cộng jitter 0-500 ms.
    const int base = qMin(30000, 1000 << qMin(m_attempt, 5));
    const int delay = base + static_cast<int>(QRandomGenerator::global()->bounded(500));
    ++m_attempt;
    emit logMessage(QString("Không có kết nối; thử lại sau %1 ms").arg(delay));
    m_reconnect.start(delay);
}

bool MqttService::publish(const QString &topic, const QByteArray &payload,
                          quint8 qos, bool retain)
{
    if (!isConnected()) {
        emit publishFailed(topic, "chưa kết nối broker");
        return false;
    }
    if (m_client.publish(QMqttTopicName(topic), payload, qos, retain) < 0) {
        emit publishFailed(topic, "QMqttClient::publish báo lỗi");
        return false;
    }
    return true;
}

bool MqttService::subscribe(const QString &filter, quint8 qos)
{
    m_subs.append({filter, qos});
    return isConnected() ? doSubscribe(filter, qos) : true;   // sẽ đăng ký khi kết nối xong
}

bool MqttService::doSubscribe(const QString &filter, quint8 qos)
{
    QMqttSubscription *sub = m_client.subscribe(QMqttTopicFilter(filter), qos);
    if (!sub) {
        emit logMessage("Subscribe thất bại: " + filter);
        return false;
    }
    connect(sub, &QMqttSubscription::stateChanged, this,
            [this, filter](QMqttSubscription::SubscriptionState st) {
        if (st == QMqttSubscription::Error)
            emit logMessage("Broker từ chối subscribe (kiểm tra ACL): " + filter);
    });
    return true;
}

void MqttService::resubscribeAll()
{
    for (const auto &s : std::as_const(m_subs))
        doSubscribe(s.first, s.second);
}

void MqttService::onClientState(QMqttClient::ClientState s)
{
    switch (s) {
    case QMqttClient::Connecting:
        if (m_state != State::Reconnecting) setState(State::Connecting);
        break;
    case QMqttClient::Connected:
        m_attempt = 0;
        m_reconnect.stop();
        emit logMessage("Đã kết nối broker");
        resubscribeAll();
        publish(m_cfg.statusTopic(), "online", 1, true);
        setState(State::Connected);
        break;
    case QMqttClient::Disconnected:
        if (m_wantRun) scheduleReconnect();
        else setState(State::Disconnected);
        break;
    }
}

void MqttService::onMessage(const QByteArray &msg, const QMqttTopicName &topic)
{
    emit messageReceived(topic.name(), msg);
}

void MqttService::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}
```

### 6.7. `core/CommandTracker.h`

```cpp
#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QTimer>
#include "Config.h"
#include "MqttService.h"

// Gửi lệnh có request_id, chờ ACK, thử lại và báo kết quả cuối cùng.
class CommandTracker : public QObject {
    Q_OBJECT
public:
    CommandTracker(MqttService *mqtt, const Config &cfg, QObject *parent = nullptr);

    // Trả về request_id. Kết quả đến qua tín hiệu finished().
    QString send(const QString &node, const QString &command,
                 const QJsonValue &value = QJsonValue());

    // Gọi với mọi tin nhận được; trả true nếu là topic reply.
    bool handleMessage(const QString &topic, const QByteArray &payload);

signals:
    // status: "success" | "error" | "timeout" | "not_sent"
    // rttMs: thời gian từ lần gửi gần nhất đến khi có ACK (đồng hồ PC), -1 nếu không có.
    void finished(const QString &requestId, const QString &command,
                  const QString &status, const QString &detail, qint64 rttMs);

private:
    struct Pending {
        QString topic;
        QString command;
        QByteArray payload;
        int attempts = 0;
        QElapsedTimer sinceLast;
    };

    bool transmit(Pending &p);
    void onTick();

    MqttService *m_mqtt;
    Config m_cfg;
    QHash<QString, Pending> m_pending;
    QTimer m_tick;

    static constexpr int kTimeoutMs = 3000;
    static constexpr int kMaxAttempts = 2;      // gửi lần đầu + 1 lần thử lại
};
```

### 6.8. `core/CommandTracker.cpp`

```cpp
#include "CommandTracker.h"
#include "Protocol.h"
#include <QStringList>
#include <utility>

CommandTracker::CommandTracker(MqttService *mqtt, const Config &cfg, QObject *parent)
    : QObject(parent), m_mqtt(mqtt), m_cfg(cfg)
{
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &CommandTracker::onTick);
    m_tick.start();
}

bool CommandTracker::transmit(Pending &p)
{
    ++p.attempts;
    p.sinceLast.restart();
    return m_mqtt->publish(p.topic, p.payload, 1);
}

QString CommandTracker::send(const QString &node, const QString &command, const QJsonValue &value)
{
    const QString id = Protocol::newRequestId();

    QJsonObject obj{{"request_id", id}, {"command", command}, {"ts_ms", Protocol::nowMs()}};
    if (!value.isNull()) obj["value"] = value;

    Pending p;
    p.topic = m_cfg.nodeTopic(node, "cmd/" + command);
    p.command = command;
    p.payload = Protocol::toJson(obj);

    if (!transmit(p)) {
        emit finished(id, command, "not_sent", "chưa kết nối broker", -1);
        return id;
    }
    m_pending.insert(id, p);
    return id;
}

void CommandTracker::onTick()
{
    QStringList expired;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        if (it->sinceLast.elapsed() >= kTimeoutMs)
            expired << it.key();

    for (const QString &id : std::as_const(expired)) {
        auto it = m_pending.find(id);
        if (it == m_pending.end()) continue;

        if (it->attempts < kMaxAttempts) {
            if (transmit(*it)) continue;                 // gửi lại cùng request_id
            const QString cmd = it->command;
            m_pending.erase(it);
            emit finished(id, cmd, "not_sent", "mất kết nối khi gửi lại", -1);
            continue;
        }
        const QString cmd = it->command;
        m_pending.erase(it);
        emit finished(id, cmd, "timeout",
                      QString("không có phản hồi sau %1 lần gửi").arg(kMaxAttempts), -1);
    }
}

bool CommandTracker::handleMessage(const QString &topic, const QByteArray &payload)
{
    // Dạng: <base>/<node>/reply/<lệnh>
    const QStringList parts = topic.split('/');
    if (parts.size() != 4 || parts[0] != m_cfg.base || parts[2] != "reply")
        return false;

    QJsonObject obj;
    if (!Protocol::parseObject(payload, obj)) return false;

    const QString id = obj.value("request_id").toString();
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return true;              // ACK muộn hoặc lặp: bỏ qua

    const qint64 rtt = it->sinceLast.elapsed();
    const QString cmd = it->command;
    m_pending.erase(it);

    const QString status = obj.value("status").toString("error");
    QString detail;
    if (obj.contains("error"))
        detail = obj.value("error").toString();
    else if (obj.contains("applied_value"))
        detail = QString::number(obj.value("applied_value").toInt());

    emit finished(id, cmd, status, detail, rtt);
    return true;
}
```

---

## 7. `pi_node/main.cpp`

Chức năng: gửi nhiệt độ CPU và heartbeat; nhận lệnh `ping`, `set_interval`; trả ACK; bỏ qua lệnh trùng `request_id`; thoát êm khi nhận `SIGINT/SIGTERM`.

```cpp
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QQueue>
#include <QTimer>
#include <QDebug>
#include <csignal>
#include "MqttService.h"
#include "Protocol.h"

static volatile std::sig_atomic_t g_stop = 0;
static void onSignal(int) { g_stop = 1; }

struct TempReading {
    bool valid = false;
    double celsius = 0.0;
    QString error;
};

// Quét các thermal zone (số lượng và thứ tự khác nhau giữa các board).
static TempReading readCpuTemp()
{
    for (int i = 0; i < 10; ++i) {
        QFile f(QString("/sys/class/thermal/thermal_zone%1/temp").arg(i));
        if (!f.exists() || !f.open(QIODevice::ReadOnly)) continue;
        bool ok = false;
        const double milli = f.readAll().trimmed().toDouble(&ok);
        if (!ok) continue;
        const double c = milli / 1000.0;
        if (c < -20.0 || c > 125.0)
            return {false, c, "giá trị ngoài khoảng hợp lệ"};
        return {true, c, QString()};
    }
    return {false, 0.0, "không đọc được thermal zone"};
}

static QString readPassword(const QCommandLineParser &p)
{
    if (p.isSet("pass-file")) {
        QFile f(p.value("pass-file"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(f.readLine()).trimmed();
    }
    return qEnvironmentVariable("MQTT_PASSWORD");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{message}");

    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({"host", "Địa chỉ broker", "host", "localhost"});
    p.addOption({"port", "Cổng broker", "port", "1883"});
    p.addOption({"user", "MQTT user (hoặc biến MQTT_USER)", "user"});
    p.addOption({"pass-file", "File chứa mật khẩu (hoặc biến MQTT_PASSWORD)", "file"});
    p.addOption({"id", "Node id (a-z 0-9 _ -)", "id", "pi"});
    p.process(app);

    Config cfg;
    cfg.nodeId = p.value("id");
    cfg.host = p.value("host");
    cfg.port = static_cast<quint16>(p.value("port").toUInt());
    cfg.user = p.isSet("user") ? p.value("user") : qEnvironmentVariable("MQTT_USER");
    cfg.password = readPassword(p);

    if (!Config::isValidId(cfg.nodeId)) {
        qCritical() << "Node id không hợp lệ";
        return 2;
    }
    if (cfg.user.isEmpty() || cfg.password.isEmpty()) {
        qCritical() << "Thiếu user hoặc mật khẩu (--user, --pass-file hoặc biến môi trường)";
        return 2;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    MqttService mqtt(cfg);
    QObject::connect(&mqtt, &MqttService::logMessage,
                     [&](const QString &s) { qInfo().noquote() << s; });
    QObject::connect(&mqtt, &MqttService::publishFailed,
                     [&](const QString &t, const QString &why) {
        qWarning().noquote() << "Gửi thất bại" << t << ":" << why;
    });

    const QString cmdPrefix   = cfg.nodeTopic(cfg.nodeId, "cmd/");
    const QString replyPrefix = cfg.nodeTopic(cfg.nodeId, "reply/");
    mqtt.subscribe(cmdPrefix + "#", 1);

    QElapsedTimer uptime;
    uptime.start();
    qint64 sensorSeq = 0, hbSeq = 0;

    // --- Telemetry ---
    QTimer sensorTimer;
    sensorTimer.setInterval(2000);
    QObject::connect(&sensorTimer, &QTimer::timeout, [&] {
        const TempReading t = readCpuTemp();
        QJsonObject o{{"seq", ++sensorSeq},
                      {"ts_ms", Protocol::nowMs()},
                      {"value", t.valid ? QJsonValue(qRound(t.celsius * 10) / 10.0)
                                        : QJsonValue(QJsonValue::Null)},
                      {"valid", t.valid}};
        if (!t.valid) o["error"] = t.error;
        mqtt.publish(cfg.nodeTopic(cfg.nodeId, "sensor/cpu_temp"), Protocol::toJson(o), 0);
    });
    sensorTimer.start();

    // --- Heartbeat ---
    auto sendHeartbeat = [&] {
        QJsonObject o{{"seq", ++hbSeq},
                      {"ts_ms", Protocol::nowMs()},
                      {"uptime_s", uptime.elapsed() / 1000},
                      {"interval_ms", sensorTimer.interval()}};
        mqtt.publish(cfg.nodeTopic(cfg.nodeId, "heartbeat"), Protocol::toJson(o), 0);
    };
    QTimer hbTimer;
    hbTimer.setInterval(5000);
    QObject::connect(&hbTimer, &QTimer::timeout, sendHeartbeat);
    hbTimer.start();
    QObject::connect(&mqtt, &MqttService::stateChanged, [&](MqttService::State s) {
        if (s == MqttService::State::Connected) sendHeartbeat();   // báo ngay khi kết nối
    });

    // --- Xử lý lệnh, chống xử lý lặp theo request_id ---
    QHash<QString, QByteArray> replyCache;
    QQueue<QString> replyOrder;
    constexpr int kCacheSize = 64;

    QObject::connect(&mqtt, &MqttService::messageReceived,
                     [&](const QString &topic, const QByteArray &payload) {
        if (!topic.startsWith(cmdPrefix)) return;
        const QString cmd = topic.mid(cmdPrefix.size());

        QJsonObject req;
        const bool parsed = Protocol::parseObject(payload, req);
        const QString id = req.value("request_id").toString();
        if (!parsed || id.isEmpty()) {
            QJsonObject bad{{"request_id", id}, {"command", cmd}, {"status", "error"},
                            {"error", "payload không hợp lệ hoặc thiếu request_id"},
                            {"ts_ms", Protocol::nowMs()}};
            mqtt.publish(replyPrefix + cmd, Protocol::toJson(bad), 1);
            return;
        }

        if (replyCache.contains(id)) {                       // lệnh trùng: không thực hiện lại
            qInfo().noquote() << "Lệnh trùng" << id << "- gửi lại ACK cũ";
            mqtt.publish(replyPrefix + cmd, replyCache.value(id), 1);
            return;
        }

        QJsonObject res{{"request_id", id}, {"command", cmd}};
        if (cmd == "ping") {
            res["status"] = "success";
        } else if (cmd == "set_interval") {
            const QJsonValue v = req.value("value");
            const double d = v.toDouble(-1);
            const int ms = static_cast<int>(d);
            if (!v.isDouble() || d != ms || ms < 200 || ms > 60000) {
                res["status"] = "error";
                res["error"] = "value phải là số nguyên 200..60000";
            } else {
                sensorTimer.setInterval(ms);
                res["status"] = "success";
                res["applied_value"] = ms;
            }
        } else {
            res["status"] = "error";
            res["error"] = "lệnh không được hỗ trợ";
        }
        res["ts_ms"] = Protocol::nowMs();

        const QByteArray out = Protocol::toJson(res);
        replyCache.insert(id, out);
        replyOrder.enqueue(id);
        if (replyOrder.size() > kCacheSize) replyCache.remove(replyOrder.dequeue());

        qInfo().noquote() << "Lệnh" << cmd << id << "->" << res.value("status").toString();
        mqtt.publish(replyPrefix + cmd, out, 1);
    });

    // --- Thoát êm khi nhận SIGINT/SIGTERM ---
    QTimer sigTimer;
    sigTimer.setInterval(200);
    QObject::connect(&sigTimer, &QTimer::timeout, [&] {
        if (!g_stop) return;
        sigTimer.stop();
        mqtt.stop();                                         // báo offline rồi ngắt
        QTimer::singleShot(300, &app, &QCoreApplication::quit);   // chờ gói tin kịp gửi
    });
    sigTimer.start();

    mqtt.start();
    return app.exec();
}
```

Build và chạy trên Pi:

```bash
cd ~/mqtt_qt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_PC_GUI=OFF
cmake --build build
echo -n '<mk_pi>' > ~/pi_node.pass && chmod 600 ~/pi_node.pass
./build/pi_node --host localhost --user pi_user --pass-file ~/pi_node.pass
```

Kiểm tra bằng dòng lệnh từ PC **trước khi làm GUI**:

```bash
mosquitto_sub -h <IP_Pi> -u pc_user -P '<mk_pc>' -t 'lab/pi/#' -v
mosquitto_pub -h <IP_Pi> -u pc_user -P '<mk_pc>' -t 'lab/pi/cmd/ping' -q 1 \
  -m '{"request_id":"manual-1","command":"ping"}'
```

---

## 8. `pc_gui`

### 8.1. `pc_gui/NodeController.h`

```cpp
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include "CommandTracker.h"
#include "MqttService.h"

class NodeController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
    Q_PROPERTY(QString nodeState READ nodeState NOTIFY nodeStateChanged)
    Q_PROPERTY(QString cpuTemp READ cpuTemp NOTIFY telemetryChanged)
    Q_PROPERTY(bool tempValid READ tempValid NOTIFY telemetryChanged)
    Q_PROPERTY(int dataAgeSec READ dataAgeSec NOTIFY telemetryChanged)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY lastResultChanged)
    Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
public:
    explicit NodeController(QObject *parent = nullptr);

    QString connectionState() const { return m_connState; }
    bool connected() const { return m_connState == "Connected"; }
    QString nodeState() const { return m_nodeState; }
    QString cpuTemp() const { return m_cpuTemp; }
    bool tempValid() const { return m_tempValid; }
    int dataAgeSec() const;
    QString lastResult() const { return m_lastResult; }
    QStringList log() const { return m_log; }

    Q_INVOKABLE void connectBroker(const QString &host, int port, const QString &user,
                                   const QString &pass, const QString &node);
    Q_INVOKABLE void disconnectBroker();
    Q_INVOKABLE void ping();
    Q_INVOKABLE void setInterval(int ms);

signals:
    void connectionStateChanged();
    void nodeStateChanged();
    void telemetryChanged();
    void lastResultChanged();
    void logChanged();

private slots:
    void onMqttState(MqttService::State s);
    void onMessage(const QString &topic, const QByteArray &payload);
    void onCommandFinished(const QString &id, const QString &command, const QString &status,
                           const QString &detail, qint64 rttMs);
    void onTick();

private:
    void addLog(const QString &s);
    void resetNodeInfo();
    void evaluateNodeState();

    Config m_cfg;
    QString m_node = "pi";
    MqttService *m_mqtt = nullptr;
    CommandTracker *m_tracker = nullptr;
    QTimer m_tick;

    QString m_connState = "Disconnected";
    QString m_nodeState = "unknown";
    QString m_statusPayload;                 // "", "online", "offline"
    bool m_hasHeartbeat = false;
    bool m_hasTelemetry = false;
    QElapsedTimer m_sinceHeartbeat;
    QElapsedTimer m_sinceTelemetry;
    QString m_cpuTemp = "--";
    bool m_tempValid = false;
    QString m_lastResult;
    QStringList m_log;
};
```

### 8.2. `pc_gui/NodeController.cpp`

```cpp
#include "NodeController.h"
#include "Protocol.h"
#include <QTime>

namespace { constexpr qint64 kStaleMs = 15000; }

NodeController::NodeController(QObject *parent) : QObject(parent)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &NodeController::onTick);
    m_tick.start();
}

int NodeController::dataAgeSec() const
{
    return m_hasTelemetry ? static_cast<int>(m_sinceTelemetry.elapsed() / 1000) : -1;
}

void NodeController::connectBroker(const QString &host, int port, const QString &user,
                                   const QString &pass, const QString &node)
{
    if (!Config::isValidId(node)) {
        addLog("Node ID không hợp lệ (chỉ a-z, 0-9, _, -; tối đa 32 ký tự)");
        return;
    }
    if (user.isEmpty() || pass.isEmpty()) {
        addLog("Cần nhập user và mật khẩu");
        return;
    }
    disconnectBroker();

    m_cfg = Config();
    m_cfg.nodeId = "pc";
    m_cfg.host = host;
    m_cfg.port = static_cast<quint16>(port);
    m_cfg.user = user;
    m_cfg.password = pass;
    m_node = node;
    resetNodeInfo();

    m_mqtt = new MqttService(m_cfg, this);
    m_tracker = new CommandTracker(m_mqtt, m_cfg, this);

    connect(m_mqtt, &MqttService::stateChanged, this, &NodeController::onMqttState);
    connect(m_mqtt, &MqttService::messageReceived, this, &NodeController::onMessage);
    connect(m_mqtt, &MqttService::logMessage, this, &NodeController::addLog);
    connect(m_mqtt, &MqttService::publishFailed, this,
            [this](const QString &t, const QString &why) { addLog("Gửi thất bại " + t + ": " + why); });
    connect(m_tracker, &CommandTracker::finished, this, &NodeController::onCommandFinished);

    m_mqtt->subscribe(m_cfg.nodeTopic(m_node, "#"), 1);
    m_mqtt->start();
}

void NodeController::disconnectBroker()
{
    if (!m_mqtt) return;
    m_mqtt->stop();
    m_tracker->deleteLater();
    m_mqtt->deleteLater();
    m_tracker = nullptr;
    m_mqtt = nullptr;
    m_connState = "Disconnected";
    resetNodeInfo();
    emit connectionStateChanged();
}

void NodeController::ping()
{
    if (m_tracker) m_tracker->send(m_node, "ping");
}

void NodeController::setInterval(int ms)
{
    if (m_tracker) m_tracker->send(m_node, "set_interval", ms);
}

void NodeController::onMqttState(MqttService::State s)
{
    switch (s) {
    case MqttService::State::Disconnected:  m_connState = "Disconnected";  break;
    case MqttService::State::Connecting:    m_connState = "Connecting";    break;
    case MqttService::State::Connected:     m_connState = "Connected";     break;
    case MqttService::State::Reconnecting:  m_connState = "Reconnecting";  break;
    }
    if (s != MqttService::State::Connected)
        resetNodeInfo();                     // mất kết nối: không còn biết trạng thái node
    emit connectionStateChanged();
}

void NodeController::onMessage(const QString &topic, const QByteArray &payload)
{
    const QString prefix = m_cfg.nodeTopic(m_node, QString());       // "lab/<node>/"
    if (!topic.startsWith(prefix)) return;
    const QString suffix = topic.mid(prefix.size());

    if (suffix == "status") {
        m_statusPayload = QString::fromUtf8(payload);
        addLog("Trạng thái " + m_node + ": " + m_statusPayload);
        evaluateNodeState();
    } else if (suffix == "heartbeat") {
        m_hasHeartbeat = true;
        m_sinceHeartbeat.restart();
        evaluateNodeState();
    } else if (suffix == "sensor/cpu_temp") {
        QJsonObject o;
        if (!Protocol::parseObject(payload, o)) return;
        m_tempValid = o.value("valid").toBool(false);
        m_cpuTemp = m_tempValid ? QString::number(o.value("value").toDouble(), 'f', 1) : "--";
        m_hasTelemetry = true;
        m_sinceTelemetry.restart();
        emit telemetryChanged();
    } else if (m_tracker && m_tracker->handleMessage(topic, payload)) {
        // ACK đã được CommandTracker xử lý
    } else {
        addLog(topic + " → " + QString::fromUtf8(payload));
    }
}

void NodeController::onCommandFinished(const QString &id, const QString &command,
                                       const QString &status, const QString &detail, qint64 rttMs)
{
    QString text = command + " → " + status;
    if (!detail.isEmpty()) text += " (" + detail + ")";
    if (status == "success" && rttMs >= 0) text += QString(", %1 ms").arg(rttMs);
    m_lastResult = text;
    emit lastResultChanged();
    addLog(text + "  [" + id + "]");
}

void NodeController::onTick()
{
    evaluateNodeState();
    if (m_hasTelemetry) emit telemetryChanged();      // cập nhật tuổi dữ liệu
}

void NodeController::resetNodeInfo()
{
    m_statusPayload.clear();
    m_hasHeartbeat = false;
    m_hasTelemetry = false;
    m_cpuTemp = "--";
    m_tempValid = false;
    evaluateNodeState();
    emit telemetryChanged();
}

void NodeController::evaluateNodeState()
{
    QString s = "unknown";
    if (m_connState == "Connected") {
        const bool fresh = m_hasHeartbeat && m_sinceHeartbeat.elapsed() <= kStaleMs;
        if (m_statusPayload == "offline")      s = "offline";
        else if (m_statusPayload == "online")  s = fresh ? "online" : "stale";
    }
    if (s != m_nodeState) {
        m_nodeState = s;
        emit nodeStateChanged();
    }
}

void NodeController::addLog(const QString &s)
{
    m_log.prepend(QTime::currentTime().toString("hh:mm:ss") + "  " + s);
    if (m_log.size() > 200) m_log.removeLast();
    emit logChanged();
}
```

### 8.3. `pc_gui/main.cpp`

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "NodeController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    NodeController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("node", &controller);
    engine.loadFromModule("MqttGui", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
```

### 8.4. `pc_gui/Main.qml`

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 760; height: 620
    title: "MQTT PC ↔ Pi"

    readonly property var stateColors: ({ online: "#2e9e44", stale: "#e08a00",
                                          offline: "#c62828", unknown: "#757575" })
    readonly property var stateTexts: ({ online: "Online",
                                         stale: "Không còn nhận heartbeat (stale)",
                                         offline: "Offline", unknown: "Chưa rõ" })

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // --- Kết nối ---
        RowLayout {
            spacing: 6
            TextField { id: host; text: "192.168.1.10"; placeholderText: "IP broker"; Layout.preferredWidth: 130 }
            SpinBox { id: port; from: 1; to: 65535; value: 1883; editable: true }
            TextField { id: user; text: "pc_user"; placeholderText: "User"; Layout.preferredWidth: 90 }
            TextField { id: pass; echoMode: TextInput.Password; placeholderText: "Mật khẩu"; Layout.preferredWidth: 100 }
            TextField { id: nodeId; text: "pi"; placeholderText: "Node ID"; Layout.preferredWidth: 70 }
            Button {
                text: node.connectionState === "Disconnected" ? "Kết nối" : "Ngắt"
                onClicked: node.connectionState === "Disconnected"
                           ? node.connectBroker(host.text, port.value, user.text, pass.text, nodeId.text)
                           : node.disconnectBroker()
            }
        }

        // --- Trạng thái ---
        RowLayout {
            spacing: 18
            Label { text: "Broker: " + node.connectionState }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: stateColors[node.nodeState]
            }
            Label { text: "Node: " + stateTexts[node.nodeState]; color: stateColors[node.nodeState] }
        }

        // --- Dữ liệu ---
        RowLayout {
            Label { text: "Nhiệt độ CPU: " + node.cpuTemp + " °C"; font.pixelSize: 24 }
            Label {
                visible: node.dataAgeSec >= 0
                text: node.dataAgeSec > 10 ? "  (dữ liệu cũ " + node.dataAgeSec + " s)"
                                            : "  (cách đây " + node.dataAgeSec + " s)"
                color: node.dataAgeSec > 10 ? "#e08a00" : "gray"
            }
            Label { visible: node.dataAgeSec >= 0 && !node.tempValid; text: "  cảm biến không hợp lệ"; color: "#c62828" }
        }

        // --- Điều khiển ---
        RowLayout {
            Button { text: "Ping"; enabled: node.connected; onClicked: node.ping() }
            Label { text: "Chu kỳ (ms):" }
            SpinBox { id: interval; from: 200; to: 60000; stepSize: 100; value: 2000; editable: true }
            Button { text: "Áp dụng"; enabled: node.connected; onClicked: node.setInterval(interval.value) }
        }
        Label { text: node.lastResult === "" ? "Chưa gửi lệnh nào" : "Lệnh gần nhất: " + node.lastResult }

        // --- Log ---
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: node.log
            delegate: Text {
                text: modelData
                font.family: "monospace"
                width: ListView.view.width
                wrapMode: Text.Wrap
            }
        }
    }
}
```

Build trên PC:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_PI_NODE=OFF \
      -DCMAKE_PREFIX_PATH=~/Qt/6.7.2/gcc_64        # đổi cho đúng bản Qt và hệ điều hành
cmake --build build
./build/pc_gui
```

(Dùng Qt Creator: mở `CMakeLists.txt`, chọn kit Qt ≥ 6.5, đặt `BUILD_PI_NODE=OFF`.)

> Mật khẩu GUI chỉ nằm trong bộ nhớ khi chạy, không được lưu ra file.

---

## 9. Chạy `pi_node` tự động bằng systemd

```bash
sudo mkdir -p /etc/mqtt_qt
echo -n '<mk_pi>' | sudo tee /etc/mqtt_qt/pi_node.pass > /dev/null
sudo chown <user_linux>:<user_linux> /etc/mqtt_qt/pi_node.pass
sudo chmod 600 /etc/mqtt_qt/pi_node.pass
```

`/etc/systemd/system/pi-node.service` (thay `<user_linux>` và đường dẫn cho đúng máy của bạn; Orange Pi thường không dùng user `pi`):

```ini
[Unit]
Description=Qt MQTT Pi Node
After=network-online.target mosquitto.service
Wants=network-online.target

[Service]
User=<user_linux>
ExecStart=/home/<user_linux>/mqtt_qt/build/pi_node --host localhost --user pi_user --pass-file /etc/mqtt_qt/pi_node.pass --id pi
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now pi-node
journalctl -u pi-node -f
```

---

## 10. Kịch bản kiểm thử

Cột **Kết quả** để trống; chỉ điền sau khi chạy thật, kèm thời gian đo được.

Chuẩn bị: một terminal chạy `mosquitto_sub -h <IP_Pi> -u pc_user -P '<mk_pc>' -t 'lab/#' -v` để quan sát; GUI đã kết nối, Node ID = `pi`.

| ID | Thử nghiệm | Cách làm | Mong đợi | Kết quả |
|---|---|---|---|---|
| T01 | Pub/sub cơ bản | Mục 5.4 | Nhận đúng topic và payload | |
| T02 | ACL | Dùng `pc_user` publish vào `lab/pi/sensor/x` | Không có tin nào xuất hiện ở subscriber | |
| T03 | Telemetry | Quan sát GUI | Nhiệt độ cập nhật ~2 s, "cách đây" ≤ 3 s | |
| T04 | Ping | Bấm Ping | `ping → success, N ms` (ghi N) | |
| T05 | Đổi chu kỳ | Áp dụng 500 ms | `success (500)`; telemetry nhanh lên ~0,5 s | |
| T06 | Giá trị sai | Dùng `mosquitto_pub` gửi `set_interval` với `value` 50 | ACK `error`, chu kỳ không đổi | |
| T07 | Lệnh trùng | Gửi hai lần cùng `request_id` (`manual-9`) bằng `mosquitto_pub` | Pi log "Lệnh trùng", ACK giống nhau, chu kỳ chỉ áp dụng một lần | |
| T08 | Tắt đột ngột | `kill -9 <pid pi_node>` | `lab/pi/status offline` do LWT; ghi thời gian từ lúc kill đến khi GUI báo Offline (kỳ vọng cỡ 1,5 × keepalive ≈ 22 s) | |
| T09 | Tắt êm | `kill -TERM <pid>` (hoặc Ctrl+C) | Nhận `offline` ngay; nếu không kịp, GUI chuyển `stale` sau 15 s | |
| T10 | Restart broker | `sudo systemctl restart mosquitto` | Cả hai bên báo Reconnecting rồi Connected, tự subscribe lại, telemetry tiếp tục; ghi thời gian phục hồi | |
| T11 | Tiến trình treo | `kill -STOP <pid>` rồi `kill -CONT <pid>` | `status` vẫn `online` nhưng GUI chuyển **stale** sau ~15 s; hết stale sau khi CONT | |
| T12 | Lệnh khi Pi offline | Tắt `pi_node`, bấm Ping | Kết quả `timeout` sau ~6 s (2 lần × 3 s) | |
| T13 | PC mất broker | Chặn mạng PC hoặc ngắt broker, bấm Ping | `not_sent` ngay, không treo giao diện | |
| T14 | Khởi động lại Pi | `sudo reboot` | `pi_node` tự chạy; GUI về `online` | |

Ghi chú khi ghi kết quả: thời điểm, phiên bản Qt/Mosquitto, kiểu mạng (LAN dây hay Wi-Fi), số lần lặp, giá trị đo được.

---

## 11. Những điều tài liệu KHÔNG cam kết

- **Tự kết nối lại:** code có backoff và subscribe lại, nhưng mức độ phục hồi trong mọi kịch bản mất mạng/broker chỉ khẳng định được sau khi T08–T14 chạy thật.
- **Online/offline:** LWT chỉ kích hoạt sau khi broker phát hiện mất kết nối (cỡ 1,5 × keepalive), không phải tức thời; ngắt êm không kích hoạt LWT nên `pi_node` tự gửi `offline`, và tin này có thể chưa kịp tới nơi. Heartbeat/`stale` là lớp bảo vệ bổ sung chứ không thay thế hoàn toàn.
- **QoS 1:** đảm bảo ở tầng MQTT là "ít nhất một lần"; việc lệnh đã thực hiện chỉ biết qua ACK. Cơ chế chống lệnh trùng chỉ nhớ 64 `request_id` gần nhất và mất khi `pi_node` khởi động lại.
- **Giá trị `publish()` trả `true`:** chỉ là client đã nhận để gửi, không phải bằng chứng broker hay đích đã nhận.
- **Bảo mật:** có xác thực và ACL, nhưng đường truyền chưa mã hóa (không TLS). Chỉ phù hợp mạng LAN tin cậy.
- **Đồng hồ:** `ts_ms` giữa hai máy có thể lệch; chỉ RTT đo ở PC là đáng tin mà không cần đồng bộ giờ.

---

## 12. Mở rộng (làm sau, khi bản này đã qua kiểm thử)

1. **Pi thứ hai:** chạy `pi_node --id pi2` (đã hỗ trợ, và ACL cần thêm khối cho user/topic của node mới). Để GUI hiển thị đồng thời nhiều node cần thêm `NodeManager` + model danh sách node (mỗi node có trạng thái, `lastSeen`, dữ liệu riêng) thay vì các biến đơn lẻ trong `NodeController`; v2 chưa làm phần này. Giả lập khi chưa có thiết bị thật: chạy thêm một tiến trình `pi_node --id pi2` trên Pi hoặc PC.
2. **STM32F407:** nối UART với Pi, thêm `SerialBridge` (`QSerialPort`) vào `core`, chuyển tiếp dòng `topic|value` ↔ MQTT dưới `lab/stm32/...`. Có thể viết chi tiết khi tới giai đoạn này.
3. **TLS:** `QMqttClient::connectToHostEncrypted` và listener 8883 trên Mosquitto, khi mạng không còn tin cậy.
4. **Tự động hóa kiểm thử:** script đo thời gian phục hồi thay vì bấm tay.

---

## 13. Xử lý sự cố

| Triệu chứng | Nguyên nhân thường gặp | Cách xử lý |
|---|---|---|
| CMake không tìm thấy `Qt6Mqtt` | Chưa cài/build Qt MQTT, hoặc cài sai prefix | Mục 4.2; chạy `find ... Qt6MqttConfig.cmake`, thêm `-DCMAKE_PREFIX_PATH` |
| Build QML lỗi `loadFromModule` | Qt < 6.5 | Cài Qt ≥ 6.5 trên PC |
| `Not authorized` / kết nối bị từ chối | Sai user/mật khẩu, hoặc `-c` làm mất user cũ | Tạo lại user (mục 5.2), restart mosquitto |
| GUI log "Broker từ chối subscribe" | ACL chưa cho đọc topic đó | Kiểm tra `/etc/mosquitto/acl` |
| Lệnh luôn `timeout` | `pi_node` không chạy, hoặc ACL chặn `cmd/#` hoặc `reply/#` | Xem log `pi_node`, kiểm tra ACL hai chiều |
| GUI luôn `stale` | Không nhận heartbeat (ACL chưa cho ghi `heartbeat` hoặc đọc `lab/pi/#`) | Kiểm tra ACL và `mosquitto_sub -t 'lab/pi/#' -v` |
| Hai chương trình đá nhau khỏi broker | Trùng `clientId` (v2 đã tránh bằng hậu tố ngẫu nhiên) | Kiểm tra không có bản cũ chạy song song |
| Nhiệt độ `--`, "cảm biến không hợp lệ" | Board không có `/sys/class/thermal/thermal_zone*` | `ls /sys/class/thermal`, xem trường `error` trong payload |
| Không truy cập được broker từ PC | Firewall, broker chỉ nghe localhost, sai IP | Kiểm tra `listener 1883`, `ss -tlnp \| grep 1883`, `ufw status` |
