cmake_minimum_required(VERSION 3.16)
project(mqtt_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(MQTT_QT_MAJOR "5" CACHE STRING "Qt major version (5 or 6)")
set_property(CACHE MQTT_QT_MAJOR PROPERTY STRINGS 5 6)
if(NOT MQTT_QT_MAJOR MATCHES "^[56]$")
    message(FATAL_ERROR "MQTT_QT_MAJOR must be 5 or 6")
endif()
set(QT_PACKAGE "Qt${MQTT_QT_MAJOR}")
if(MQTT_QT_MAJOR STREQUAL "5")
    find_package(Qt5 5.12 REQUIRED COMPONENTS Core)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
# Qt MQTT có thể được cài ở prefix riêng so với Qt hệ thống.
find_package(${QT_PACKAGE}Mqtt REQUIRED)
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
    find_package(${QT_PACKAGE} REQUIRED COMPONENTS Quick QuickControls2)
    set(CMAKE_AUTORCC ON)
    add_executable(pc_gui
        pc_gui/main.cpp pc_gui/NodeController.cpp pc_gui/NodeController.h
        pc_gui/pc_gui.qrc)
    target_link_libraries(pc_gui PRIVATE core ${QT_PACKAGE}::Quick ${QT_PACKAGE}::QuickControls2)
    install(TARGETS pc_gui RUNTIME DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(${QT_PACKAGE} REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
