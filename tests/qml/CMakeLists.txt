# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# Qt Quick Test runner for the Nutty QML module: every tst_*.qml here.
# Runs offscreen on the software renderer, so the non-shader fallbacks are
# what gets exercised; any QML warning fails the test.
add_executable(tst_qml tst_qml.cpp)
target_link_libraries(tst_qml PRIVATE Qt::QuickTest nutty_ui nutty_uiplugin)
target_include_directories(tst_qml PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
nutty_set_warnings(tst_qml)

file(GLOB NUTTY_QML_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.qml")
target_sources(tst_qml PRIVATE ${NUTTY_QML_TESTS})

add_test(NAME tst_qml COMMAND tst_qml -input "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(tst_qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_LOGGING_RULES=*.debug=false;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 180)
