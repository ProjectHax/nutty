# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# Every tests/<area>/tst_*.cpp becomes its own QtTest executable and ctest
# entry, linked against that area's library. Header-only helpers shared by
# several tests live in tests/common.
function(nutty_add_tests area)
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${area}/tst_*.cpp")
    nutty_filter_platform_sources(tests)
    foreach(src IN LISTS tests)
        get_filename_component(name "${src}" NAME_WE)
        add_executable(${name} "${src}")
        target_link_libraries(${name} PRIVATE Qt::Test ${ARGN})
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        nutty_set_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
        # Without a console, Windows sends Qt's messages to the debugger, so a
        # failing test would print nothing; QT_FORCE_STDERR_LOGGING keeps them.
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*.debug=false;QT_FORCE_STDERR_LOGGING=1"
            TIMEOUT 120)
    endforeach()
endfunction()

nutty_add_tests(model nutty_model)
nutty_add_tests(sources nutty_sources)
nutty_add_tests(storage nutty_storage)
nutty_add_tests(engine nutty_engine nutty_storage)
nutty_add_tests(notify nutty_notify)
nutty_add_tests(platform nutty_platform)
nutty_add_tests(app nutty_ui nutty_engine nutty_storage)
# The tray icon is drawn in the app's shell, which isn't a library.
target_sources(tst_trayicon PRIVATE "${PROJECT_SOURCE_DIR}/src/app/shell/TrayIconEngine.cpp")
# Several libraries working together, as the app wires them.
nutty_add_tests(integration nutty_engine nutty_storage nutty_notify)

# QML component tests (Qt Quick Test) bring their own CMakeLists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/CMakeLists.txt")
    add_subdirectory(qml)
endif()
