#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC
"""A tiny NUT upsd stand-in for trying nutty without a real UPS.

Serves one or more simulated UPS units over the NUT network protocol
(LIST UPS, LIST VAR, GET VAR, USERNAME, PASSWORD, LOGIN, LOGOUT, VER).
STARTTLS is refused unless --cert/--key are given. While running, type a
command on stdin to change what the UPS reports:

    ol          on line power (charging back up)
    ob          on battery (charge and runtime drain)
    lb          on battery with the low-battery flag
    fsd         forced shutdown
    stale       answer ERR DATA-STALE
    charge N    set the battery charge to N percent
    runtime N   set the runtime to N seconds
    drain N     drain speed in percent per minute (default 6)
    quit

A command changes every UPS; add a UPS name at the end to change only that
one (for example "ob rack" or "charge 40 rack").

Example:
    scripts/fake-upsd.py --port 3493 --ups ups --ups rack
    # then add a NUT device in nutty for localhost:3493
"""

import argparse
import shlex
import socket
import ssl
import sys
import threading
import time


class Ups:
    def __init__(self, name):
        self.name = name
        self.status = "OL"
        self.charge = 100.0
        self.runtime = 1800.0
        self.drain = 6.0  # percent per minute while on battery
        self.stale = False
        self.logins = 0
        self.last = time.monotonic()

    def tick(self):
        now = time.monotonic()
        dt = now - self.last
        self.last = now
        if self.status.startswith("OB"):
            self.charge = max(0.0, self.charge - self.drain * dt / 60.0)
            self.runtime = max(0.0, self.runtime - dt)
            if self.charge <= 20 and "LB" not in self.status:
                self.status = "OB DISCHRG LB"
        elif self.status.startswith("OL"):
            self.charge = min(100.0, self.charge + 2.0 * dt / 60.0)
            self.runtime = min(1800.0, self.runtime + 20.0 * dt)
            self.status = "OL CHRG" if self.charge < 99.5 else "OL"

    def variables(self):
        self.tick()
        load = 23
        return {
            "battery.charge": f"{self.charge:.0f}",
            "battery.charge.low": "20",
            "battery.runtime": f"{self.runtime:.0f}",
            "battery.runtime.low": "120",
            "battery.voltage": f"{24.0 + self.charge / 50.0:.1f}",
            "device.mfr": "Example",
            "device.model": "Simulated UPS 1500",
            "device.serial": "SIM0000001",
            "device.type": "ups",
            "driver.name": "dummy-ups",
            "input.voltage": "0.0" if self.status.startswith("OB") else "121.0",
            "output.voltage": "120.0",
            "ups.load": str(load),
            "ups.realpower.nominal": "900",
            "ups.status": self.status,
            "ups.temperature": "29.5",
        }


