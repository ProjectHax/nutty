// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

#include "core/model/Event.h"

namespace nutty {

Severity defaultSeverity(EventType type)
{
    switch (type) {
    case EventType::LowBattery:
    case EventType::ForcedShutdown:
    case EventType::ActionScheduled:
    case EventType::ActionExecuted:
    case EventType::ActionFailed:
        return Severity::Critical;
    case EventType::SettingFailed:
    case EventType::OnBattery:
    case EventType::ChargeBelow:
    case EventType::RuntimeBelow:
    case EventType::ReplaceBattery:
    case EventType::Overload:
    case EventType::Alarm:
    case EventType::CommLost:
    case EventType::AuthFailed:
    case EventType::UpsOff:
        return Severity::Warning;
    case EventType::PowerRestored:
    case EventType::Bypass:
    case EventType::CommRestored:
    case EventType::ActionCancelled:
    case EventType::Test:
    case EventType::SettingChanged:
        return Severity::Info;
    }
    return Severity::Info;
}

}  // namespace nutty
