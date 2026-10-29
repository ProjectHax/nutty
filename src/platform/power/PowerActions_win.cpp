// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/PowerActions.h"

#include <QCoreApplication>

#include <windows.h>
// powrprof.h relies on the types from windows.h.
#include <powrprof.h>

namespace nutty::PowerActions {

namespace {

QString lastErrorText(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    QString text
        = len ? QString::fromWCharArray(buffer, int(len)).trimmed() : QStringLiteral("error %1").arg(code);
    if (buffer)
        LocalFree(buffer);
    return text;
}

// SetSuspendState and InitiateShutdownW both need SeShutdownPrivilege, which
// every interactive user holds but has disabled by default.
bool enableShutdownPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES privileges {};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)
        && AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr)
        && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

}  // namespace

PowerCapability capability(ActionType action)
{
    if (action == ActionType::Notify || action == ActionType::Command)
        return { true, {} };
    if (action == ActionType::Shutdown)
        return { true, {} };

    SYSTEM_POWER_CAPABILITIES caps {};
    if (!GetPwrCapabilities(&caps))
        return { false,
            QCoreApplication::translate(
                "nutty::PowerActions", "Windows did not report its power capabilities.") };

    if (action == ActionType::Sleep) {
        if (caps.SystemS3)
            return { true, {} };
        if (caps.AoAc)
            return { false,
                QCoreApplication::translate(
                    "nutty::PowerActions", "This PC uses Modern Standby, which apps can't start.") };
        return { false,
            QCoreApplication::translate("nutty::PowerActions", "This PC doesn't support sleep.") };
    }
    if (action == ActionType::Hibernate) {
        if (caps.SystemS4 && caps.HiberFilePresent)
            return { true, {} };
        return { false,
            QCoreApplication::translate("nutty::PowerActions", "Hibernation is turned off on this PC.") };
    }
    return { false, {} };
}

std::expected<void, QString> execute(ActionType action)
{
    if (!enableShutdownPrivilege())
        return std::unexpected(QCoreApplication::translate(
            "nutty::PowerActions", "nutty is not allowed to shut down or suspend this PC."));

    switch (action) {
    case ActionType::Sleep:
    case ActionType::Hibernate: {
        const BOOLEAN hibernate = action == ActionType::Hibernate ? TRUE : FALSE;
        if (!SetSuspendState(hibernate, FALSE, FALSE))
            return std::unexpected(lastErrorText(GetLastError()));
        return {};
    }
    case ActionType::Shutdown: {
        // Apps that hold up shutdown to ask about unsaved work are closed
        // anyway: on a dying battery the alternative is losing power.
        const DWORD result = InitiateShutdownW(nullptr, nullptr, 0,
            SHUTDOWN_POWEROFF | SHUTDOWN_FORCE_OTHERS | SHUTDOWN_FORCE_SELF,
            SHTDN_REASON_MAJOR_POWER | SHTDN_REASON_MINOR_ENVIRONMENT);
        if (result != ERROR_SUCCESS)
            return std::unexpected(lastErrorText(result));
        return {};
    }
    case ActionType::Notify:
    case ActionType::Command:
        break;
    }
    return std::unexpected(QCoreApplication::translate("nutty::PowerActions", "Not a power action."));
}

std::expected<void, QString> requestShutdownPermission()
{
    return {};
}

}  // namespace nutty::PowerActions
