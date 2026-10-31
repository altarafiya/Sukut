"""Stage 8: the installed host runs the installed sealed engine.

  dbus-run-session -- python3 -I stage8_engine_check.py /usr/libexec/sukut/sukut-host <work-dir>

Starts the installed sukut-host for the tray (no window) with throwaway settings,
turns Music Filter on for all system audio, checks the engine runs, turns it off
and exits. Prints one PASS/FAIL line per check.
"""
import json
import os
import pathlib
import socket
import subprocess
import sys
import time

HOST, WORK = sys.argv[1], pathlib.Path(sys.argv[2])
WORK.mkdir(parents=True, exist_ok=True)
SOCKET = pathlib.Path(os.environ["XDG_RUNTIME_DIR"]) / "sukut" / "host.socket"
ENV = dict(os.environ, XDG_CONFIG_HOME=str(WORK / "config"), XDG_STATE_HOME=str(WORK / "state"))
ok = True


def check(name, passed, detail=""):
    global ok
    ok = ok and passed
    print(f"{name}={'PASS' if passed else 'FAIL'} {detail}".rstrip(), flush=True)


def request(*fields):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(20)
        s.connect(str(SOCKET))
        s.send("\x1f".join(str(f) for f in fields).encode())
        return s.recv(1 << 20).decode()


def node_id(name):
    dump = json.loads(subprocess.run(["pw-dump"], capture_output=True, text=True).stdout or "[]")
    return next((o["id"] for o in dump if o.get("type") == "PipeWire:Interface:Node"
                 and o.get("info", {}).get("props", {}).get("node.name") == name), None)


# A temporary virtual output, as in the earlier stage tests: WSL has no speakers.
subprocess.run(["pw-cli", "create-node", "adapter",
                "{ factory.name=support.null-audio-sink node.name=test_speakers node.description=test_speakers "
                "media.class=Audio/Sink audio.position=[ FL FR ] audio.rate=48000 object.linger=true }"],
               capture_output=True)
time.sleep(1)
log = open(WORK / "host.log", "w")
host = subprocess.Popen([HOST, "--tray", "--window-command", "true"], stderr=log, env=ENV)
end = time.time() + 15
while time.time() < end and "ready" not in (WORK / "host.log").read_text():
    time.sleep(0.2)
check("I_host_starts", "ready" in (WORK / "host.log").read_text(), (WORK / "host.log").read_text().splitlines()[0] if (WORK / "host.log").read_text() else "")
reply = request("select", 1, "", 0, 1, 0)
state = {}
for _ in range(30):
    state = json.loads(request("read")[1:])
    if state["mode"] == 1 and state["state"] == 1 and not state["busy"]:
        break
    time.sleep(0.3)
check("I_engine_runs_installed_model", reply == "1" and state.get("mode") == 1 and state.get("state") == 1,
      f"reply={reply!r} mode={state.get('mode')} state={state.get('state')}")
check("I_engine_off", request("select", 0, "", 0, 1, 0) == "1")
request("exit")
try:
    code = host.wait(timeout=15)
except subprocess.TimeoutExpired:
    host.kill()
    code = None
check("I_host_exits", code == 0, f"exit={code}")
if (node := node_id("test_speakers")) is not None:
    subprocess.run(["pw-cli", "destroy", str(node)], capture_output=True)
sys.exit(0 if ok else 1)
