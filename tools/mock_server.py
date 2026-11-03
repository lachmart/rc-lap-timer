#!/usr/bin/env python3
"""Mock RC Lap Timer: serves web/ and fakes the ESP32 API + WebSocket, no board needed.

    python3 tools/mock_server.py                      # one simulated car, beam only
    python3 tools/mock_server.py --cars 5             # five transponder cars (multi-car mode)
    python3 tools/mock_server.py --manual             # no simulation, trigger passes yourself:
                                                      #   curl -X POST localhost:8080/mock/cross
                                                      #   curl -X POST 'localhost:8080/mock/cross?tx=6969'

Open http://localhost:8080 (or http://<this Mac's IP>:8080 on the phone).
The page connects the WebSocket to port 81 like on the ESP32. If port 81 can't be bound,
the mock uses 8081 and the page must be opened with ?ws=8081.

Logic mirrors src/laps.cpp, src/cars.cpp, src/drivers.cpp, src/timing.cpp and src/wlan.cpp;
keep them in sync when changing behaviour. Standard library only (Python 3.9+).
"""
import argparse
import base64
import hashlib
import json
import random
import socket
import socketserver
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

WEB = Path(__file__).resolve().parent.parent / "web"
MAX_LAPS, MAX_DRIVERS, MAX_CARS, MAX_NAME = 200, 8, 8, 32
STATS_CAR = 0x100
DEF_MIN_LAP, DEF_DELTA = 2000, 800
RACE_COUNTDOWN_MS = 4000
LIVE_EVERY = 0.2
FAKE_NETWORKS = {                  # name -> (rssi dBm, open)
    "Lach Home": (-52, False),
    "Neighbour 2.4": (-74, False),
    "Cafe Free WiFi": (-83, True),
    "Very long network name for testing": (-66, False),
}
SIM_TRANSPONDERS = [0x1B39, 0x2A10, 0x3C55, 465, 576, 0x4D01, 0x5E22]   # RiCino ids and NE556 blink rates


def now_us():
    return int(time.monotonic() * 1e6)


