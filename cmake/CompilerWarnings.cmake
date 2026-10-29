# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# nutty_set_warnings(<target>)
#
# Applies the project's warning set. Qt's headers come in through imported
# targets, so they are treated as system headers and stay quiet.
function(nutty_set_warnings target)
    if(MSVC)
        # C4702 (unreachable code) comes from the optimizer, after inlining,
        # so /external:W0 can't keep it out of Qt's headers.
        target_compile_options(${target} PRIVATE /W4 /wd4702 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
        if(NUTTY_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wnon-virtual-dtor -Woverloaded-virtual -Wimplicit-fallthrough)
        if(NUTTY_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    target_compile_definitions(${target} PRIVATE
        QT_NO_FOREACH
        QT_NO_URL_CAST_FROM_STRING
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT)
endfunction()
