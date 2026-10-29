# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC

# The core is split into small libraries that only depend downward on
# nutty_model. Sources and notifiers are created through registries declared
# in the model, so the engine never links the concrete implementations.
#
#   nutty_model    shared types, Source/Notifier base classes, registries
#   nutty_sources  NUT, apcupsd, EcoFlow, HTTP/JSON
#   nutty_storage  config (QSettings), history (SQLite), secret-store fallback
#   nutty_engine   event detection, rules, action arbitration, the Monitor
#   nutty_notify   Pushover, ntfy, email (SMTP), webhooks, PagerDuty, dispatch

nutty_add_library(nutty_model
    DIRS "${CMAKE_CURRENT_SOURCE_DIR}/model"
    LIBS Qt::Core Qt::Network)
target_compile_definitions(nutty_model PUBLIC
    NUTTY_VERSION="${PROJECT_VERSION}"
    NUTTY_APP_ID="${NUTTY_APP_ID}"
    NUTTY_APP_NAME="${NUTTY_APP_NAME}"
    NUTTY_ORGANIZATION="${NUTTY_ORGANIZATION}"
    NUTTY_HOMEPAGE="${PROJECT_HOMEPAGE_URL}")

nutty_add_library(nutty_sources
    DIRS "${CMAKE_CURRENT_SOURCE_DIR}/sources"
    LIBS nutty_model Qt::Network)

nutty_add_library(nutty_storage
    DIRS "${CMAKE_CURRENT_SOURCE_DIR}/storage"
    LIBS nutty_model Qt::Sql)

nutty_add_library(nutty_engine
    DIRS "${CMAKE_CURRENT_SOURCE_DIR}/engine"
    LIBS nutty_model nutty_storage)

nutty_add_library(nutty_notify
    DIRS "${CMAKE_CURRENT_SOURCE_DIR}/notify"
    LIBS nutty_model Qt::Network)

# Resources the core needs at runtime (e.g. the EcoFlow quota-key mapping).
file(GLOB_RECURSE NUTTY_SOURCE_RESOURCES CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/sources/resources/*")
if(NUTTY_SOURCE_RESOURCES)
    qt_add_resources(nutty_sources "nutty_sources_resources"
        PREFIX "/nutty"
        FILES ${NUTTY_SOURCE_RESOURCES})
endif()
