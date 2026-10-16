# Kết quả kiểm thử triển khai

Ngày: **2026-10-16**. Máy kiểm tra: Codex cloud Debian 13, x86_64, GCC 14.2.0, CMake 3.31.6, Ninja 1.12.1, Qt/Qt MQTT 6.8.2, Mosquitto 2.0.21. Kết nối loopback TCP; không có bo Orange Pi 3B hoặc PC Windows được nối vào task này.

## Đã chạy

| Kiểm tra | Kết quả |
|---|---|
| Build `pi_node` và Qt Quick `pc_gui` (RelWithDebInfo) | PASS |
| Build riêng `pi_node`, `BUILD_PC_GUI=OFF`, `BUILD_TESTING=OFF` (Release) | PASS |
| Qt Test unit | 24 trường hợp/data row; 26 PASS kể cả init/cleanup; 0 fail/skip |
| Qt Test integration với Mosquitto thật, xác thực và ACL production | 12 trường hợp; 14 PASS kể cả init/cleanup; 0 fail/skip |
| Qt Quick offscreen `pc_gui --smoke-test` | PASS; engine tải được Main, tiến trình thoát 0 |
| CTest tổng | **3/3 suite PASS**, khoảng 48,28 giây |
| ASan + UBSan: telemetry/cleanup và restart broker | 2 trường hợp PASS (4 kể cả init/cleanup); không ghi nhận lỗi sanitizer trong hai ca này |
| Shell scripts | `bash -n` từng file PASS; không thay thế thử cài trên thiết bị |
| systemd unit | `systemd-analyze verify` PASS với đường dẫn ExecStart thay tạm sang binary build cloud; không bật systemd thật |

Unit tests kiểm tra ID, JSON, khoảng/kiểu interval (kể cả số rất lớn), dedupe/giới hạn cache, dữ liệu thermal giả lập và heartbeat.

Integration tests chạy Pi node như tiến trình riêng và NodeController của GUI trong Qt event loop. Chúng xác minh telemetry, heartbeat, ping + ACK (một lần đo RTT **20 ms** trên loopback), áp dụng interval 500 ms, từ chối interval sai, ACK lệnh trùng giống từng byte, ACL chặn PC ghi sensor, shutdown êm, LWT khi kill, retry cùng payload rồi timeout, not_sent khi mất broker, reconnect + resubscribe, stale khi SIGSTOP, bỏ ACK sai/lặp và bỏ dữ liệu không hợp lệ. Kết quả RTT chỉ là số đo của lần chạy này, không phải cam kết hiệu năng LAN/Wi-Fi.

## Đối chiếu 14 kịch bản trong tài liệu

| ID | Phạm vi đã xác minh trên cloud | Còn cần thiết bị thật |
|---|---|---|
| T01 | Pub/sub có tài khoản qua broker TCP | Liên lạc PC ↔ Pi qua LAN |
| T02 | ACL chặn `pc_user` ghi sensor | Kiểm tra cấu hình đã cài trên Pi |
| T03 | Payload nhiệt độ và tuổi dữ liệu trong controller | Nhiệt độ CPU RK3566 thật, hiển thị PC |
| T04 | Ping ACK/RTT | Đo RTT LAN/Wi-Fi |
| T05 | ACK 500 ms và nhịp telemetry thực tế | Kiểm tra trên board |
| T06 | Interval 50 được ACK error | Kiểm tra qua GUI/CLI trên LAN |
| T07 | Duplicate trả ACK cũ, handler không áp dụng lại | Kiểm tra log trên Pi |
| T08 | Kill node → broker phát offline/LWT | Mất điện/mất mạng thật; đo thời gian |
| T09 | SIGTERM → offline, exit 0 | systemd stop trên Pi |
| T10 | Dừng/start broker → reconnect + subscribe + ping thành công | Restart dịch vụ Mosquitto trên Pi |
| T11 | SIGSTOP → stale; SIGCONT → online | Đo độ trễ mạng thật |
| T12 | Hai lần gửi cùng payload, timeout ≥ 5,9 s | Thử Pi offline trên LAN |
| T13 | Mất broker → unknown; gửi trả not_sent | Firewall/rút mạng PC thật |
| T14 | **CHƯA CHẠY** | Reboot Orange Pi, xác nhận systemd tự chạy |

Chưa xác minh: build/chạy native ARM64, build Windows, cài broker/service thật, reboot bo, tắt nguồn, TLS, nhiều node cùng hiển thị. CI đã có workflow; trạng thái GitHub Actions cần xem ở run thực tế sau khi push.

Tài liệu `design-v2.md` giữ nguyên bản đầu vào của người dùng, gồm các snippet chưa xác minh trong bản gốc. Mã trong repo và README là hướng dẫn triển khai hiện tại: có thêm kiểm tra port/ACK/dữ liệu, ưu tiên thermal CPU/SoC, xử lý shutdown MQTT và script triển khai.
