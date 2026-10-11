# Fake devices that say the traffic of a house, on this PC, to try ExpNet on a real
# board without having them: a dish of Starlink (gRPC-Web, port 9201), a 4G router of Huawei
# (HiLink, port 80, it wants a session), a router with SNMP (UDP 161, or another port). The
# traffic swells and calms as in a house. Every request is written on the output.
#   fake_wan.py SECONDS [starlink] [huawei] [snmp[:PORT]]
import math
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.stdout.reconfigure(encoding="utf-8")
START = time.time()


def traffic():
    """bit/s down and up, now."""
    s = time.time() - START
    return 18e6 + 12e6 * math.sin(s / 6.0) + 3e6 * math.sin(s / 1.7), 2.5e6 + 1.5e6 * math.sin(s / 4.0)


def log(text):
    print("%7.1f  %s" % (time.time() - START, text), flush=True)


# ---------------------------------------------------------------- Starlink
def varint(v):
    out = b""
    while v >= 0x80:
        out += bytes([0x80 | (v & 0x7F)])
        v >>= 7
    return out + bytes([v])


def number(field, v):
    return varint(field << 3) + varint(v)


def real(field, v):
    return varint((field << 3) | 5) + struct.pack("<f", v)


def message(field, inner):
    return varint((field << 3) | 2) + varint(len(inner)) + inner


def frame(flags, body):
    return bytes([flags]) + struct.pack(">I", len(body)) + body


def dish_answer():
    down, up = traffic()
    s = time.time() - START
    status = (message(1, message(1, b"ut01000000-00000000-000fake0") + message(2, b"rev3_proto2") +
                      message(3, b"2026.09.30.mr12345")) +
              message(2, number(1, 86400 + int(s))) + real(1003, 0.004 + 0.004 * math.sin(s / 9.0)) +
              message(1004, real(1, 0.013)) + real(1007, down) + real(1008, up) +
              real(1009, 34.0 + 8.0 * math.sin(s / 3.0)) + real(1011, 12.5) + real(1012, 63.0) +
              message(1015, number(1, 1) + number(2, 14)))
    return frame(0, number(1, 1) + number(3, 9) + message(2004, status)) + frame(0x80, b"grpc-status:0\r\n")


class Dish(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        good = self.path == "/SpaceX.API.Device.Device/Handle" and body == bytes([0, 0, 0, 0, 3, 0xE2, 0x3E, 0])
        log("dish: POST %s, %s, x-grpc-web %s, body %s: %s" % (
            self.path, self.headers.get("Content-Type"), self.headers.get("X-Grpc-Web"), body.hex(),
            "answered" if good else "NOT the question of a status"))
        out = dish_answer() if good else frame(0x80, b"grpc-status:12\r\n")
        self.send_response(200)
        self.send_header("Content-Type", "application/grpc-web+proto")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def log_message(self, *args):
        pass


# ---------------------------------------------------------------- Huawei
class Huawei(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    session = "SessionID=fAkE5eSsIoNfAkE5eSsIoNfAkE5eSsIoNfAkE5eSsIoNfAkE5eSsIoNfAkE5eSsIoNfAkE5eSsIoN0123456789"

    def do_GET(self):
        cookie = self.headers.get("Cookie", "")
        if self.path == "/api/webserver/SesTokInfo":
            out = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><SesInfo>%s</SesInfo>"
                   "<TokInfo>fAkEt0k3n</TokInfo></response>" % self.session)
            log("huawei: a session asked")
        elif self.path == "/api/monitoring/traffic-statistics" and cookie == self.session:
            down, up = traffic()
            out = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><CurrentConnectTime>%d</CurrentConnectTime>"
                   "<CurrentUpload>1</CurrentUpload><CurrentDownload>2</CurrentDownload>"
                   "<CurrentDownloadRate>%d</CurrentDownloadRate><CurrentUploadRate>%d</CurrentUploadRate>"
                   "<TotalUpload>3</TotalUpload><TotalDownload>4</TotalDownload><TotalConnectTime>5</TotalConnectTime>"
                   "<showtraffic>1</showtraffic></response>" % (time.time() - START, down / 8, up / 8))
            log("huawei: the traffic, with the session")
        elif self.path == "/api/monitoring/traffic-statistics":
            out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><error><code>125002</code><message></message></error>"
            log("huawei: the traffic asked without a session: refused")
        else:
            out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><error><code>100002</code><message></message></error>"
            log("huawei: GET %s" % self.path)
        data = out.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/xml")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


