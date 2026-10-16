cmake_minimum_required(VERSION 3.21)
project(mqtt_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)
# Qt MQTT có thể được cài ở prefix riêng so với Qt hệ thống.
find_package(Qt6Mqtt REQUIRED)
add_subdirectory(core)

# Pi:  cmake -S . -B build -G Ninja -DBUILD_PC_GUI=OFF
# PC:  cmake -S . -B build -G Ninja -DBUILD_PI_NODE=OFF
option(BUILD_PI_NODE "Build pi_node" ON)
option(BUILD_PC_GUI  "Build pc_gui"  ON)

if(BUILD_PI_NODE)
    add_library(pi_support STATIC
        pi_node/CommandHandler.cpp pi_node/CommandHandler.h
        pi_node/Temperature.cpp pi_node/Temperature.h)
    target_include_directories(pi_support PUBLIC pi_node)
    target_link_libraries(pi_support PUBLIC core)
    add_executable(pi_node pi_node/main.cpp)
    target_link_libraries(pi_node PRIVATE pi_support)
    install(TARGETS pi_node RUNTIME DESTINATION bin)
endif()

if(BUILD_PC_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
    qt_add_executable(pc_gui
        pc_gui/main.cpp pc_gui/NodeController.cpp pc_gui/NodeController.h)
    qt_add_qml_module(pc_gui URI MqttGui VERSION 1.0 QML_FILES pc_gui/Main.qml)
    target_link_libraries(pc_gui PRIVATE core Qt6::Quick)
    install(TARGETS pc_gui RUNTIME DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
