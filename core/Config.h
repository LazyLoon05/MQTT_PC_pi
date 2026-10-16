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
        static const QRegularExpression re("\\A[a-z0-9_-]{1,32}\\z");
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
