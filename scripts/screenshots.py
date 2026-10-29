#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC
"""Renders the screenshots in docs/screenshots from made-up data.

Starts a simulated NUT server (fake-upsd.py) with three UPS units and a
simulated EcoFlow cloud with one DELTA Pro, writes a demo config and a week
of history into a temporary folder, then has nutty render each page with
--screenshot. Your own settings, history and keychain are never touched, and
nothing reaches the desktop: nutty runs offscreen without a session bus.

    scripts/screenshots.py                   # every shot, at 1.5x
    scripts/screenshots.py --only home-dark  # just one
    scripts/screenshots.py --nutty build/dev/src/app/nutty --scale 2
"""

import argparse
import base64
import http.server
import importlib.util
import json
import math
import os
import random
import re
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

RACK = "6f1c2a6e-1f4b-4d6e-9a51-6c1f0e2b7a11"
DESK = "9b2d4c8e-3a5f-4b7c-8d19-2e4f6a8c0b22"
CLOSET = "3d5f7b9c-2e4a-4c6d-8f1b-5a7c9e1b3d44"
SHED = "7e9a1c3e-4b6d-4f8a-9c2e-6b8d0f2a4c55"
OFFICE = "2c4e6a8b-5d7f-4a1c-9e3b-7f9a1c3e5d33"
SHED_SERIAL = "DCABDEMO0000042"  # DELTA Pro serials start with DCAB

# (file, page, theme, device, window size)
SHOTS = [
    ("home-dark", "home", "dark", None, "1280x820"),
    ("home-light", "home", "light", None, "1280x820"),
    ("ecoflow-dark", "device", "dark", SHED, "1280x820"),
    ("ecoflow-full-light", "device", "light", SHED, "1280x3300"),
    ("history-dark", "history", "dark", None, "1280x820"),
    ("actions-light", "actions", "light", None, "1280x820"),
    ("notifications-dark", "notifications", "dark", None, "1280x820"),
    ("countdown-dark", "countdown", "dark", None, None),
]


# --- simulated NUT server --------------------------------------------------------

