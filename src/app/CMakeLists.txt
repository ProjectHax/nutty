# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# nutty_ui is the QML module "Nutty": every QML file, the C++ types exposed to
# QML (theme, formatting, image providers, view-models), the shaders and the
# artwork. It is a static library with a static QML plugin so that both the
# application and the QML tests (tests/qml) can load the same module.
qt_add_library(nutty_ui STATIC)

file(GLOB_RECURSE NUTTY_UI_CPP CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/viewmodels/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/viewmodels/*.cpp")
nutty_filter_platform_sources(NUTTY_UI_CPP)
target_sources(nutty_ui PRIVATE ${NUTTY_UI_CPP})
target_include_directories(nutty_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
# The generated QML type registrations include headers by file name only.
target_include_directories(nutty_ui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ui" "${CMAKE_CURRENT_SOURCE_DIR}/viewmodels")
target_link_libraries(nutty_ui PUBLIC
    nutty_model nutty_engine nutty_storage nutty_notify nutty_platform
    Qt::Gui Qt::Qml Qt::Quick Qt::QuickControls2 Qt::Svg Qt::Widgets Qt::Network)
nutty_set_warnings(nutty_ui)

file(GLOB_RECURSE NUTTY_UI_QML CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml" "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js")

# Qt < 6.5 doesn't add qrc:/qt/qml to the import path by default; main.cpp
# and the QML test runner add it (see ui/UiSetup.cpp).
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()
qt_add_qml_module(nutty_ui
    URI Nutty
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qml/Nutty"
    QML_FILES ${NUTTY_UI_QML})

# Fragment shaders, compiled to .qsb (SPIR-V + GLSL/HLSL/MSL) at build time.
# They only use features available in GLSL ES 1.00 so every backend works.
file(GLOB NUTTY_UI_SHADERS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert")
qt_add_shaders(nutty_ui "nutty_ui_shaders"
    PREFIX "/nutty/shaders"
    BASE "shaders"
    FILES ${NUTTY_UI_SHADERS})

# Artwork and UI icons, rendered through the image providers in ui/, and the
# app icon (window icon, About page).
file(GLOB NUTTY_UI_ASSETS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/resources/art/*.svg"
    "${PROJECT_SOURCE_DIR}/resources/icons/*.svg"
    "${PROJECT_SOURCE_DIR}/resources/appicon/nutty.svg")
qt_add_resources(nutty_ui "nutty_ui_assets"
    PREFIX "/nutty"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES ${NUTTY_UI_ASSETS})

# --- the application --------------------------------------------------------

# main.cpp plus the shell around the UI: startup, tray, windows, power actions.
file(GLOB NUTTY_SHELL_CPP CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shell/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/shell/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell/*.mm")
nutty_filter_platform_sources(NUTTY_SHELL_CPP)
qt_add_executable(nutty WIN32 MACOSX_BUNDLE main.cpp ${NUTTY_SHELL_CPP})
set_target_properties(nutty PROPERTIES OUTPUT_NAME nutty)
target_link_libraries(nutty PRIVATE nutty_ui nutty_uiplugin nutty_sources)
nutty_set_warnings(nutty)

# Translations: every i18n/nutty_<lang>.ts becomes :/i18n/nutty_<lang>.qm.
# scripts/translate.py creates them; until then there are none.
file(GLOB NUTTY_TS_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/i18n/*.ts")
if(NUTTY_TS_FILES)
    qt_add_lrelease(nutty
        TS_FILES ${NUTTY_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE NUTTY_QM_FILES)
    qt_add_resources(nutty "nutty_translations"
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${NUTTY_QM_FILES})
endif()
