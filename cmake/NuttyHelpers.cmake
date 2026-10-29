# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# Platform-specific sources use a filename suffix instead of per-file lists:
#   Foo_linux.cpp   Linux only
#   Foo_win.cpp     Windows only
#   Foo_mac.cpp/mm  macOS only
#   Foo_unix.cpp    Linux + macOS
# nutty_filter_platform_sources(<list-var>) drops whatever doesn't apply here.
function(nutty_filter_platform_sources var)
    set(result)
    foreach(file IN LISTS ${var})
        get_filename_component(stem "${file}" NAME_WE)
        get_filename_component(ext "${file}" EXT)
        if(stem MATCHES "_linux$" AND NOT NUTTY_LINUX)
            continue()
        elseif(stem MATCHES "_win$" AND NOT WIN32)
            continue()
        elseif(stem MATCHES "_mac$" AND NOT APPLE)
            continue()
        elseif(stem MATCHES "_unix$" AND WIN32)
            continue()
        elseif(ext STREQUAL ".mm" AND NOT APPLE)
            continue()
        endif()
        list(APPEND result "${file}")
    endforeach()
    set(${var} "${result}" PARENT_SCOPE)
endfunction()

# nutty_add_library(<name> DIRS <dir>... [LIBS <lib>...])
#
# A static library made of every .h/.cpp/.mm under DIRS (recursively), filtered
# for the current platform. Includes are rooted at src/, so code includes
# e.g. "core/model/DeviceStatus.h".
function(nutty_add_library name)
    cmake_parse_arguments(ARG "" "" "DIRS;LIBS" ${ARGN})
    set(files)
    foreach(dir IN LISTS ARG_DIRS)
        file(GLOB_RECURSE found CONFIGURE_DEPENDS
            "${dir}/*.h" "${dir}/*.cpp" "${dir}/*.mm")
        list(APPEND files ${found})
    endforeach()
    nutty_filter_platform_sources(files)
    # A library whose directory is still empty (or holds no code for this OS)
    # still has to exist for its dependents to link.
    if(NOT files MATCHES "\\.(cpp|mm)(;|$)")
        set(stub "${CMAKE_CURRENT_BINARY_DIR}/${name}_stub.cpp")
        file(WRITE "${stub}" "// Generated: ${name} has no sources on this platform.\n")
        list(APPEND files "${stub}")
    endif()
    add_library(${name} STATIC ${files})
    target_include_directories(${name} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${name} PUBLIC ${ARG_LIBS})
    nutty_set_warnings(${name})
endfunction()