def load_fake_upsd():
    spec = importlib.util.spec_from_file_location("fake_upsd", ROOT / "scripts" / "fake-upsd.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def start_nut(port, live):
    fake = load_fake_upsd()

    class DemoUps(fake.Ups):
        """A unit that holds still for the camera, with its own model and load."""

        def __init__(self, name, model, serial, load, nominal, status, charge, runtime):
            super().__init__(name)
            self.model, self.serial, self.load, self.nominal = model, serial, load, nominal
            self.status, self.charge, self.runtime = status, charge, runtime
            self.drain = 0.2

        def tick(self):
            self.last = time.monotonic()

        def variables(self):
            v = super().variables()
            v.pop("device.mfr")
            v.update({
                "device.model": self.model,
                "device.serial": self.serial,
                "ups.load": str(self.load),
                "ups.realpower.nominal": str(self.nominal),
                "ups.temperature": "31.0" if self.status.startswith("OB") else "27.5",
            })
            return v

    args = argparse.Namespace(host="127.0.0.1", port=port, ups=["rack", "desk", "closet"], password=None,
                              cert=None, key=None, verbose=False)
    server = fake.Server(args)
    server.units = {
        "rack": DemoUps("rack", "Line-Interactive 1500 RM 2U", "DEMO0000001", 38, 1350, *live[RACK]),
        "desk": DemoUps("desk", "Tower 850", "DEMO0000002", 17, 510, *live[DESK]),
        "closet": DemoUps("closet", "Tower 1000", "DEMO0000003", 26, 600, *live[CLOSET]),
    }
    threading.Thread(target=server.serve, daemon=True).start()


# --- simulated EcoFlow cloud -------------------------------------------------------

# A DELTA Pro with an extra battery, plugged in, charging from solar and
# running a fridge and a few small things. update_shed() sets the readings
# that change to where the simulated week ends.
SHED_QUOTA = {
    "ems.lcdShowSoc": 72, "ems.dsgRemainTime": 690, "ems.chgRemainTime": 95,
    "ems.maxChargeSoc": 90, "ems.minDsgSoc": 10, "ems.minOpenOilEbSoc": 15, "ems.maxCloseOilEbSoc": 80,
    "ems.bms1Online": 3, "ems.bms2Online": 0,
    "bmsMaster.soc": 71, "bmsMaster.soh": 98, "bmsMaster.cycles": 214, "bmsMaster.temp": 29,
    "bmsMaster.vol": 52100, "bmsMaster.amp": 4300, "bmsMaster.fullCap": 80000, "bmsMaster.designCap": 80000,
    "bmsMaster.remainCap": 56800, "bmsMaster.minCellVol": 3402, "bmsMaster.maxCellVol": 3409,
    "bmsSlave1.soc": 73, "bmsSlave1.soh": 100, "bmsSlave1.cycles": 61, "bmsSlave1.temp": 27,
    "bmsSlave1.vol": 52300, "bmsSlave1.amp": 4100, "bmsSlave1.fullCap": 80000, "bmsSlave1.designCap": 80000,
    "bmsSlave1.remainCap": 58400, "bmsSlave1.minCellVol": 3405, "bmsSlave1.maxCellVol": 3410,
    "pd.wattsInSum": 640, "pd.wattsOutSum": 312, "pd.remainTime": 690,
    "inv.inputWatts": 0, "inv.outputWatts": 262, "inv.acInVol": 121000, "inv.invOutVol": 120000,
    "inv.outTemp": 34, "inv.dcInTemp": 31, "mppt.mpptTemp": 38,
    "mppt.inWatts": 640, "mppt.carOutWatts": 0, "mppt.carState": 1, "mppt.cfgDcChgCurrent": 8000,
    "pd.usb1Watts": 6, "pd.usb2Watts": 0, "pd.qcUsb1Watts": 12, "pd.qcUsb2Watts": 0,
    "pd.typec1Watts": 32, "pd.typec2Watts": 0, "pd.wirelessWatts": 0,
    "pd.chgPowerAc": 418000, "pd.chgSunPower": 1268000, "pd.chgPowerDc": 9200,
    "pd.dsgPowerAc": 1312000, "pd.dsgPowerDc": 84000,
    "inv.cfgAcEnabled": 1, "inv.cfgAcXboost": 1, "inv.cfgStandbyMin": 0, "inv.cfgSlowChgWatts": 1200,
    "inv.cfgAcOutVoltage": 120000,
    "pd.watthisconfig": 1, "pd.bppowerSoc": 40, "pd.beepState": 1, "pd.lcdOffSec": 300, "pd.standByMode": 0,
}


def update_shed(charge, solar, out):
    capacity = 7200.0  # Wh, with the extra battery
    usb, usbc = 18, 32
    q = SHED_QUOTA
    q["ems.lcdShowSoc"] = round(charge)
    q["bmsMaster.soc"], q["bmsSlave1.soc"] = round(charge) - 1, round(charge) + 1
    q["bmsMaster.remainCap"] = round(charge / 100 * 80000)
    q["bmsSlave1.remainCap"] = round((charge + 1) / 100 * 80000)
    q["bmsMaster.amp"] = q["bmsSlave1.amp"] = round((solar - out) / 52.2 / 2 * 1000)
    q["mppt.inWatts"] = q["pd.wattsInSum"] = round(solar)
    q["pd.wattsOutSum"] = round(out)
    q["inv.outputWatts"] = max(0, round(out) - usb - usbc)
    q["ems.dsgRemainTime"] = round(charge / 100 * capacity / max(out, 1) * 60)
    q["ems.chgRemainTime"] = (round((90 - charge) / 100 * capacity / (solar - out) * 60)
                              if solar > out + 5 and charge < 90 else 5999)
    q["mppt.mpptTemp"] = round(25 + 15 * solar / 1500)


class EcoFlowCloud(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/iot-open/sign/device/list":
            data = [{"sn": SHED_SERIAL, "online": 1, "productName": "DELTA Pro", "deviceName": "Shed station"}]
        elif path == "/iot-open/sign/device/quota/all":
            data = SHED_QUOTA
        else:
            self.send_error(404)
            return
        body = json.dumps({"code": "0", "message": "Success", "data": data}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def start_ecoflow(port):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), EcoFlowCloud)
    threading.Thread(target=server.serve_forever, daemon=True).start()


# --- config ------------------------------------------------------------------------

def write_config(folder, nut_port, eco_port):
    lines = ["[General]", "schemaVersion=1", "", "[devices]"]
    devices = [
        (RACK, "Rack UPS", "Nut", "RackUps", {"host": "localhost", "port": nut_port, "ups": "rack"}),
        (DESK, "Desk UPS", "Nut", "TowerUps", {"host": "localhost", "port": nut_port, "ups": "desk"}),
        (CLOSET, "Network closet", "Nut", "TowerUps", {"host": "localhost", "port": nut_port, "ups": "closet"}),
        (SHED, "Shed station", "EcoFlow", "PowerStation",
         {"region": "custom", "host": f"http://127.0.0.1:{eco_port}", "serial": SHED_SERIAL}),
    ]
    for i, (uid, name, kind_type, kind, settings) in enumerate(devices, 1):
        lines += [f"{i}\\id={{{uid}}}", f"{i}\\name={name}", f"{i}\\type={kind_type}", f"{i}\\enabled=true",
                  f"{i}\\kind={kind}", f"{i}\\pollIntervalSec=2", f"{i}\\notifyChargeBelow=50",
                  f"{i}\\notifyRuntimeBelowSec=600"]
        lines += [f"{i}\\settings\\{k}={v}" for k, v in settings.items()]
    lines += [f"size={len(devices)}", "", "[groups]", f"1\\id={{{OFFICE}}}", "1\\name=Office",
              f"1\\members={{{RACK}}}, {{{DESK}}}", "size=1", "", "[rules]"]
    rules = [
        ("Shut down when the rack runs low", "Device", RACK, "ChargeBelow", 30, "Shutdown", 60),
        ("Forced shutdown", "Device", RACK, "ForcedShutdown", 0, "Shutdown", 0),
        ("Hibernate the desk PC", "Device", DESK, "OnBatteryFor", 300, "Hibernate", 120),
        ("Office running out", "GroupAll", OFFICE, "RuntimeBelow", 300, "Shutdown", 90),
        ("Closet is low", "Device", CLOSET, "ChargeBelow", 25, "Notify", 0),
    ]
    for i, (name, scope, target, cond, threshold, action, countdown) in enumerate(rules, 1):
        lines += [f"{i}\\id={{{i:08x}-0000-4000-8000-0000000000{i:02d}}}", f"{i}\\name={name}",
                  f"{i}\\enabled=true", f"{i}\\scope={scope}", f"{i}\\target={{{target}}}",
                  f"{i}\\condition={cond}", f"{i}\\threshold={threshold}", f"{i}\\action={action}",
                  f"{i}\\countdownSec={countdown}"]
    lines += [f"size={len(rules)}", "", "[channels]"]
    channels = [
        ("This computer", "Desktop", {}),
        ("Phone", "Ntfy", {"server": "https://ntfy.sh", "topic": "home-power-demo"}),
        ("Ops mailbox", "Email", {"host": "smtp.example.com", "port": 587, "security": "starttls",
                                  "from": "nutty@example.com", "to": "ops@example.com"}),
        ("Home automation", "Webhook", {"url": "http://homeassistant.local:8123/api/webhook/nutty",
                                        "method": "POST", "preset": "json"}),
    ]
    for i, (name, kind, settings) in enumerate(channels, 1):
        lines += [f"{i}\\id={{{i:08x}-1111-4000-8000-0000000000{i:02d}}}", f"{i}\\name={name}",
                  f"{i}\\type={kind}", f"{i}\\enabled=true"]
        lines += [f"{i}\\settings\\{k}={v}" for k, v in settings.items()]
    lines += [f"size={len(channels)}", ""]
    (folder / "nutty.ini").write_text("\n".join(lines))

    fake = lambda text: base64.b64encode(text.encode()).decode()
    secrets = {f"device/{SHED}/accessKey": fake("demo-access-key"),
               f"device/{SHED}/secretKey": fake("demo-secret-key")}
    (folder / "secrets.json").write_text(json.dumps(secrets))


# --- history -----------------------------------------------------------------------

def schema():
    """The history schema, read from HistoryStore.cpp so it never drifts."""
    text = (ROOT / "src" / "core" / "storage" / "HistoryStore.cpp").read_text()
    version = int(re.search(r"kSchemaVersion = (\d+)", (ROOT / "src" / "core" / "storage" /
                                                          "HistoryStore.h").read_text()).group(1))
    statements = []
    for n in range(1, version + 1):
        body = re.search(r"const char\* const kSchemaV%d\[\] = \{(.*?)\n\};" % n, text, re.S).group(1)
        current = ""
        for literal, separator in re.findall(r'"((?:[^"\\]|\\.)*)"(\s*,?)', body):
            current += literal
            if "," in separator:
                statements.append(current)
                current = ""
        if current:
            statements.append(current)
    return version, statements


ONLINE, ON_BATTERY, CHARGING, DISCHARGING = 1, 2, 32, 64


def seed_history(folder):
    version, statements = schema()
    db = sqlite3.connect(folder / "history.db")
    db.execute("CREATE TABLE schema_version (version INTEGER NOT NULL)")
    for s in statements:
        db.execute(s)
    db.execute("INSERT INTO schema_version (version) VALUES (?)", (version,))

    random.seed(42)
    minute = 60_000
    day = 1440 * minute
    now = int(time.time()) // 60 * 60_000
    start = now - 7 * day
    names = {RACK: "Rack UPS", DESK: "Desk UPS", CLOSET: "Network closet", SHED: "Shed station"}
    # (start, minutes on battery, lowest charge) per device. The closet's
    # last one is still going: nutty logs that one itself when it starts.
    outages = {
        RACK: [(now - 3 * 60 * minute, 22, 61), (now - int(4.6 * day), 6, 90)],
        DESK: [(now - 3 * 60 * minute, 22, 44), (now - int(4.6 * day), 6, 83)],
        CLOSET: [(now - 3 * 60 * minute, 22, 52), (now - int(1.8 * day), 3, 93), (now - int(4.6 * day), 6, 88),
                 (now - 9 * minute, 10, 64)],
    }
    ups = {RACK: (38, 1350, 2880), DESK: (17, 510, 1560), CLOSET: (26, 600, 1500)}

    samples, events, episodes, live = [], [], [], {}

    def event(ts, device, kind, severity, title, message, flags, charge, extra=None):
        events.append((ts, device, names[device], kind, severity, title, message, flags, charge, None, None,
                       json.dumps(extra) if extra else None))

    for device, (load, nominal, full_runtime) in ups.items():
        charge = 100.0
        drop = None
        for ts in range(start, now, minute):
            outage = next((o for o in outages[device] if o[0] <= ts < o[0] + o[1] * minute), None)
            l = load + 4 * math.sin(ts / 5.4e6 + len(device)) + random.uniform(-1.5, 1.5)
            if outage:
                begin, length, lowest = outage
                if drop is None or drop[0] != begin:
                    drop = (begin, charge, (charge - lowest) / length)
                charge = drop[1] - drop[2] * (ts - begin) / minute
                flags = ON_BATTERY | DISCHARGING
                volts = 0.0
            else:
                charge = min(100.0, charge + 0.25)
                flags = ONLINE | (CHARGING if charge < 99.5 else 0)
                volts = round(121 + random.uniform(-1.5, 1.5), 1)
            runtime = int(full_runtime * charge / 100 * load / max(l, 5))
            samples.append((device, ts, round(charge, 1), runtime, round(l, 1), None,
                            round(l / 100 * nominal, 1), volts, flags, None, None))
        on_battery = bool(flags & ON_BATTERY)
        status = "OB DISCHRG" if on_battery else ("OL CHRG" if charge < 99.5 else "OL")
        live[device] = (status, round(charge), int(full_runtime * charge / 100 * (0.85 if on_battery else 1)))
        for begin, length, lowest in outages[device]:
            end = begin + length * minute
            ongoing = end > now - minute
            actions = []
            if device == RACK and length > 20:
                actions = [{"ts": begin + 15 * minute, "text": "Countdown to shut down started (60 s)"},
                           {"ts": begin + 16 * minute, "text": "Cancelled: power came back"}]
            if not ongoing:
                episodes.append((device, names[device], begin, end, 100, lowest, int(full_runtime * lowest / 100),
                                 json.dumps(actions)))
                event(begin, device, "OnBattery", "Warning", "On battery",
                      f"{names[device]} is on battery (100 %, {full_runtime // 60} min left)",
                      ON_BATTERY, 100)
                if lowest <= 50:
                    event(begin + int(length * minute * 0.8), device, "ChargeBelow", "Warning", "Battery below 50 %",
                          f"{names[device]} is running low ({lowest + 3} %)", ON_BATTERY, lowest + 3)
                event(end + 30_000, device, "PowerRestored", "Info", "Power restored",
                      f"{names[device]} is back on line power after {length} min on battery", ONLINE, lowest)

    # The station: solar by day, a fridge and small loads around the clock,
    # and wall power topping it up overnight when it runs low.
    charge, capacity, topping_up = 55.0, 7200.0, False
    sky = {}  # how sunny each day is
    for ts in range(start, now, minute):
        local = time.localtime(ts / 1000)
        hour = local.tm_hour + local.tm_min / 60
        cloud = sky.setdefault(local.tm_yday, 0.6 if local.tm_yday == time.localtime(now / 1000).tm_yday
                               else random.uniform(0.35, 1.0))
        solar = max(0.0, math.sin((hour - 6.5) / 13 * math.pi)) * 1500 * cloud
        solar = max(0.0, solar + random.uniform(-25, 25)) if solar > 0 else 0.0
        out = 150 + (110 if (ts // (37 * minute)) % 3 == 0 else 0) + (380 if 18 <= hour < 20.5 else 0)
        out += 250 if 10 <= hour < 16 else 0  # tools in the shed
        out += random.uniform(-15, 15)
        topping_up = charge < 30 or (topping_up and charge < 45)
        ac = 1200.0 if topping_up else 0.0
        win = solar + ac
        if charge >= 90 and win > out:  # held at the charge limit
            win, solar = out, min(solar, out)
        charge = max(5.0, min(90.0, charge + (win - out) / 60 / capacity * 100))
        temp = 25 + 9 * solar / 1500 + random.uniform(-0.4, 0.4)
        flags = ONLINE | (CHARGING if win > out + 5 else 0)
        samples.append((SHED, ts, round(charge, 1), int(charge / 100 * capacity / max(out, 50) * 3600),
                        None, round(win, 1), round(out, 1), 121.0, flags, round(solar, 1), round(temp, 1)))
    update_shed(charge, solar, out)
    event(now - 14 * 60 * minute, SHED, "CommLost", "Warning", "Connection lost",
          "Lost contact with Shed station: The EcoFlow cloud did not answer", ONLINE, 61)
    event(now - 14 * 60 * minute + 4 * minute, SHED, "CommRestored", "Info", "Connection restored",
          "Shed station is back", ONLINE, 61)
    event(now - 5 * 60 * minute, SHED, "SettingChanged", "Info", "Charge limit set to 90 %", "Changed from nutty.",
          ONLINE, 58, {"control": "chargeLimit", "value": 90, "result": "Applied"})
    event(now - 4 * 60 * minute - 40 * minute, SHED, "SettingChanged", "Info", "X-Boost set to On",
          "Changed from nutty.", ONLINE, 59, {"control": "xBoost", "value": 1, "result": "Applied"})

    db.executemany("INSERT INTO samples (device_id, ts, charge, runtime, load, watts_in, watts_out, input_voltage, "
                   "flags, watts_solar, temperature) VALUES (?,?,?,?,?,?,?,?,?,?,?)", samples)
    db.executemany("INSERT INTO episodes (device_id, device_name, start_ts, end_ts, start_charge, min_charge, "
                   "min_runtime, actions) VALUES (?,?,?,?,?,?,?,?)", episodes)
    db.executemany("INSERT INTO events (ts, device_id, device_name, type, severity, title, message, flags, charge, "
                   "runtime, load, extra) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", sorted(events))
    db.commit()
    db.close()
    return live


# --- rendering ---------------------------------------------------------------------

def afternoon_zone():
    """A fixed-offset zone where it's about 2:30 PM right now, so the sun,
    the charts and the clock in the shots agree whenever they're taken."""
    utc = time.gmtime()
    offset = round(14.5 - (utc.tm_hour + utc.tm_min / 60))
    offset = (offset + 12) % 24 - 12
    # Etc/GMT names count the other way round: Etc/GMT-3 is UTC+3.
    return "Etc/UTC" if offset == 0 else f"Etc/GMT{'-' if offset > 0 else '+'}{abs(offset)}"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def render(nutty, folder, out, scale, only):
    # Offscreen, without the desktop session: no keyring, no notifications,
    # no tray. English, so the shots match the README.
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "QT_SCALE_FACTOR")}
    env.update({
        "QT_QPA_PLATFORM": "offscreen",
        "QT_SCALE_FACTOR": str(scale),
        "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent",
        "XDG_CONFIG_HOME": str(folder / "xdg-config"),
        "XDG_DATA_HOME": str(folder / "xdg-data"),
        "XDG_CACHE_HOME": str(folder / "xdg-cache"),
        "XDG_RUNTIME_DIR": str(folder / "xdg-runtime"),
        "LANG": "en_US.UTF-8",
        "LC_ALL": "en_US.UTF-8",
        "LANGUAGE": "en",
        "TZ": os.environ["TZ"],
    })
    (folder / "xdg-runtime").mkdir(mode=0o700)
    out.mkdir(parents=True, exist_ok=True)
    failed = []
    for name, page, theme, device, size in SHOTS:
        if only and name not in only:
            continue
        target = out / f"{name}.png"
        cmd = [str(nutty), "--config-dir", str(folder), "--screenshot", str(target), "--page", page,
               f"--{theme}", "--delay", "6000"]
        if device:
            cmd += ["--device", device]
        if size:
            cmd += ["--size", size]
        result = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=90)
        noise = ("nutty.app: nutty", "does not support raise()", "propagateSizeHints")
        log = [l for l in result.stderr.splitlines() if l.strip() and not any(n in l for n in noise)]
        if result.returncode != 0 or not target.exists():
            failed.append(name)
        print(f"{name}: {'ok' if target.exists() else 'FAILED'}")
        for line in log:
            print(f"    {line}")
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--nutty", type=Path, default=ROOT / "build" / "release" / "src" / "app" / "nutty")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "screenshots")
    parser.add_argument("--scale", type=float, default=1.5, help="device pixel ratio (default 1.5)")
    parser.add_argument("--only", help="comma-separated shot names: " + ", ".join(s[0] for s in SHOTS))
    parser.add_argument("--keep", action="store_true", help="keep the demo folder and print where it is")
    args = parser.parse_args()
    if not args.nutty.exists():
        sys.exit(f"{args.nutty} not found; build nutty first or pass --nutty")
    only = set(args.only.split(",")) if args.only else None

    os.environ["TZ"] = afternoon_zone()
    time.tzset()
    nut_port, eco_port = free_port(), free_port()
    with tempfile.TemporaryDirectory(prefix="nutty-shots-") as tmp:
        folder = Path(tmp)
        write_config(folder, nut_port, eco_port)
        live = seed_history(folder)
        start_nut(nut_port, live)
        start_ecoflow(eco_port)
        failed = render(args.nutty, folder, args.out, args.scale, only)
        if args.keep:
            kept = Path(tempfile.mkdtemp(prefix="nutty-demo-"))
            for f in ("nutty.ini", "secrets.json", "history.db"):
                (kept / f).write_bytes((folder / f).read_bytes())
            print(f"demo config kept in {kept}")
    if failed:
        sys.exit("failed: " + ", ".join(failed))


if __name__ == "__main__":
    main()
