# Asks a board the "live" of ExpNet every two seconds, as an open page does, and
# writes what its router object says: who tells the traffic, the last values, what a dish adds.
#   live_watch.py ADDRESS MODULE SECONDS [PASSWORD]
import json
import sys
import time
import urllib.request

sys.stdout.reconfigure(encoding="utf-8")
base = "http://" + sys.argv[1]
module = sys.argv[2]
seconds = int(sys.argv[3])
password = sys.argv[4] if len(sys.argv) > 4 else "loopmaxap"


def call(method, path, body=None, token=""):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(req, timeout=8) as r:
        return json.loads(r.read().decode("utf-8") or "{}")


token = call("POST", "/api/auth/login", {"password": password}).get("token", "")
end = time.time() + seconds
last = None
while time.time() < end:
    try:
        a = call("POST", "/api/cmd/mod.%s/live" % module, {}, token)
        r = (a.get("result") or a).get("router", {})
        down, up = r.get("down") or [], r.get("up") or []
        line = "state %s" % r.get("state")
        for k in ("via", "ip", "manual", "needPort", "maxDown"):
            if k in r:
                line += ", %s %s" % (k, r[k])
        if down:
            line += ", samples %d, down %.1f Mbit/s, up %.1f" % (len(down), down[-1] / 1000, up[-1] / 1000)
        if "dish" in r:
            line += ", dish %s" % json.dumps(r["dish"])
        if line != last or down:
            print("%s  %s" % (time.strftime("%H:%M:%S"), line), flush=True)
        last = line
    except Exception as e:
        print("%s  no answer: %s" % (time.strftime("%H:%M:%S"), e), flush=True)
    time.sleep(2)
