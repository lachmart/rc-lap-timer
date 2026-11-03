#!/usr/bin/env python3
"""Mock RC Lap Timer: serves web/ and fakes the ESP32 API + WebSocket, no board needed.

    python3 tools/mock_server.py            # simulated car drives laps
    python3 tools/mock_server.py --manual   # no simulation, trigger passes yourself:
                                            #   curl -X POST localhost:8080/mock/cross

Open http://localhost:8080 (or http://<this Mac's IP>:8080 on the phone).
The page connects the WebSocket to port 81 like on the ESP32. If port 81 can't be bound,
the mock uses 8081 and the page must be opened with ?ws=8081.

Lap logic mirrors src/laps.cpp and src/drivers.cpp; keep them in sync when changing behaviour.
Standard library only (Python 3.9+).
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
MAX_LAPS, MAX_DRIVERS, MAX_NAME = 200, 8, 32
LIVE_EVERY = 0.2
FAKE_NETWORKS = {                  # name -> (rssi dBm, open)
    "Lach Home": (-52, False),
    "Neighbour 2.4": (-74, False),
    "Cafe Free WiFi": (-83, True),
    "Very long network name for testing": (-66, False),
}


def now_us():
    return int(time.monotonic() * 1e6)


class Timer:
    """Same behaviour as laps.cpp + drivers.cpp + settings.cpp, guarded by one lock."""

    def __init__(self):
        self.lock = threading.RLock()
        self.min_lap, self.delta = 2000, 800
        self.ssid, self.password, self.sta, self.sta_err = "", "", "off", ""
        self.scan_until = 0          # monotonic time when the fake scan finishes, 0 = idle
        self.drivers = []          # [{"id", "name"}]
        self.next_id = 1
        self.driver = 0
        self.revision = 0
        self.raw, self.base, self.broken = 3000, 3000, False
        self.reset()

    def changed(self):
        self.revision += 1

    # ------------------------------------------------------------ laps.cpp
    def reset(self):
        with self.lock:
            self.last_cross = 0
            self.prev_cross = 0
            self.last_pass = None   # None | "start" | "lap"
            self.laps = []          # [ms, driver, deleted, end_us]
            self.ignored = 0
            self.stats = {}         # id -> {"count", "best", "total"}
            self.changed()

    def oldest_kept(self):
        return max(0, len(self.laps) - MAX_LAPS)

    def recompute_best(self, did):
        kept = self.laps[self.oldest_kept():]
        times = [l[0] for l in kept if not l[2] and l[1] == did]
        self.stats[did]["best"] = min(times) if times else 0

    def remove_from_stats(self, lap):
        st = self.stats.get(lap[1])
        if not st:
            return
        st["count"] -= 1
        st["total"] -= lap[0]
        if st["best"] == lap[0]:
            self.recompute_best(lap[1])

    def crossing(self, t=None):
        t = t or now_us()
        with self.lock:
            if self.last_cross == 0:
                self.last_cross, self.prev_cross, self.last_pass = t, 0, "start"
            elif t - self.last_cross >= self.min_lap * 1000:
                ms = (t - self.last_cross + 500) // 1000
                self.laps.append([ms, self.driver, False, t])
                st = self.stats.setdefault(self.driver, {"count": 0, "best": 0, "total": 0})
                st["count"] += 1
                st["total"] += ms
                if st["best"] == 0 or ms < st["best"]:
                    st["best"] = ms
                self.prev_cross, self.last_cross, self.last_pass = self.last_cross, t, "lap"
            else:
                self.ignored += 1
            self.changed()

    def set_driver(self, did):
        with self.lock:
            if did != self.driver:
                self.driver, self.last_cross, self.last_pass = did, 0, None
                self.changed()

    def undo(self):
        with self.lock:
            if self.last_pass == "start":
                self.last_cross = 0
            elif self.last_pass == "lap":
                lap = self.laps.pop()
                if not lap[2]:
                    self.remove_from_stats(lap)
                self.last_cross = self.prev_cross
            else:
                return False
            self.last_pass = None
            self.changed()
            return True

    def restart(self):
        with self.lock:
            self.last_cross, self.last_pass = 0, None
            self.changed()

    def delete(self, n):
        with self.lock:
            if not (self.oldest_kept() < n <= len(self.laps)) or self.laps[n - 1][2]:
                return False
            lap = self.laps[n - 1]
            lap[2] = True
            self.remove_from_stats(lap)
            if n == len(self.laps) and self.last_pass == "lap":
                self.last_pass = None
            self.changed()
            return True

    @staticmethod
    def continues(prev, nxt):
        return abs(nxt[3] - nxt[0] * 1000 - prev[3]) <= 1000

    def join(self, n):
        with self.lock:
            i = n - 1
            if not (self.oldest_kept() <= i < len(self.laps) - 1):
                return False
            a, b = self.laps[i], self.laps[i + 1]
            if a[2] or b[2] or a[1] != b[1] or not self.continues(a, b):
                return False
            a[2] = True
            self.remove_from_stats(a)
            del self.laps[i + 1]
            self.remove_from_stats(b)
            self.laps[i] = [a[0] + b[0], a[1], False, b[3]]
            st = self.stats.setdefault(a[1], {"count": 0, "best": 0, "total": 0})
            st["count"] += 1
            st["total"] += self.laps[i][0]
            if st["best"] == 0 or self.laps[i][0] < st["best"]:
                st["best"] = self.laps[i][0]
            if self.last_pass == "lap" and i == len(self.laps) - 1:
                self.prev_cross = a[3] - a[0] * 1000
            self.changed()
            return True

    def cur_ms(self):
        return (now_us() - self.last_cross) // 1000 if self.last_cross else 0

    # ------------------------------------------------------------ drivers.cpp
    def driver_add(self, name):
        name = "".join(c for c in name if ord(c) >= 0x20).strip()
        while len(name.encode()) > MAX_NAME:
            name = name[:-1]
        name = name.strip()
        with self.lock:
            if not name or len(self.drivers) >= MAX_DRIVERS or any(d["name"] == name for d in self.drivers):
                return False
            did = self.next_id
            self.next_id += 1
            self.drivers.append({"id": did, "name": name})
            if self.driver == 0:
                self.set_driver(did)
            self.changed()
            return True

    def driver_remove(self, did):
        with self.lock:
            if not any(d["id"] == did for d in self.drivers):
                return False
            self.drivers = [d for d in self.drivers if d["id"] != did]
            self.stats.pop(did, None)
            if self.driver == did:
                self.set_driver(self.drivers[0]["id"] if self.drivers else 0)
            self.changed()
            return True

    def driver_select(self, did):
        with self.lock:
            if did != 0 and not any(d["id"] == did for d in self.drivers):
                return False
            self.set_driver(did)
            return True

    # ------------------------------------------------------------ web.cpp
    def state(self):
        with self.lock:
            kept = self.laps[self.oldest_kept():]
            bests = [s["best"] for s in self.stats.values() if s["best"]]
            return {
                "t": "state", "running": self.last_cross != 0, "cur": self.cur_ms(),
                "driver": self.driver, "canUndo": self.last_pass is not None,
                "count": len(self.laps), "best": min(bests) if bests else 0, "ignored": self.ignored,
                "minLap": self.min_lap, "delta": self.delta,
                "ssid": self.ssid, "staIp": "192.168.1.50" if self.sta == "connected" else "", "sta": self.sta,
                "staErr": self.sta_err if self.sta == "waiting" else "", "hasPass": bool(self.password),
                "drivers": self.drivers,
                "stats": [{"id": k, **v} for k, v in self.stats.items()],
                "laps": [[l[0], l[1], 1 if l[2] else 0, 1 if i and self.continues(kept[i - 1], l) else 0]
                         for i, l in enumerate(kept)],
            }

    def live(self):
        rssi = FAKE_NETWORKS.get(self.ssid, (0, False))[0] + random.randint(-2, 2) if self.sta == "connected" else 0
        return {"t": "live", "raw": self.raw, "base": self.base, "broken": self.broken, "cur": self.cur_ms(), "rssi": rssi}

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
def simulate():
    """A car lapping the track. Each driver has their own pace; sometimes a false trigger."""
    next_cross = time.monotonic() + 3
    while True:
        t = time.monotonic()
        timer.raw = timer.base + random.randint(-25, 25)
        if t >= next_cross:
            timer.crossing()
            timer.broken, timer.raw = True, 600
            time.sleep(0.25)
            timer.broken = False
            name = next((d["name"] for d in timer.drivers if d["id"] == timer.driver), "")
            pace = 6.5 + (sum(map(ord, name)) % 30) / 10      # 6.5–9.5 s, stable per driver
            next_cross = time.monotonic() + random.gauss(pace, 0.4)
            if random.random() < 0.07:                         # someone walks through the beam
                threading.Timer(random.uniform(2.5, pace - 1), false_trigger).start()
        time.sleep(0.02)


def false_trigger():
    print("  (simulated false trigger)")
    timer.crossing()


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
        if timer.revision != sent:
            sent = timer.revision
            broadcast(timer.state())
        broadcast(timer.live())
        time.sleep(LIVE_EVERY)


# ---------------------------------------------------------------- HTTP
STATIC = {"/": ("index.html", "text/html"), "/style.css": ("style.css", "text/css"),
          "/app.js": ("app.js", "application/javascript")}


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
        if p == "/api/reset":
            timer.reset(); self.result(True)
        elif p == "/api/undo":
            self.result(timer.undo())
        elif p == "/api/restart":
            timer.restart(); self.result(True)
        elif p == "/api/lap/delete":
            self.result(timer.delete(num("n")))
        elif p == "/api/lap/join":
            self.result(timer.join(num("n")))
        elif p == "/api/driver/add":
            self.result(timer.driver_add(q.get("name", "")))
        elif p == "/api/driver/remove":
            self.result(timer.driver_remove(num("id")))
        elif p == "/api/driver/select":
            self.result(timer.driver_select(num("id")))
        elif p == "/api/calibrate":
            timer.base = timer.raw; self.result(True)
        elif p == "/api/config":
            timer.min_lap = min(max(num("minLap") or timer.min_lap, 300), 600000)
            timer.delta = min(max(num("delta") or timer.delta, 50), 4000)
            self.result(True)
        elif p == "/api/wifi":
            ssid = q.get("ssid", "").strip()
            keep = "pass" not in q and ssid == timer.ssid        # no pass argument = keep the saved one
            timer.wifi_apply(ssid, timer.password if keep else q.get("pass", ""))
            self.result(True)
        elif p == "/api/wifi/scan":
            self.result(timer.scan_start())
        elif p == "/mock/cross":
            timer.crossing(); self.result(True)
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
    ap.add_argument("--manual", action="store_true", help="no simulated car, use POST /mock/cross")
    ap.add_argument("--drivers", default="", help="comma-separated names to start with, e.g. Martin,Jana")
    args = ap.parse_args()

    for name in filter(None, (n.strip() for n in args.drivers.split(","))):
        timer.driver_add(name)

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
        threading.Thread(target=simulate, daemon=True).start()

    host_ip = lan_ip()
    suffix = "" if ws_port == 81 else f"/?ws={ws_port}"
    print(f"Mock RC Lap Timer: http://localhost:{args.port}{suffix}  (phone: http://{host_ip}:{args.port}{suffix})")
    print("Simulated car is driving." if not args.manual else f"Manual mode: curl -X POST localhost:{args.port}/mock/cross")
    ThreadingHTTPServer(("0.0.0.0", args.port), Http).serve_forever()


if __name__ == "__main__":
    main()
