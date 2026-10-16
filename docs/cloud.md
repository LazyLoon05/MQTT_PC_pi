# Phát triển trong Codex cloud

Dùng checkout hiện có `/workspace/MQTT_PC_pi`; mỗi cloud task đã cách ly, không cần tạo Git worktree.

Thiết bị đích: Windows 10 / Qt 5.14.2 và Orange Pi OS 1.0.8 Focal / Qt 5.12.8. Cloud dùng Qt 6 để phát triển cùng mã nguồn; kiểm tra Qt 5/Focal bằng container riêng.

Máy cloud Debian 13 không có quyền root. Bộ công cụ được tải bằng APT với kiểm tra chữ ký và checksum, giải nén dưới `/workspace/.mqtt-dev`, kết hợp thư viện sẵn có trong image. Qt MQTT lấy từ repo chính thức `qt/qtmqtt` tại tag khớp Qt. Không có tài khoản hay mật khẩu thiết bị trong cấu hình cloud.

Lần đầu hoặc khi cần làm mới:

```bash
cd /workspace/MQTT_PC_pi
./scripts/setup-cloud.sh
```

Mỗi shell/task mới:

```bash
source /workspace/.mqtt-dev/activate.sh
cd /workspace/MQTT_PC_pi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMQTT_QT_MAJOR=6
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
```

Không giữ broker nền giữa các task: suite integration tự tạo broker có xác thực/ACL và chạy node, gửi lệnh thật, rồi dọn tiến trình. `gui_smoke` dùng Qt offscreen vì cloud không có desktop. Dịch vụ production trên Orange Pi chạy qua systemd theo README, không chạy chung với cloud.

Mạng cần các preset package managers và GitHub: `deb.debian.org`, `security.debian.org`, `github.com`. Cấu hình cloud được lưu thành bản nháp; việc publish snapshot do người dùng thực hiện trong cài đặt môi trường. Máy mới sau publish vẫn cần xác minh lại, không suy luận từ tiến trình của task trước.

Qt 5 mặc định của dự án không được tự suy ra từ Qt có trên cloud. Luôn thêm `-DMQTT_QT_MAJOR=6` với bộ công cụ cloud này. Để kiểm thử cùng nền Ubuntu 20.04 x86_64: `./scripts/test-focal-container.sh` (cần Docker). Container không mô phỏng kernel Rockchip hoặc phần cứng ARM64, và trạng thái container không được giả định còn ở task sau.
