# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# Per-OS glue: power actions, sleep/wake, autostart, desktop notifications,
# secret storage, the OS power-source reader, window effects, single instance.
# Files are picked by suffix (_linux/_win/_mac/_unix), see NuttyHelpers.cmake.

set(NUTTY_PLATFORM_LIBS nutty_model nutty_storage Qt::Gui Qt::Widgets)
if(NUTTY_LINUX)
    list(APPEND NUTTY_PLATFORM_LIBS Qt::DBus)
endif()

nutty_add_library(nutty_platform
    DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    LIBS ${NUTTY_PLATFORM_LIBS})

if(WIN32)
    target_link_libraries(nutty_platform PUBLIC
        powrprof advapi32 user32 dwmapi shell32 ole32 credui)
    target_compile_definitions(nutty_platform PUBLIC
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
elseif(APPLE)
    target_link_libraries(nutty_platform PUBLIC
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework Security"
        "-framework AppKit"
        "-framework UserNotifications"
        "-framework ServiceManagement")
    target_compile_options(nutty_platform PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()
