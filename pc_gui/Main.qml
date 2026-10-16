import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

ApplicationWindow {
    id: root
    visible: true
    width: 920; height: 620
    minimumWidth: 680; minimumHeight: 520
    title: "MQTT PC ↔ Orange Pi 3B"

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
        GridLayout {
            columns: root.width >= 880 ? 6 : 3
            rowSpacing: 6
            columnSpacing: 6
            TextField { id: host; text: "localhost"; placeholderText: "IP broker trên Orange Pi"; Layout.fillWidth: true }
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
