QT = core network mqtt

CONFIG += c++17 cmdline

SOURCES += \
    main.cpp \
    mqttconsoleclient.cpp

HEADERS += \
    mqttconsoleclient.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
