# Kết quả kiểm thử

Ngày: **2026-10-16**. Thiết bị đích theo thông tin người dùng: **Windows 10 / Qt 5.14.2** và **Orange Pi 3B, Orange Pi OS 1.0.8 Focal (Ubuntu 20.04), kernel Linux 5.10.160-rockchup-rk356x**.

Dự án đã đổi sang Qt 5 mặc định, API/QML tối thiểu Qt 5.12, CMake tối thiểu 3.16. PC giữ Qt 5.14.2; Pi có thể dùng Qt 5.12.8 từ apt. Giao thức MQTT/JSON giống nhau giữa hai phiên bản. Qt 6 là lựa chọn build riêng.

## Môi trường đã chạy

| Môi trường | Công cụ | Phạm vi |
|---|---|---|
| Ubuntu 20.04 container, x86_64 | GCC 9.4.0, CMake 3.16.3, Ninja 1.10.0, Qt/Qt MQTT **5.12.8**, Mosquitto **1.6.9** | Build cả hai app, unit, MQTT integration và GUI offscreen; build riêng headless |
| Debian 13 cloud, x86_64 | GCC 14.2.0, CMake 3.31.6, Qt/Qt MQTT **6.8.2**, Mosquitto **2.0.21** | Build cả hai app với `MQTT_QT_MAJOR=6`, cùng các suite; build riêng headless |

Qt MQTT 5.12.8 được clone từ tag chính thức `v5.12.8`, build bằng qmake + make và cài thành công bằng `scripts/install-deps-debian.sh --gui` trong Focal. Không tắt kiểm tra APT/chữ ký/checksum/TLS. Trong cloud có proxy, CA công khai được thêm vào trust store của container.

## Kết quả

| Kiểm tra | Qt 5 / Focal | Qt 6 / Debian |
|---|---|---|
| Build `pi_node`, `pc_gui` | PASS | PASS |
| Build `BUILD_PC_GUI=OFF`, `BUILD_TESTING=OFF` | PASS | PASS |
| Unit (24 trường hợp/data row; 26 PASS kể cả init/cleanup) | PASS, không skip | PASS, không skip |
| MQTT integration (12 trường hợp; 14 PASS kể cả init/cleanup) | PASS, không skip | PASS, không skip |
| GUI `pc_gui --smoke-test`, Qt offscreen/software | PASS | PASS |
| CTest tổng | **3/3 suite PASS**, 82,93 s | **3/3 suite PASS**, 48,82 s |

Unit kiểm tra node ID, JSON, kiểu/khoảng interval (gồm số rất lớn), dedupe/giới hạn cache, thermal zone giả lập và heartbeat. Timestamp/sequence JSON dùng chuyển đổi số rõ ràng để biên dịch được với QJsonValue của cả Qt 5/6.

Integration chạy node như tiến trình riêng và NodeController của PC trong Qt event loop, qua broker TCP thật có xác thực/ACL từ repo. Đã kiểm tra telemetry/heartbeat, ping ACK, interval 500 ms và lỗi 50 ms, duplicate ACK giống từng byte, ACL chặn PC ghi sensor, shutdown êm/LWT, retry cùng payload rồi timeout, not_sent khi mất broker, reconnect + subscribe lại + ping, stale khi SIGSTOP, bỏ ACK sai/lặp, dữ liệu lỗi và CLI port sai.

Mosquitto 1.6.9 trên Focal trong container mất khoảng **10,02 giây** để thoát sau SIGTERM kể cả không có client (đo độc lập, exit 0). Fixture restart đợi tối đa 15 giây để broker dừng và vẫn xử lý event loop PC; sau đó vẫn bắt buộc xác minh kết nối lại, online và ACK thành công. Thời gian chờ ACK lệnh trong ứng dụng vẫn là 3 giây/lần, tối đa hai lần gửi.

Mọi shell script đã qua `bash -n`. Script cài Qt MQTT Windows được chuẩn bị cho đúng kit MinGW/MSVC 5.14.2, build Debug/Release; **chưa chạy PowerShell/compiler trên Windows thật**. Script triển khai systemd chưa được chạy trên Orange Pi thật.

## Đối chiếu 14 kịch bản trong tài liệu gốc

| ID | Đã xác minh trong môi trường Linux x86_64 | Còn cần thiết bị thật |
|---|---|---|
| T01 | Pub/sub có xác thực qua broker TCP | PC ↔ Pi qua LAN |
| T02 | ACL chặn PC ghi sensor | ACL đã cài trên Pi |
| T03 | Payload/tuổi dữ liệu, xử lý sensor lỗi | Nhiệt độ RK3566 thật và hiển thị PC |
| T04 | Ping ACK/RTT | RTT LAN/Wi-Fi |
| T05 | ACK 500 ms và nhịp telemetry | Kiểm tra trên bo |
| T06 | Interval 50 → ACK error | Thử trên LAN |
| T07 | Duplicate → ACK cũ, handler không áp dụng lại | Log trên Pi |
| T08 | Kill node → offline/LWT | Mất điện/mất mạng; đo thời gian |
| T09 | SIGTERM → offline, exit 0 | systemd stop trên Pi |
| T10 | Stop/start broker → reconnect + subscribe + ping | Restart Mosquitto trên Pi |
| T11 | SIGSTOP → stale; SIGCONT → online | Đo trên mạng thật |
| T12 | Hai lần gửi cùng payload rồi timeout | Pi offline qua LAN |
| T13 | Mất broker → unknown và not_sent | Firewall/rút mạng PC |
| T14 | **CHƯA CHẠY** | Reboot Orange Pi, xác nhận tự khởi động |

Chưa xác minh build/chạy **Windows 10 / Qt 5.14.2** và **ARM64 / kernel Rockchip** trực tiếp. Container Focal kiểm tra nền userspace Ubuntu 20.04/Qt 5.12.8, không mô phỏng bo hoặc kernel Orange Pi. Chưa đo hiệu năng LAN/Wi-Fi, chưa có TLS hoặc nhiều node cùng hiển thị. CI có matrix Qt 5/Focal và Qt 6/Debian; trạng thái run trên GitHub cần xem thực tế sau push.

`design-v2.md` giữ nguyên tài liệu đầu vào (snippet Qt 6); mã trong repo và README là hướng dẫn hiện tại cho Qt 5/6 và thiết bị của người dùng.
