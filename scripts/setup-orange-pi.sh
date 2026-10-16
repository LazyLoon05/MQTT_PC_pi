#!/usr/bin/env bash
# Chạy trên Orange Pi sau khi build thành công pi_node. Không ghi mật khẩu vào Git.
set -euo pipefail
if (( EUID != 0 )); then
    echo "Chạy: sudo $0 /đường/dẫn/build-pi/pi_node" >&2
    exit 1
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${1:-"$root/build-pi/pi_node"}
[[ -x $binary ]] || { echo "Không thấy binary pi_node: $binary" >&2; exit 1; }
command -v mosquitto_passwd >/dev/null
command -v systemctl >/dev/null
# Không tự ghi đè cấu hình hoặc mật khẩu đang dùng.
for dest in /etc/mosquitto/conf.d/mqtt-pc-pi.conf /etc/mosquitto/mqtt-pc-pi.acl \
    /etc/mosquitto/mqtt-pc-pi.passwd /etc/mqtt_qt/pi_node.pass /etc/systemd/system/pi-node.service; do
    [[ ! -e $dest ]] || { echo "Đã có $dest; xem README để cập nhật thủ công." >&2; exit 1; }
done
getent passwd mqtt-node >/dev/null || useradd --system --no-create-home --shell /usr/sbin/nologin mqtt-node
install -d -m 750 -o root -g mqtt-node /etc/mqtt_qt
install -m 755 "$binary" /usr/local/bin/pi_node
# Nếu Qt MQTT dùng prefix riêng, lưu đường dẫn thư viện để systemd cũng tìm thấy.
if ldd /usr/local/bin/pi_node | grep -q 'not found'; then
    echo 'Thiếu thư viện động. Đăng ký prefix Qt MQTT bằng ldconfig trước (README).' >&2
    exit 1
fi
read -r -s -p 'Mật khẩu mới cho pi_user: ' pi_password; printf '\n'
read -r -s -p 'Mật khẩu mới cho pc_user: ' pc_password; printf '\n'
[[ -n $pi_password && -n $pc_password ]] || { echo 'Mật khẩu không được rỗng.' >&2; exit 1; }
umask 077
# mosquitto_passwd -U chuyển file cleartext tạm thành hash, không đưa pass vào argv.
printf 'pi_user:%s\npc_user:%s\n' "$pi_password" "$pc_password" > /etc/mosquitto/mqtt-pc-pi.passwd
mosquitto_passwd -U /etc/mosquitto/mqtt-pc-pi.passwd
printf '%s\n' "$pi_password" > /etc/mqtt_qt/pi_node.pass
unset pi_password pc_password
chown root:mosquitto /etc/mosquitto/mqtt-pc-pi.passwd
chmod 640 /etc/mosquitto/mqtt-pc-pi.passwd
chown root:mqtt-node /etc/mqtt_qt/pi_node.pass
chmod 640 /etc/mqtt_qt/pi_node.pass
install -m 640 -o root -g mosquitto "$root/deploy/mosquitto/mqtt-pc-pi.acl" /etc/mosquitto/mqtt-pc-pi.acl
install -m 644 "$root/deploy/mosquitto/mqtt-pc-pi.conf" /etc/mosquitto/conf.d/mqtt-pc-pi.conf
install -m 644 "$root/deploy/systemd/pi-node.service" /etc/systemd/system/pi-node.service
systemctl daemon-reload
systemctl enable mosquitto pi-node
systemctl restart mosquitto
systemctl restart pi-node
systemctl --no-pager --full status mosquitto pi-node
