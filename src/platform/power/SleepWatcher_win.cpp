// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "platform/power/SleepWatcher.h"

#include <QMetaObject>
#include <QPointer>

#include <windows.h>
// powrprof.h relies on the types from windows.h.
#include <powrprof.h>

namespace nutty {

namespace {

struct CallbackContext {
    QPointer<SleepWatcher> watcher;
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params {};
};

// Called on a system thread; hop to the watcher's thread before emitting.
ULONG CALLBACK onPowerEvent(PVOID context, ULONG type, PVOID)
{
    QPointer<SleepWatcher> watcher = static_cast<CallbackContext*>(context)->watcher;
    if (!watcher)
        return ERROR_SUCCESS;
    if (type == PBT_APMSUSPEND) {
        QMetaObject::invokeMethod(
            watcher.data(),
            [watcher]() {
                if (watcher)
                    watcher->notifySleeping();
            },
            Qt::QueuedConnection);
    } else if (type == PBT_APMRESUMEAUTOMATIC || type == PBT_APMRESUMESUSPEND) {
        QMetaObject::invokeMethod(
            watcher.data(),
            [watcher]() {
                if (watcher)
                    watcher->notifyResumed();
            },
            Qt::QueuedConnection);
    }
    return ERROR_SUCCESS;
}

}  // namespace

struct SleepWatcher::Private {
    HPOWERNOTIFY handle = nullptr;
    CallbackContext context;
};

SleepWatcher::SleepWatcher(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->context.watcher = this;
    d->context.params.Callback = onPowerEvent;
    d->context.params.Context = &d->context;
    HPOWERNOTIFY handle = nullptr;
    if (PowerRegisterSuspendResumeNotification(
            DEVICE_NOTIFY_CALLBACK, static_cast<HANDLE>(&d->context.params), &handle)
        == ERROR_SUCCESS)
        d->handle = handle;
}

SleepWatcher::~SleepWatcher()
{
    if (d->handle)
        PowerUnregisterSuspendResumeNotification(d->handle);
}

bool SleepWatcher::isSupported() const
{
    return d->handle != nullptr;
}

}  // namespace nutty
