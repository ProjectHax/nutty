# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

cmake_minimum_required(VERSION 3.21)

# The project version is the single source of truth: the release workflow, the
# macOS Info.plist, the Windows resource file and the installer all read it.
project(nutty
    VERSION 0.1.0
    DESCRIPTION "UPS and battery monitor for NUT, EcoFlow and more"
    HOMEPAGE_URL "https://github.com/ProjectHax/nutty"
    LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(UNIX AND NOT APPLE)
    set(NUTTY_LINUX ON)
endif()

set(NUTTY_APP_ID "io.github.projecthax.nutty")
set(NUTTY_APP_NAME "nutty")
set(NUTTY_ORGANIZATION "ProjectHax LLC")
set(NUTTY_COPYRIGHT "Copyright (C) 2026 ProjectHax LLC")

option(NUTTY_BUILD_TESTS "Build the unit tests" ON)
option(NUTTY_WERROR "Treat compiler warnings as errors" OFF)

# Qt 6.4 is the floor: it is what Ubuntu 24.04 ships, and the .deb/.rpm are
# built against it. Anything newer must sit behind a QT_VERSION_CHECK guard.
set(NUTTY_QT_COMPONENTS Core Gui Widgets Network Sql Qml Quick QuickControls2 Svg ShaderTools LinguistTools)
if(NUTTY_LINUX)
    list(APPEND NUTTY_QT_COMPONENTS DBus)
endif()
if(NUTTY_BUILD_TESTS)
    list(APPEND NUTTY_QT_COMPONENTS Test QuickTest)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${NUTTY_QT_COMPONENTS})

qt_standard_project_setup()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(NuttyHelpers)

add_subdirectory(src)

if(NUTTY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/cmake/Packaging.cmake")
    include(Packaging)
endif()
