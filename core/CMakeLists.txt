add_library(core STATIC
    Config.h Protocol.h
    MqttService.h MqttService.cpp
    CommandTracker.h CommandTracker.cpp)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC ${QT_PACKAGE}::Core ${QT_PACKAGE}::Mqtt)
