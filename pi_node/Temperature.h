#pragma once
#include <QString>
struct TempReading {
    bool valid = false;
    double celsius = 0;
    QString error;
};
TempReading readCpuTemp(const QString &thermalRoot = "/sys/class/thermal");