# ---------------------------------------------------------------- SNMP
def piece(tag, body):
    if len(body) < 128:
        return bytes([tag, len(body)]) + body
    return bytes([tag, 0x82, len(body) >> 8, len(body) & 0xFF]) + body


def value(tag, v):
    body = b""
    while True:
        body = bytes([v & 0xFF]) + body
        v >>= 8
        if not v:
            break
    if body[0] & 0x80:
        body = b"\0" + body
    return piece(tag, body)


def name(ids):
    body = bytes([ids[0] * 40 + ids[1]])
    for one in ids[2:]:
        part = bytes([one & 0x7F])
        one >>= 7
        while one:
            part = bytes([0x80 | (one & 0x7F)]) + part
            one >>= 7
        body += part
    return piece(0x06, body)


def take(data, at):
    tag = data[at]
    n = data[at + 1]
    at += 2
    if n & 0x80:
        k = n & 0x7F
        n = int.from_bytes(data[at:at + k], "big")
        at += k
    return tag, data[at:at + n], at + n


def ids_of(body):
    ids = [body[0] // 40, body[0] % 40]
    v = 0
    for b in body[1:]:
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            ids.append(v)
            v = 0
    return tuple(ids)


class Agent:
    """A router whose port 7 goes to Internet: its routes say so in the second table."""
    PORT = 7

    def __init__(self):
        self.down = 5_000_000_000
        self.up = 100_000
        self.last = time.time()

    def objects(self):
        now = time.time()
        down, up = traffic()
        self.down += int(down / 8 * (now - self.last))
        self.up += int(up / 8 * (now - self.last))
        self.last = now
        p = self.PORT
        return {(1, 3, 6, 1, 2, 1, 4, 24, 4, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 64, 0, 1): (0x02, p),
                (1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 6, p): (0x46, self.down),
                (1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 10, p): (0x46, self.up),
                (1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15, p): (0x42, 100)}

    def answer(self, q):
        tag, msg, _ = take(q, 0)
        _, _version, at = take(msg, 0)
        _, community, at = take(msg, at)
        kind, pdu, _ = take(msg, at)
        _, rid, at = take(pdu, 0)
        _, _e, at = take(pdu, at)
        _, _i, at = take(pdu, at)
        _, binds, _ = take(pdu, at)
        if community != b"public":
            log("snmp: the word '%s' is not the one of this router: no answer" % community.decode("latin1"))
            return None
        objects = self.objects()
        out = b""
        asked = []
        at = 0
        while at < len(binds):
            _, bind, at = take(binds, at)
            _, oid, _ = take(bind, 0)
            ids = ids_of(oid)
            asked.append(".".join(str(i) for i in ids[6:]))
            if kind == 0xA1:
                later = sorted(k for k in objects if k > ids)
                found = later[0] if later else None
            else:
                found = ids if ids in objects else None
            out += piece(0x30, name(found) + value(*objects[found]) if found else
                         name(ids) + bytes([0x82 if kind == 0xA1 else 0x80, 0]))
        log("snmp: %s of %s" % ("the next" if kind == 0xA1 else "the value", ", ".join(asked)))
        return piece(0x30, value(0x02, 1) + piece(0x04, community) +
                     piece(0xA2, value(0x02, int.from_bytes(rid, "big")) + value(0x02, 0) + value(0x02, 0) +
                           piece(0x30, out)))


def snmp(port):
    agent = Agent()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", port))
    while True:
        q, who = s.recvfrom(1500)
        try:
            a = agent.answer(q)
        except Exception as e:
            log("snmp: not a question: %s" % e)
            continue
        if a:
            s.sendto(a, who)


seconds = int(sys.argv[1])
for what in sys.argv[2:]:
    if what == "starlink":
        server = ThreadingHTTPServer(("0.0.0.0", 9201), Dish)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        log("a dish of Starlink at port 9201")
    elif what == "huawei":
        server = ThreadingHTTPServer(("0.0.0.0", 80), Huawei)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        log("a router of Huawei at port 80")
    elif what.startswith("snmp"):
        port = int(what.split(":")[1]) if ":" in what else 161
        threading.Thread(target=snmp, args=(port,), daemon=True).start()
        log("a router with SNMP at UDP %d" % port)
time.sleep(seconds)
log("end")
