#include "Temperature.h"
#include <QDir>
#include <QFile>
#include <cmath>

TempReading readCpuTemp(const QString &thermalRoot)
{
    const QDir root(thermalRoot);
    QStringList zones = root.entryList({"thermal_zone*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    // RK3566/Orange Pi: ưu tiên cpu-thermal/soc-thermal, không giả định zone0.
    QStringList preferred, other;
    for (const QString &zone : zones) {
        QFile type(root.filePath(zone + "/type"));
        QString name;
        if (type.open(QIODevice::ReadOnly)) name = QString::fromUtf8(type.readAll()).toLower();
        (name.contains("cpu") || name.contains("soc") ? preferred : other).append(zone);
    }
    preferred.append(other);
    bool outOfRange = false;
    for (const QString &zone : preferred) {
        QFile file(root.filePath(zone + "/temp"));
        if (!file.open(QIODevice::ReadOnly)) continue;
        bool ok = false;
        const double c = file.readAll().trimmed().toDouble(&ok) / 1000.0;
        if (!ok || !std::isfinite(c)) continue;
        if (c < -20 || c > 125) { outOfRange = true; continue; }
        return {true, c, {}};
    }
    return {false, 0, outOfRange ? "giá trị ngoài khoảng hợp lệ" : "không đọc được thermal zone"};
}
