# Changelog

All notable changes to nutty are documented here. This project adheres to
[Semantic Versioning](https://semver.org).

## [Unreleased]

## [0.1.0] - 2026-10-29

### Added
- **First release.** nutty is a tray app for Windows, macOS and Linux (x86_64
  and arm64). It monitors UPS units, power stations and batteries.
- **Sources**:
  - **NUT**: STARTTLS with certificate pinning, UPS discovery and an optional
    `upsmon secondary` login.
  - **EcoFlow**: through the official IoT Open Platform. A DELTA Pro's device
    page also shows each battery pack, where the power comes from and goes,
    and the energy in and out each day, and can change the unit's settings:
    outputs, X-Boost, charging speed, charge limits, backup reserve, timeouts
    and more. nutty asks before every change, and twice before one that can
    cut power.
  - **apcupsd**.
  - **The OS's own power sources**: UPower, IOKit and Win32.
  - **Generic HTTP/JSON**: with a Home Assistant preset.
- **Adding devices**: a new device on the same EcoFlow account, NUT server or
  Home Assistant starts with its keys or login and finds the devices there on
  its own. Changing shared keys or a login on one device offers to change the
  others that use them.
- **Notifications**: desktop, Pushover, ntfy, email, webhooks and PagerDuty,
  whose alerts resolve themselves when power or contact comes back. Each
  channel picks which events it gets.
- **Power actions per device or device group**: sleep, hibernate, shutdown or a
  command. They trigger on time on battery, charge, runtime, low battery, forced
  shutdown or lost contact. Countdowns can be cancelled, and nutty shortens
  them when the battery won't last that long. Each rule fires once per outage.
  A dry-run mode is included.
- **Home screen**: a live card per device, with an animated battery and a
  24-hour sparkline. Recent activity sits below the cards.
- **History**: an event log, a list of outages, and charge, load and runtime
  charts, all exportable to CSV.
- **Appearance**: light and dark themes that follow the system or can be
  forced. Mica on Windows 11, vibrancy on macOS, and a custom title bar on
  Linux that scales on KDE and GNOME. The tray icon is an acorn that fills up
  with the charge, or a battery.
- **Back and forward** between pages with the back button, the mouse's side
  buttons, or Alt+Left / Alt+Right (⌘[ / ⌘] on macOS).
- **Secrets**: kept in the OS keychain.
- **Packages**:
  - **Linux**: .deb, .rpm, AppImage and Flatpak.
  - **macOS**: a universal .dmg and .zip.
  - **Windows**: an installer and a portable .zip.
