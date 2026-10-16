# Phát triển trong Codex cloud

Dùng checkout hiện có `/workspace/MQTT_PC_pi`; mỗi cloud task đã cách ly, không cần tạo Git worktree.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
```

Không giữ broker nền giữa các task: suite integration tự tạo broker có xác thực/ACL và chạy node, gửi lệnh thật, rồi dọn tiến trình. `gui_smoke` dùng Qt offscreen vì cloud không có desktop. Dịch vụ production trên Orange Pi chạy qua systemd theo README, không chạy chung với cloud.

Mạng cần các preset package managers và GitHub: `deb.debian.org`, `security.debian.org`, `github.com`. Cấu hình cloud được lưu thành bản nháp; việc publish snapshot do người dùng thực hiện trong cài đặt môi trường. Máy mới sau publish vẫn cần xác minh lại, không suy luận từ tiến trình của task trước.