def suggest_delta(noise, peak):
    """Same as beamSuggestDelta() in src/beam.cpp."""
    if peak <= 0:
        return 0
    lo, hi = 2 * noise + 50, peak * 7 // 10
    if lo > hi:
        return -1
    s = ((lo + hi) // 2 + 25) // 50 * 50
    return min(max(s, 50), 4000)


def clean_name(name):
    name = "".join(c for c in name if ord(c) >= 0x20).strip()
    while len(name.encode()) > MAX_NAME:
        name = name[:-1]
    return name.strip()


class Car:
    def __init__(self, tx=0, name=""):
        self.tx, self.name, self.driver = tx, name, 0
        self.last_cross = self.prev_cross = 0
        self.last_pass = None              # None | "start" | "lap"
        self.num = 0


class Timer:
    """Same behaviour as the firmware, guarded by one lock."""

    def __init__(self):
        self.lock = threading.RLock()
        self.min_lap, self.delta = DEF_MIN_LAP, DEF_DELTA
        self.multi, self.zround = False, False
        self.race_laps, self.race_min = 10, 0
        self.race = {"state": "off", "go": 0, "laps": 0, "minutes": 0}
        self.ssid, self.password, self.sta, self.sta_err = "", "", "off", ""
        self.scan_until = 0
        self.drivers = []                  # [{"id", "name"}]
        self.next_id = 1
        self.cars = {0: Car()}             # slot -> Car; slot 0 = beam-only car
        self.revision = 0
        self.raw, self.base, self.broken = 3000, 3000, False
        self.noise, self.peak, self.blocked = 25, 0, False
        self.ir_id, self.ir_us, self.unident = 0, 0, 0
        self.laps = []                     # dicts: ms num car driver deleted precise end
        self.ignored = 0
        self.forgotten = set()             # removed drivers, their stats are dropped (lapsForgetDriver)

    def changed(self):
        self.revision += 1

    # ------------------------------------------------------------ laps.cpp
    def reset(self):
        with self.lock:
            for c in self.cars.values():
                c.last_cross = c.prev_cross = 0
                c.last_pass, c.num = None, 0
            self.laps, self.ignored, self.unident = [], 0, 0
            self.forgotten = set()
            self.changed()

    def oldest_kept(self):
        """Index of the oldest lap still in the firmware's ring."""
        return max(0, len(self.laps) - MAX_LAPS)

    def kept(self):
        return self.laps[self.oldest_kept():]

    def crossing(self, slot, t, precise=True):
        with self.lock:
            c = self.cars.get(slot)
            if c is None:
                return
            if c.last_cross == 0:
                c.last_cross, c.prev_cross, c.last_pass = t, 0, "start"
            elif t - c.last_cross >= self.min_lap * 1000:
                c.num += 1
                self.laps.append({"ms": (t - c.last_cross + 500) // 1000, "num": c.num, "car": slot,
                                  "driver": c.driver, "deleted": False, "precise": precise, "end": t})
                c.prev_cross, c.last_cross, c.last_pass = c.last_cross, t, "lap"
            else:
                self.ignored += 1
            self.changed()

    def set_driver(self, slot, did):
        c = self.cars[slot]
        if did != c.driver:
            c.driver, c.last_cross, c.last_pass = did, 0, None
            self.changed()

    def last_lap_index(self, slot):
        for i in range(len(self.laps) - 1, -1, -1):
            if self.laps[i]["car"] == slot:
                return i
        return -1

    def undo(self, slot):
        with self.lock:
            c = self.cars.get(slot)
            if c is None:
                return False
            if c.last_pass == "start":
                c.last_cross = 0
            elif c.last_pass == "lap" and self.last_lap_index(slot) >= 0:
                del self.laps[self.last_lap_index(slot)]
                c.num -= 1
                c.last_cross = c.prev_cross
            else:
                return False
            c.last_pass = None
            self.changed()
            return True

    def restart(self, slot):
        with self.lock:
            if slot in self.cars:
                self.cars[slot].last_cross, self.cars[slot].last_pass = 0, None
                self.changed()

    def delete(self, seq):
        with self.lock:
            i = seq - 1
            if not (self.oldest_kept() <= i < len(self.laps)) or self.laps[i]["deleted"]:
                return False
            lap = self.laps[i]
            lap["deleted"] = True
            c = self.cars.get(lap["car"])
            if c and i == self.last_lap_index(lap["car"]) and c.last_pass == "lap":
                c.last_pass = None
            self.changed()
            return True

    @staticmethod
    def continues(prev, nxt):
        return abs(nxt["end"] - nxt["ms"] * 1000 - prev["end"]) <= 1000

    def join(self, seq):
        with self.lock:
            i = seq - 1
            if not (self.oldest_kept() <= i < len(self.laps)):
                return False
            a = self.laps[i]
            j = next((k for k in range(i + 1, len(self.laps)) if self.laps[k]["car"] == a["car"]), -1)
            if j < 0:
                return False
            b = self.laps[j]
            if a["deleted"] or b["deleted"] or a["driver"] != b["driver"] or not self.continues(a, b):
                return False
            c = self.cars[a["car"]]
            b_was_last = j == self.last_lap_index(a["car"])
            del self.laps[j]
            for lap in self.laps[j:]:
                if lap["car"] == a["car"]:
                    lap["num"] -= 1
            c.num -= 1
            a.update(ms=a["ms"] + b["ms"], end=b["end"], precise=a["precise"] and b["precise"])
            if b_was_last and c.last_pass == "lap":
                c.prev_cross = a["end"] - a["ms"] * 1000
            self.changed()
            return True

    def stats(self):
        by = {}
        for lap in self.kept():
            if lap["deleted"]:
                continue
            key = lap["driver"] or (STATS_CAR | lap["car"])
            if key in self.forgotten:
                continue
            s = by.setdefault(key, {"key": key, "count": 0, "best": 0, "total": 0})
            s["count"] += 1
            s["total"] += lap["ms"]
            if s["best"] == 0 or lap["ms"] < s["best"]:
                s["best"] = lap["ms"]
        return list(by.values())

    def cur_ms(self, slot):
        c = self.cars.get(slot)
        return (now_us() - c.last_cross) // 1000 if c and c.last_cross else 0

    # ------------------------------------------------------------ drivers.cpp, cars.cpp
    def driver_add(self, name):
        name = clean_name(name)
        with self.lock:
            if not name or len(self.drivers) >= MAX_DRIVERS or any(d["name"] == name for d in self.drivers):
                return False
            did = self.next_id
            self.next_id += 1
            self.drivers.append({"id": did, "name": name})
            if self.cars[0].driver == 0:
                self.set_driver(0, did)
            self.changed()
            return True

    def driver_remove(self, did):
        with self.lock:
            if not any(d["id"] == did for d in self.drivers):
                return False
            self.drivers = [d for d in self.drivers if d["id"] != did]
            self.forgotten.add(did)
            for slot, c in self.cars.items():
                if c.driver == did:
                    self.set_driver(slot, 0)
            if self.cars[0].driver == 0:
                busy = {c.driver for s, c in self.cars.items() if s}
                free = [d["id"] for d in self.drivers if d["id"] not in busy]
                if free:
                    self.set_driver(0, free[0])
            self.changed()
            return True

    def car_set_driver(self, slot, did):
        with self.lock:
            if slot not in self.cars or (did and not any(d["id"] == did for d in self.drivers)):
                return False
            if did:
                for s, c in self.cars.items():
                    if s != slot and c.driver == did:
                        self.set_driver(s, 0)
            self.set_driver(slot, did)
            self.changed()
            return True

    def slot_for(self, tx):
        with self.lock:
            for s, c in self.cars.items():
                if s and c.tx == tx:
                    return s
            free = [s for s in range(1, MAX_CARS) if s not in self.cars]
            if not free:
                return -1
            self.cars[free[0]] = Car(tx)
            self.changed()
            return free[0]

    def car_rename(self, slot, name):
        with self.lock:
            if slot not in self.cars:
                return False
            self.cars[slot].name = clean_name(name)
            self.changed()
            return True

    def car_remove(self, slot):
        with self.lock:
            if slot <= 0 or slot not in self.cars:
                return False
            del self.cars[slot]
            self.changed()
            return True

    # ------------------------------------------------------------ timing.cpp
    def passing(self, tx, t, precise=True):
        """A finished passing from the gate (tx 0 = beam only)."""
        with self.lock:
            race = self.race
            if race["state"] == "countdown" or (race["state"] == "running" and t // 1000 < race["go"]):
                return
            if tx:
                self.ir_id, self.ir_us = tx, t
            slot = self.slot_for(tx) if tx else (-1 if self.multi else 0)
            if slot >= 0:
                self.crossing(slot, t, precise)

    def race_start(self, laps, minutes):
        with self.lock:
            self.race_laps, self.race_min = laps, minutes
            self.reset()
            self.race = {"state": "countdown", "go": now_us() // 1000 + RACE_COUNTDOWN_MS,
                         "laps": laps, "minutes": minutes}
            self.changed()

    def race_tick(self):
        with self.lock:
            if self.race["state"] == "countdown" and now_us() // 1000 >= self.race["go"]:
                self.race["state"] = "running"
                for slot in self.cars:
                    self.restart(slot)

    # ------------------------------------------------------------ web.cpp
    def state(self):
        with self.lock:
            kept = self.kept()
            stats = self.stats()
            bests = [s["best"] for s in stats if s["best"]]
            prev_of = {}
            laps = []
            for lap in kept:
                p = prev_of.get(lap["car"])
                prev_of[lap["car"]] = lap
                laps.append([lap["ms"], lap["driver"], int(lap["deleted"]), int(bool(p) and self.continues(p, lap)),
                             lap["car"], lap["num"], int(lap["precise"]), lap["end"] // 1000])
            return {
                "t": "state", "now": now_us() // 1000, "multi": self.multi,
                "race": dict(self.race, setLaps=self.race_laps, setMin=self.race_min),
                "count": len(self.laps), "best": min(bests) if bests else 0,
                "ignored": self.ignored, "unident": self.unident,
                "minLap": self.min_lap, "delta": self.delta,
                "zround": {"on": self.zround, "connected": False, "racing": False},
                "ssid": self.ssid, "staIp": "192.168.1.50" if self.sta == "connected" else "", "sta": self.sta,
                "staErr": self.sta_err if self.sta == "waiting" else "", "hasPass": bool(self.password),
                "drivers": self.drivers,
                "cars": [{"slot": s, "tx": c.tx, "name": c.name, "driver": c.driver, "running": c.last_cross != 0,
                          "cur": self.cur_ms(s), "canUndo": c.last_pass is not None, "laps": c.num}
                         for s, c in sorted(self.cars.items())],
                "stats": stats,
                "laps": laps,
            }

    def live(self):
        rssi = FAKE_NETWORKS.get(self.ssid, (0, False))[0] + random.randint(-2, 2) if self.sta == "connected" else 0
        broken = self.broken or self.blocked
        return {"t": "live", "raw": 600 if self.blocked else self.raw, "base": self.base, "broken": broken,
                "cur": [self.cur_ms(s) for s in range(MAX_CARS)],
                "noise": self.noise, "peak": self.peak, "suggest": suggest_delta(self.noise, self.peak),
                "irId": self.ir_id, "irAgo": (now_us() - self.ir_us) // 1000 if self.ir_us else -1,
                "unident": self.unident, "rssi": rssi}

    # ------------------------------------------------------------ wlan.cpp
    def wifi_apply(self, ssid, password):
        """Fake join: known network + password 'wrong' fails, unknown name isn't found."""
        self.ssid, self.password, self.sta_err = ssid, password, ""
        self.sta = "connecting" if ssid else "off"
        self.changed()
        if ssid:
            threading.Timer(2.5, self.wifi_result).start()

    def wifi_result(self):
        net = FAKE_NETWORKS.get(self.ssid)
        if net is None:
            self.sta, self.sta_err = "waiting", "network not found"
        elif not net[1] and self.password == "wrong":
            self.sta, self.sta_err = "waiting", "wrong password"
        else:
            self.sta = "connected"
        self.changed()

    def scan_start(self):
        if self.sta == "connecting" or self.scan_until:
            return False
        self.scan_until = time.monotonic() + 2
        return True

    def scan_results(self):
        if self.scan_until and time.monotonic() < self.scan_until:
            return {"scanning": True, "networks": []}
        self.scan_until = 0
        nets = sorted(FAKE_NETWORKS.items(), key=lambda kv: -kv[1][0])
        return {"scanning": False, "networks": [{"ssid": k, "rssi": v[0], "open": v[1]} for k, v in nets]}


timer = Timer()


# ---------------------------------------------------------------- simulation
def simulate(n_cars):
    """Cars lapping the track, each at its own pace. Sometimes a false trigger or an IR-only pass."""
    txs = SIM_TRANSPONDERS[:n_cars] if n_cars > 1 else [0]
    rng = random.Random(7)
    pace = {tx: 6.5 + rng.random() * 2.5 for tx in txs}
    next_cross = {tx: time.monotonic() + 2 + rng.random() * 3 for tx in txs}
    while True:
        t = time.monotonic()
        timer.race_tick()
        timer.raw = timer.base + random.randint(-timer.noise, timer.noise)
        for tx in txs:
            if t < next_cross[tx] or timer.blocked:
                continue
            imprecise = n_cars > 1 and random.random() < 0.08    # hidden behind another car at the beam
            timer.passing(tx, now_us(), precise=not imprecise)
            timer.peak = random.randint(2300, 2500)
            next_cross[tx] = t + max(3, random.gauss(pace[tx], 0.35))
            if random.random() < 0.05:                            # someone walks through the beam
                threading.Timer(random.uniform(2, 4), false_trigger, args=(tx,)).start()
        time.sleep(0.02)


def false_trigger(tx):
    if timer.multi:
        timer.unident += 1                                         # no transponder near the gate
        timer.changed()
    else:
        print("  (simulated false trigger)")
        timer.passing(0, now_us())


# ---------------------------------------------------------------- WebSocket (minimal, text frames only)
clients, clients_lock = set(), threading.Lock()


def ws_frame(text):
    data = text.encode()
    n = len(data)
    if n < 126:
        header = bytes([0x81, n])
    elif n < 65536:
        header = bytes([0x81, 126]) + n.to_bytes(2, "big")
    else:
        header = bytes([0x81, 127]) + n.to_bytes(8, "big")
    return header + data


def ws_send(sock, obj):
    try:
        sock.sendall(ws_frame(json.dumps(obj, separators=(",", ":"))))
        return True
    except OSError:
        return False


def broadcast(obj):
    with clients_lock:
        dead = [c for c in clients if not ws_send(c, obj)]
        for c in dead:
            clients.discard(c)


class WsHandler(socketserver.BaseRequestHandler):
    def handle(self):
        req = b""
        while b"\r\n\r\n" not in req:
            chunk = self.request.recv(1024)
            if not chunk:
                return
            req += chunk
        headers = dict(
            line.split(": ", 1) for line in req.decode(errors="ignore").split("\r\n")[1:] if ": " in line
        )
        key = next((v for k, v in headers.items() if k.lower() == "sec-websocket-key"), "")
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        self.request.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n"
        )
        ws_send(self.request, timer.state())
        with clients_lock:
            clients.add(self.request)
        try:
            while True:                     # we don't use client messages, just wait for close
                head = self.request.recv(2)
                if not head or head[0] & 0x0F == 8:
                    break
                self.request.recv(65536)
        except OSError:
            pass
        finally:
            with clients_lock:
                clients.discard(self.request)


class WsServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def pusher():
    sent = -1
    while True:
        timer.race_tick()
        if timer.revision != sent:
            sent = timer.revision
            broadcast(timer.state())
        broadcast(timer.live())
        time.sleep(LIVE_EVERY)


# ---------------------------------------------------------------- HTTP
STATIC = {"/": ("index.html", "text/html"), "/style.css": ("style.css", "text/css"),
          "/app.js": ("app.js", "application/javascript"), "/race.js": ("race.js", "application/javascript")}


class Http(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        if self.command == "POST":
            print("  " + self.requestline)

    def send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def result(self, ok):
        timer.changed()
        self.send(200 if ok else 400, '{"ok":%s}' % ("true" if ok else "false"))

    def do_GET(self):
        path = urlparse(self.path).path
        if path in STATIC:
            name, ctype = STATIC[path]
            self.send(200, (WEB / name).read_bytes(), ctype)   # read every time: edit and reload
        elif path == "/api/state":
            self.send(200, json.dumps(timer.state()))
        elif path == "/api/wifi/scan":
            self.send(200, json.dumps(timer.scan_results()))
        else:
            self.send(404, "Not found", "text/plain")

    def do_POST(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query, keep_blank_values=True).items()}
        num = lambda k: int(q.get(k) or 0)
        p = url.path
        routes = {
            "/api/reset":         lambda: (timer.reset(), timer.race.update(state="off"), True)[-1],
            "/api/undo":          lambda: timer.undo(num("car")),
            "/api/restart":       lambda: (timer.restart(num("car")), True)[-1],
            "/api/lap/delete":    lambda: timer.delete(num("n")),
            "/api/lap/join":      lambda: timer.join(num("n")),
            "/api/driver/add":    lambda: timer.driver_add(q.get("name", "")),
            "/api/driver/remove": lambda: timer.driver_remove(num("id")),
            "/api/driver/select": lambda: timer.car_set_driver(num("car"), num("id")),
            "/api/car/rename":    lambda: timer.car_rename(num("slot"), q.get("name", "")),
            "/api/car/remove":    lambda: timer.car_remove(num("slot")),
            "/api/mode":          lambda: (setattr(timer, "multi", num("multi") != 0), True)[-1],
            "/api/usb":           lambda: (setattr(timer, "zround", num("zround") != 0), True)[-1],
            "/api/race/start":    lambda: (timer.race_start(min(max(num("laps"), 0), 999),
                                                            min(max(num("minutes"), 0), 600)), True)[-1],
            "/api/race/stop":     lambda: (timer.race.update(state="off"), True)[-1],
            "/api/calibrate":     lambda: (setattr(timer, "base", timer.raw), True)[-1],
            "/api/wifi/scan":     lambda: timer.scan_start(),
        }
        if p in routes:
            self.result(routes[p]())
        elif p == "/api/config":
            timer.min_lap = min(max(num("minLap") if "minLap" in q else timer.min_lap, 300), 600000)
            timer.delta = min(max(num("delta") if "delta" in q else timer.delta, 50), 4000)
            self.result(True)
        elif p == "/api/wifi":
            ssid = q.get("ssid", "").strip()
            keep = "pass" not in q and ssid == timer.ssid        # no pass argument = keep the saved one
            timer.wifi_apply(ssid, timer.password if keep else q.get("pass", ""))
            self.result(True)
        elif p == "/mock/cross":                                 # ?tx=6969 (decimal) for a transponder car
            timer.passing(num("tx"), now_us())
            timer.peak = 2400
            self.result(True)
        elif p == "/mock/block":                                 # hold the beam broken: ?on=1 / ?on=0
            timer.blocked = q.get("on") == "1"
            self.result(True)
        else:
            self.send(404, "Not found", "text/plain")


def lan_ip():
    """This machine's address on the local network (no traffic is sent, UDP connect only picks a route)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.168.0.1", 9))
            return s.getsockname()[0]
    except OSError:
        return "localhost"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080, help="HTTP port (default 8080)")
    ap.add_argument("--cars", type=int, default=1, help="simulated cars; more than 1 = transponder cars (max 7)")
    ap.add_argument("--manual", action="store_true", help="no simulated cars, use POST /mock/cross")
    ap.add_argument("--drivers", default="", help="comma-separated names to start with, e.g. Martin,Jana")
    args = ap.parse_args()

    for name in filter(None, (n.strip() for n in args.drivers.split(","))):
        timer.driver_add(name)
    timer.multi = args.cars > 1

    ws_port = None
    for port in (81, 8081):
        try:
            ws = WsServer(("0.0.0.0", port), WsHandler)
            ws_port = port
            break
        except OSError:
            continue
    if ws_port is None:
        raise SystemExit("Can't bind a WebSocket port (81 or 8081)")
    threading.Thread(target=ws.serve_forever, daemon=True).start()
    threading.Thread(target=pusher, daemon=True).start()
    if not args.manual:
        threading.Thread(target=simulate, args=(min(args.cars, 7),), daemon=True).start()

    suffix = "" if ws_port == 81 else f"/?ws={ws_port}"
    print(f"Mock RC Lap Timer: http://localhost:{args.port}{suffix}  (phone: http://{lan_ip()}:{args.port}{suffix})")
    print(f"{args.cars} simulated car(s)." if not args.manual else f"Manual mode: curl -X POST localhost:{args.port}/mock/cross")
    ThreadingHTTPServer(("0.0.0.0", args.port), Http).serve_forever()


if __name__ == "__main__":
    main()
