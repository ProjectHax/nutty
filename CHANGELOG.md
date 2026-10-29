# Changelog

All notable changes to nutty are documented here. This project adheres to
[Semantic Versioning](https://semver.org).

## [Unreleased]

### Added
- **PagerDuty** as a notification channel (Events API v2, US or EU). Going on
  battery, a low battery, lost contact, a scheduled or failed shutdown and so
  on each open an alert. Power coming back resolves the device's power
  alerts, contact coming back its lost-contact alert, and a cancelled action
  its action alert.
- **Adding another device on the same account is quick.** A new EcoFlow device
  starts with the keys of the last one, and a new NUT or Home Assistant device
  with its server and login. The search for devices then runs on its own, and
  devices that are already set up show as "Added".
- **Back and forward**: a back button next to the page title returns to
  whatever page you were on before, such as the device list after opening a
  device. The mouse's back and forward buttons, the Back and Forward keys and
  Alt+Left / Alt+Right (⌘[ / ⌘] on macOS) do the same, anywhere in the
  window, except while a dialog is open.
- **Changing shared keys or logins**: editing them on one device offers to make
  the same change on the other devices that use them.
- **An acorn tray icon** that fills up with the charge like a battery. It's
  the new default; Settings → Appearance → Tray icon switches back to the
  battery.
- **Change an EcoFlow DELTA Pro's settings** from its device page: AC output,
  X-Boost, the AC timeout, the 12 V output, the AC charging speed, the car
  charging current, the charge and discharge limits, the backup reserve and
  its level, the beeper, the screen and unit timeouts, and the smart
  generator's start and stop levels. nutty asks before every change, and
  asks a second time, with what could happen, before one that can cut power
  or load the wall circuit: AC output, X-Boost, the AC charging speed,
  turning the 12 V output off, and the AC and unit timeouts. The change goes
  out through EcoFlow's cloud, and the setting shows as changing until the
  device reports the new value. Every change, and every one that didn't take,
  is logged in History.
- **More EcoFlow readings** on the device page: where the power comes from and
  goes right now (AC, solar, each kind of output), each battery pack's health,
  cycles, temperature, voltage, stored energy, capacity and cell spread, the
  energy that went in and out each day for the last week or month (with the
  solar share), the unit's lifetime totals and its temperatures. The chart
  gains Power (in and out together), Solar and Temperature.

### Changed
- **A new app icon**: a flat acorn whose stem is the battery terminal and whose
  body holds the charge bars, in place of the glossy hex nut.
- **A bigger tray icon**: the battery now fills its square instead of half of
  it, with straight edges on whole pixels, so it's as big and as sharp as the
  panel's own icons. More sizes are offered to panels that scale icons, such as
  GNOME's AppIndicator extension.
- **Roomier pickers and forms**: the device and channel type cards line up in
  even rows, the event checkboxes of a channel sit in columns, and dialogs
  leave space above their buttons.
- **A quieter look**: smaller corners on cards, buttons, chips and dialogs;
  cards are flat with a border instead of a shadow and don't lift on hover;
  the glow behind device pictures, the battery's gloss and charging glow, the
  pulsing glow in the countdown and the sheen on the home banner are gone, and
  icons sit on their own instead of on tinted tiles.
- **Less to read**: settings, dialogs and empty pages drop the explanations
  their titles already make clear. The ones that matter, like what can't be
  cancelled, stay.
- **Home's title** no longer has the device count under it; the banner and
  the cards below already say how the devices are doing.
- **Device cards on Home** line up: the charge bars start at the same place
  whatever the charge, the bars lose their battery tip, and cards side by side
  are the same height.
- **The device list lines up**: every status chip is as wide as the widest
  one, and the charge next to it keeps the same column, so neither shifts with
  the length of a status.
- **Settings and lists line up**: the rows of Settings, Power actions, About,
  notification channels, rules and groups share one inset, icon column and
  height, so icons and titles start at the same place on every page and a row
  with a switch is as tall as one with a dropdown. Buttons, fields, dropdowns
  and segmented controls are the same height side by side, and "Use the
  system title bar" has a window icon instead of a plain square.

### Fixed
- **Clicks went through dialogs.** Choosing a type in "Add device" could also
  click the device row underneath it, which opened that device and closed the
  dialog.
- **Editors opened empty.** The add and edit dialogs for devices, notification
  channels and rules had no height, so only their title and buttons showed.
- **Long names in "Found:"**: a long device name pushed the "Use" button out
  of view.
- **UPS names and models on Linux** no longer include the firmware version
  that some USB units report as part of their model.
- **Event counts of notification channels** were one short for channels whose
  settings file was written by hand.
- **The help for "Run command"** said commands run without a shell. They run
  in the system shell (`sh`, or `cmd` on Windows), so pipes, redirects and
  quotes work.

## [0.1.0] - 2026-10-27

### Added
- **First release.** nutty is a tray app for Windows, macOS and Linux (x86_64
  and arm64). It monitors UPS units, power stations and batteries.
- **Sources**:
  - **NUT**: STARTTLS with certificate pinning, UPS discovery and an optional
    `upsmon secondary` login.
  - **EcoFlow**: through the official IoT Open Platform.
  - **apcupsd**.
  - **The OS's own power sources**: UPower, IOKit and Win32.
  - **Generic HTTP/JSON**: with a Home Assistant preset.
- **Notifications**: desktop, Pushover, ntfy, email and webhooks, with a
  choice of events for each channel.
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
  Linux that scales on KDE and GNOME.
- **Secrets**: kept in the OS keychain.
- **Packages**:
  - **Linux**: .deb, .rpm, AppImage and Flatpak.
  - **macOS**: a universal .dmg and .zip.
  - **Windows**: an installer and a portable .zip.
