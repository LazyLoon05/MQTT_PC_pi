if(BUILD_PI_NODE)
    add_executable(unit_tests unit_tests.cpp)
    target_link_libraries(unit_tests PRIVATE pi_support ${QT_PACKAGE}::Test)
    add_test(NAME unit COMMAND unit_tests)

    if(UNIX)
    add_executable(integration_tests integration_tests.cpp
        ../pc_gui/NodeController.cpp ../pc_gui/NodeController.h)
    target_include_directories(integration_tests PRIVATE ../pc_gui)
    target_link_libraries(integration_tests PRIVATE pi_support ${QT_PACKAGE}::Test ${QT_PACKAGE}::Network)
    target_compile_definitions(integration_tests PRIVATE
        PI_NODE_PATH="$<TARGET_FILE:pi_node>"
        ACL_PATH="${PROJECT_SOURCE_DIR}/deploy/mosquitto/mqtt-pc-pi.acl")
    add_dependencies(integration_tests pi_node)
    add_test(NAME mqtt_integration COMMAND integration_tests)
    set_tests_properties(mqtt_integration PROPERTIES TIMEOUT 150)
    endif()
endif()
if(BUILD_PC_GUI)
    add_test(NAME gui_smoke COMMAND pc_gui --smoke-test)
    set_tests_properties(gui_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QT_QUICK_BACKEND=software"
        TIMEOUT 15)
endif()
