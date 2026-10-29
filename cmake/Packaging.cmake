# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# Install rules, per-OS bundle metadata and CPack (.deb, .rpm).
#
# Bundled builds (AppImage, Flatpak, macOS .app, Windows installer) are
# assembled by the release workflow with linuxdeploy / flatpak-builder /
# macdeployqt / windeployqt; this file only describes what nutty itself
# installs.

include(GNUInstallDirs)

set(NUTTY_APPICON_DIR "${PROJECT_SOURCE_DIR}/resources/appicon")

# --- macOS bundle -------------------------------------------------------------
if(APPLE)
    set(NUTTY_ICNS "${NUTTY_APPICON_DIR}/nutty.icns")
    if(EXISTS "${NUTTY_ICNS}")
        target_sources(nutty PRIVATE "${NUTTY_ICNS}")
        set_source_files_properties("${NUTTY_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
    set_target_properties(nutty PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "${NUTTY_APP_NAME}"
        MACOSX_BUNDLE_EXECUTABLE_NAME "nutty"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${NUTTY_APP_ID}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "nutty.icns"
        MACOSX_BUNDLE_COPYRIGHT "${NUTTY_COPYRIGHT}")
endif()

# --- Windows resources (version info, icon, manifest) ---------------------------
if(WIN32)
    set(NUTTY_ICO "${NUTTY_APPICON_DIR}/nutty.ico")
    if(EXISTS "${NUTTY_ICO}")
        set(NUTTY_RC_ICON "IDI_ICON1 ICON \"${NUTTY_ICO}\"")
    else()
        set(NUTTY_RC_ICON "")
    endif()
    string(REPLACE "/" "\\\\" NUTTY_RC_ICON "${NUTTY_RC_ICON}")
    set(NUTTY_RC_MANIFEST "${PROJECT_SOURCE_DIR}/packaging/windows/nutty.manifest")
    string(REPLACE "/" "\\\\" NUTTY_RC_MANIFEST "${NUTTY_RC_MANIFEST}")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/nutty.rc.in"
                   "${PROJECT_BINARY_DIR}/nutty.rc" @ONLY)
    target_sources(nutty PRIVATE "${PROJECT_BINARY_DIR}/nutty.rc")
    # The manifest is embedded through the .rc file; keep the linker's default out.
    if(MSVC)
        target_link_options(nutty PRIVATE /MANIFEST:NO)
    endif()
endif()

# --- Install rules -----------------------------------------------------------------
if(APPLE)
    install(TARGETS nutty BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS nutty RUNTIME DESTINATION .)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md"
            DESTINATION . OPTIONAL)
    # The MSVC runtime goes next to nutty.exe: the installer is per-user and the
    # portable zip has no installer, so vc_redist (which needs admin) is no option.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
else()
    install(TARGETS nutty RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/${NUTTY_APP_ID}.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/${NUTTY_APP_ID}.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    if(EXISTS "${NUTTY_APPICON_DIR}/nutty.svg")
        install(FILES "${NUTTY_APPICON_DIR}/nutty.svg"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
                RENAME "${NUTTY_APP_ID}.svg")
    endif()
    foreach(size 16 22 24 32 48 64 128 256 512)
        set(png "${NUTTY_APPICON_DIR}/png/nutty-${size}.png")
        if(EXISTS "${png}")
            install(FILES "${png}"
                    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
                    RENAME "${NUTTY_APP_ID}.png")
        endif()
    endforeach()
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}" OPTIONAL)
endif()

# --- CPack (Linux: .deb, .rpm) --------------------------------------------
if(NOT NUTTY_LINUX)
    return()
endif()

set(CPACK_PACKAGE_NAME "nutty")
set(CPACK_PACKAGE_VENDOR "${NUTTY_ORGANIZATION}")
set(CPACK_PACKAGE_CONTACT "${NUTTY_ORGANIZATION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION
    "nutty lives in the system tray and watches your UPS units, power stations and \
batteries: NUT servers, apcupsd, EcoFlow, Home Assistant and the OS's own power \
sources. It sends desktop, Pushover, ntfy, email, webhook and PagerDuty \
notifications when something changes, and can sleep, hibernate or shut the \
computer down on low battery.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "nutty")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "DEB;RPM")

# .deb ------------------------------------------------------------------------------
# dpkg-shlibdeps resolves the directly linked Qt libraries (and their t64
# names on Ubuntu 24.04+). It can't see what Qt loads at runtime: platform
# plugins, the SVG/SQLite/TLS plugins and the QML modules. Those are listed by
# hand, and CI installs the .deb in a clean ubuntu:24.04 to check them.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${NUTTY_ORGANIZATION}")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qt6-qpa-plugins, qt6-wayland, libqt6svg6, libqt6sql6-sqlite, \
qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-templates, \
qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, qml6-module-qtquick-window, \
qml6-module-qtqml-workerscript, qml6-module-qtqml-models")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-svg-plugins")

# .rpm ------------------------------------------------------------------------------
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtsvg, qt6-qtwayland")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/metainfo
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps
    /usr/share/doc)
foreach(size 16 22 24 32 48 64 128 256 512)
    list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/icons/hicolor/${size}x${size}
        /usr/share/icons/hicolor/${size}x${size}/apps)
endforeach()

include(CPack)