def quote(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


class Server:
    def __init__(self, args):
        self.args = args
        self.units = {name: Ups(name) for name in (args.ups or ["ups"])}
        self.lock = threading.Lock()
        self.tls = None
        if args.cert and args.key:
            self.tls = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
            self.tls.load_cert_chain(args.cert, args.key)

    def serve(self):
        sock = socket.create_server((self.args.host, self.args.port), reuse_port=False)
        print(f"fake-upsd listening on {self.args.host}:{self.args.port}; units: {', '.join(self.units)}",
              file=sys.stderr)
        while True:
            conn, addr = sock.accept()
            threading.Thread(target=self.client, args=(conn, addr), daemon=True).start()

    def client(self, conn, addr):
        user = None
        password = None
        logged_in = None
        stream = conn
        buf = b""
        try:
            while True:
                data = stream.recv(4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip()
                    if not line:
                        continue
                    try:
                        words = shlex.split(line)
                    except ValueError:
                        stream.sendall(b"ERR INVALID-ARGUMENT\n")
                        continue
                    if self.args.verbose:
                        shown = line if not line.upper().startswith("PASSWORD") else "PASSWORD ***"
                        print(f"{addr[0]}: {shown}", file=sys.stderr)
                    cmd = words[0].upper() if words else ""
                    if cmd == "STARTTLS":
                        if not self.tls:
                            stream.sendall(b"ERR FEATURE-NOT-CONFIGURED\n")
                            continue
                        stream.sendall(b"OK STARTTLS\n")
                        stream = self.tls.wrap_socket(conn, server_side=True)
                        continue
                    if cmd == "LOGOUT":
                        stream.sendall(b"OK Goodbye\n")
                        return
                    reply = self.handle(words, user, password, logged_in)
                    if cmd == "USERNAME" and reply.startswith("OK") and len(words) > 1:
                        user = words[1]
                    elif cmd == "PASSWORD" and reply.startswith("OK") and len(words) > 1:
                        password = words[1]
                    elif cmd == "LOGIN" and reply.startswith("OK"):
                        logged_in = words[1]
                    stream.sendall(reply.encode())
        except (OSError, ssl.SSLError):
            pass
        finally:
            if logged_in:
                with self.lock:
                    self.units[logged_in].logins -= 1
            try:
                stream.close()
            except OSError:
                pass

    def handle(self, words, user, password, logged_in):
        cmd = [w.upper() for w in words[:2]]
        with self.lock:
            if not words:
                return "ERR UNKNOWN-COMMAND\n"
            if cmd[0] == "VER":
                return "Network UPS Tools upsd 2.8.1 - fake\n"
            if cmd[0] == "NETVER":
                return "1.3\n"
            if cmd[0] == "USERNAME":
                return "ERR ALREADY-SET-USERNAME\n" if user else "OK\n"
            if cmd[0] == "PASSWORD":
                if password:
                    return "ERR ALREADY-SET-PASSWORD\n"
                if self.args.password and (len(words) < 2 or words[1] != self.args.password):
                    return "ERR ACCESS-DENIED\n"
                return "OK\n"
            if cmd[0] == "LOGIN":
                if len(words) < 2 or words[1] not in self.units:
                    return "ERR UNKNOWN-UPS\n"
                if logged_in:
                    return "ERR ALREADY-LOGGED-IN\n"
                if self.args.password and not password:
                    return "ERR ACCESS-DENIED\n"
                self.units[words[1]].logins += 1
                return "OK\n"
            if cmd == ["LIST", "UPS"]:
                body = "".join(f'UPS {n} "Simulated UPS {n}"\n' for n in self.units)
                return f"BEGIN LIST UPS\n{body}END LIST UPS\n"
            if cmd == ["LIST", "VAR"] and len(words) >= 3:
                ups = self.units.get(words[2])
                if not ups:
                    return "ERR UNKNOWN-UPS\n"
                if ups.stale:
                    return "ERR DATA-STALE\n"
                body = "".join(f"VAR {ups.name} {k} {quote(v)}\n" for k, v in sorted(ups.variables().items()))
                body += f'VAR {ups.name} ups.numlogins "{ups.logins}"\n'
                return f"BEGIN LIST VAR {ups.name}\n{body}END LIST VAR {ups.name}\n"
            if cmd == ["GET", "VAR"] and len(words) >= 4:
                ups = self.units.get(words[2])
                if not ups:
                    return "ERR UNKNOWN-UPS\n"
                if ups.stale:
                    return "ERR DATA-STALE\n"
                value = ups.variables().get(words[3])
                if value is None:
                    return "ERR VAR-NOT-SUPPORTED\n"
                return f"VAR {ups.name} {words[3]} {quote(value)}\n"
            return "ERR UNKNOWN-COMMAND\n"

    def console(self):
        for line in sys.stdin:
            parts = line.split()
            if not parts:
                continue
            cmd, rest = parts[0].lower(), parts[1:]
            targets = list(self.units.values())
            if rest and rest[-1] in self.units:
                targets = [self.units[rest[-1]]]
                rest = rest[:-1]
            with self.lock:
                for ups in targets:
                    ups.tick()
                    if cmd == "ol":
                        ups.status, ups.stale = "OL CHRG", False
                    elif cmd == "ob":
                        ups.status, ups.stale = "OB DISCHRG", False
                    elif cmd == "lb":
                        ups.status, ups.stale = "OB DISCHRG LB", False
                    elif cmd == "fsd":
                        ups.status = "FSD OB DISCHRG LB"
                    elif cmd == "stale":
                        ups.stale = True
                    elif cmd == "charge" and rest:
                        ups.charge = float(rest[0])
                    elif cmd == "runtime" and rest:
                        ups.runtime = float(rest[0])
                    elif cmd == "drain" and rest:
                        ups.drain = float(rest[0])
            if cmd == "quit":
                raise SystemExit(0)
            print(", ".join(f"{u.name}: {u.status} {u.charge:.0f}% {u.runtime:.0f}s" for u in self.units.values()),
                  file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3493)
    parser.add_argument("--ups", action="append", help="UPS name to serve (repeatable, default: ups)")
    parser.add_argument("--password", help="password that PASSWORD must match; LOGIN needs it (as in upsd)")
    parser.add_argument("--cert", help="PEM certificate for STARTTLS")
    parser.add_argument("--key", help="PEM private key for STARTTLS")
    parser.add_argument("-v", "--verbose", action="store_true", help="log every command")
    args = parser.parse_args()

    server = Server(args)
    threading.Thread(target=server.serve, daemon=True).start()
    try:
        server.console()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
