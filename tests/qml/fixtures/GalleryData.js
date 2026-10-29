.pragma library
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

// Made-up devices, history and activity for the component gallery.

const minute = 60 * 1000
const hour = 60 * minute

function devices(now) {
    return [
        { name: "Office UPS", subtitle: "NUT · office-ups", kind: "TowerUps", status: "charging",
          charge: 86, runtime: 2820, load: 23, wattsIn: 0, wattsOut: 142, updated: now - 3000,
          history: wave(48, 70, 86, 0.8) },
        { name: "Rack UPS", subtitle: "NUT · rack-ups", kind: "RackUps", status: "onBattery",
          charge: 63, runtime: 1260, load: 41, wattsIn: 0, wattsOut: 388, updated: now - 2000,
          history: drain(48, 100, 63) },
        { name: "Camping station", subtitle: "EcoFlow · Open API", kind: "PowerStation", status: "lowBattery",
          charge: 12, runtime: 540, load: -1, wattsIn: 0, wattsOut: 96, updated: now - 9000,
          history: drain(48, 58, 12) },
        { name: "Laptop", subtitle: "This computer", kind: "Laptop", status: "stale",
          charge: 74, runtime: 14400, load: -1, wattsIn: -1, wattsOut: 11, updated: now - 7 * minute,
          history: wave(48, 70, 78, 0.3) },
        { name: "Garage battery", subtitle: "HTTP · JSON", kind: "HomeBattery", status: "offline",
          charge: -1, runtime: -1, load: -1, wattsIn: -1, wattsOut: -1, updated: now - 3 * hour,
          history: [] },
    ]
}

function wave(n, lo, hi, freq) {
    const out = []
    for (let i = 0; i < n; ++i)
        out.push(lo + (hi - lo) * (0.5 + 0.5 * Math.sin(i / n * Math.PI * 2 * freq - 1.2)))
    return out
}

function drain(n, from, to) {
    const out = []
    for (let i = 0; i < n; ++i) {
        const t = i / (n - 1)
        out.push(t < 0.55 ? from : from - (from - to) * Math.pow((t - 0.55) / 0.45, 1.1))
    }
    return out
}

// 24 h of charge readings every 5 minutes, with an outage 14 h ago and one
// still going on.
function series(now) {
    const pts = []
    const start = now - 24 * hour
    const out1 = now - 14 * hour, out1End = out1 + 22 * minute
    const out2 = now - 26 * minute
    let charge = 100
    for (let t = start; t <= now; t += 5 * minute) {
        if (t >= out1 && t < out1End)
            charge = Math.max(0, charge - 4.6)
        else if (t >= out2)
            charge = Math.max(63, charge - 7.4)
        else if (charge < 100)
            charge = Math.min(100, charge + 1.4)
        pts.push({ x: t, y: charge })
    }
    pts[pts.length - 1].y = 63
    return { points: pts, bands: [{ from: out1, to: out1End }, { from: out2, to: now }] }
}

function activity(now) {
    const today = qsTr("Today")
    const yesterday = qsTr("Yesterday")
    return [
        { timestamp: now - 26 * minute, type: "OnBattery", severity: "Warning", deviceName: "Rack UPS",
          title: qsTr("On battery"), message: qsTr("Input power lost. 98 %, about 52 min left."), day: today },
        { timestamp: now - 41 * minute, type: "LowBattery", severity: "Critical", deviceName: "Camping station",
          title: qsTr("Low battery"), message: qsTr("12 % left, about 9 min at 96 W."), day: today },
        { timestamp: now - 2 * hour, type: "CommLost", severity: "Warning", deviceName: "Garage battery",
          title: qsTr("Connection lost"), message: qsTr("No response for 3 polls."), day: today },
        { timestamp: now - 13.6 * hour, type: "PowerRestored", severity: "Info", deviceName: "Rack UPS",
          title: qsTr("Power restored"), message: qsTr("On line after 22 min on battery."), day: today },
        { timestamp: now - 13.7 * hour, type: "ActionCancelled", severity: "Info", deviceName: "Rack UPS",
          title: qsTr("Shutdown cancelled"), message: qsTr("Cancelled from the countdown window."), day: today },
        { timestamp: now - 14 * hour, type: "OnBattery", severity: "Warning", deviceName: "Rack UPS",
          title: qsTr("On battery"), message: qsTr("Input power lost. 100 %, about 1 h 10 min left."), day: today },
        { timestamp: now - 30 * hour, type: "ReplaceBattery", severity: "Warning", deviceName: "Office UPS",
          title: qsTr("Replace battery"), message: qsTr("The UPS reports its battery needs replacing."), day: yesterday },
    ]
}

const icons = [
    "home", "history", "devices", "server", "bell", "bolt", "shield", "gear", "info", "plus", "minus",
    "trash", "edit", "refresh", "close", "minimize", "maximize", "restore", "battery", "battery-full",
    "battery-low", "battery-alert", "battery-charging", "plug", "plug-off", "warning", "check",
    "check-circle", "search", "export", "download", "image", "chevron-left", "chevron-right", "chevron-up",
    "chevron-down", "power", "moon", "sun", "monitor", "laptop", "clock", "timer", "calendar", "link",
    "link-off", "lock", "key", "mail", "send", "message", "phone", "terminal", "gauge", "bypass", "globe",
    "copy", "external", "play", "pause", "eye", "eye-off", "sliders", "filter", "menu", "sidebar", "more",
    "thermometer", "test",
]

const kinds = ["TowerUps", "RackUps", "PowerStation", "HomeBattery", "Laptop", "Generic"]
