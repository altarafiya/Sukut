"""Stage 6 test: the real window with the real host (Linux/PLAN.md section 7, 12).

  dbus-run-session -- python3 -I stage6_window_test.py <build-bin-dir> <evidence-dir>

Runs `sukut` (the GTK window) on GTK's headless Broadway display (GDK_BACKEND
and BROADWAY_DISPLAY come from the caller) with stand-ins for the panel's tray
watcher and the notification server (fake_desktop.py). Settings and state go
to ~/sukut-stage6. No mode is started, so no audio is rerouted.
  - the window starts sukut-host for the tray when none runs, and connects;
  - a second launch only raises the open window;
  - closing the window keeps Sukut in the tray with the notification;
  - a tray click opens the window again;
  - Exit from the tray closes the window too;
  - `sukut --tray` starts only the host; `sukut --restore-audio` works.
"""
import json
import os
import pathlib
import signal
import socket
import subprocess
import sys
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

BIN, EVIDENCE = (pathlib.Path(a) for a in sys.argv[1:3])
EVIDENCE.mkdir(parents=True, exist_ok=True)
HERE = pathlib.Path(__file__).resolve().parent
WORK = pathlib.Path(os.environ.get("HOME", "/tmp")) / "sukut-stage6"
WORK.mkdir(exist_ok=True)
SOCKET = pathlib.Path(os.environ["XDG_RUNTIME_DIR"]) / "sukut" / "host.socket"
ENV = dict(os.environ, XDG_CONFIG_HOME=str(WORK / "config"), XDG_STATE_HOME=str(WORK / "state"))
ENV.pop("XDG_ACTIVATION_TOKEN", None)
MENU, ITEM = "com.canonical.dbusmenu", "org.kde.StatusNotifierItem"
BUS = Gio.bus_get_sync(Gio.BusType.SESSION, None)
results, log_lines = [], []


def log(text):
    print(text, flush=True)
    log_lines.append(text)


def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": str(detail)})
    log(f"{name}={'PASS' if ok else 'FAIL'}{' ' + str(detail) if detail != '' else ''}")


def wait_until(predicate, seconds=10.0):
    end = time.time() + seconds
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.2)
    return predicate()


def pids(name):
    out = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True).stdout.split()
    return [int(p) for p in out]


def request(*fields, timeout=10):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(timeout)
        s.connect(str(SOCKET))
        s.send("\x1f".join(str(f) for f in fields).encode())
        return s.recv(1 << 20).decode()


def host_up():
    try:
        return request("read", timeout=2).startswith("1")
    except OSError:
        return False


class Fake:
    def __init__(self, kind):
        self.file = WORK / f"fake-{kind}.jsonl"
        self.proc = subprocess.Popen([sys.executable, "-I", str(HERE / "fake_desktop.py"), kind],
                                     stdout=open(self.file, "w"), stderr=subprocess.DEVNULL)
        wait_until(lambda: any("ready" in e for e in self.events()), 5)

    def events(self):
        try:
            return [json.loads(line) for line in self.file.read_text().splitlines() if line.strip()]
        except (OSError, json.JSONDecodeError):
            return []

    def calls(self, method):
        return [e for e in self.events() if e.get("call") == method]

    def stop(self):
        self.proc.terminate()
        self.proc.wait(timeout=5)


def call(dest, path, interface, method, params=None):
    reply = BUS.call_sync(dest, path, interface, method, params, None, Gio.DBusCallFlags.NONE, 8000, None)
    return reply.unpack() if reply is not None else None


def root_items(sni):
    _, root = call(sni, "/MenuBar", MENU, "GetLayout", GLib.Variant("(iias)", (0, -1, [])))
    return [child[0] for child in root[2]]


def window(*args):
    out = open(WORK / f"window-{len(list(WORK.glob('window-*.log')))}.log", "w")
    return subprocess.Popen([str(BIN / "sukut"), *args], stdout=out, stderr=subprocess.STDOUT, env=ENV)


def main():
    if host_up() or pids("sukut") or pids("sukut-host"):
        check("nothing_running_before", False, f"sukut={pids('sukut')} host={pids('sukut-host')}")
        return
    subprocess.run(["rm", "-rf", str(WORK / "config"), str(WORK / "state")])
    watcher, notifications = Fake("watcher"), Fake("notifications")
    try:
        first = window()
        connected = wait_until(host_up, 15)
        check("W_window_starts_host", connected and len(pids("sukut-host")) == 1, f"host={pids('sukut-host')}")
        registered = wait_until(lambda: watcher.calls("RegisterStatusNotifierItem"), 5)
        sni = registered[0]["service"] if registered else ""
        shrunk = wait_until(lambda: sni and root_items(sni) == [13, 14], 10)
        check("W_window_connected", bool(shrunk), "tray menu shows only Open and Exit while the window is open")
        check("W_window_running", first.poll() is None)

        second = window()
        try:
            code = second.wait(timeout=10)
        except subprocess.TimeoutExpired:
            code = None
        check("W_second_launch_raises", code == 0 and len(pids("sukut")) == 1, f"exit={code} windows={pids('sukut')}")

        first.send_signal(signal.SIGTERM)  # the window goes away without asking Sukut to exit
        first.wait(timeout=10)
        notice = wait_until(lambda: notifications.calls("Notify"), 8)
        full = wait_until(lambda: len(root_items(sni)) == 14, 5)
        check("W_close_to_tray", bool(notice) and bool(full) and host_up(), notice[0]["title"] if notice else "no notification")

        call(sni, "/StatusNotifierItem", ITEM, "Activate", GLib.Variant("(ii)", (0, 0)))
        reopened = wait_until(lambda: len(pids("sukut")) == 1 and root_items(sni) == [13, 14], 15)
        check("W_tray_click_opens_window", bool(reopened), f"windows={pids('sukut')}")

        call(sni, "/MenuBar", MENU, "Event", GLib.Variant("(isvu)", (14, "clicked", GLib.Variant("i", 0), 0)))
        gone = wait_until(lambda: not pids("sukut-host") and not pids("sukut"), 15)
        check("W_exit_closes_window", bool(gone), f"host={pids('sukut-host')} windows={pids('sukut')}")

        code = subprocess.run([str(BIN / "sukut"), "--tray"], env=ENV, timeout=15).returncode
        started = wait_until(host_up, 10)
        time.sleep(1)
        check("W_tray_flag_starts_host_only", code == 0 and started and not pids("sukut"), f"windows={pids('sukut')}")
        code = subprocess.run([str(BIN / "sukut"), "--tray"], env=ENV, timeout=15).returncode
        time.sleep(1)
        check("W_tray_flag_again_is_quiet", code == 0 and len(pids("sukut-host")) == 1 and not pids("sukut"))
        request("exit")
        wait_until(lambda: not pids("sukut-host"), 10)

        restore = subprocess.run([str(BIN / "sukut"), "--restore-audio"], env=ENV, capture_output=True, text=True, timeout=30)
        check("W_restore_audio", restore.returncode == 0, restore.stdout.strip() or restore.stderr.strip())
    finally:
        for name in ("sukut", "sukut-host"):
            for pid in pids(name):
                os.kill(pid, signal.SIGTERM)
        watcher.stop()
        notifications.stop()


try:
    main()
finally:
    passed = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage6-results.json").write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n")
    (EVIDENCE / "stage6-log.txt").write_text("\n".join(log_lines) + "\n")
    for path in sorted(WORK.glob("window-*.log")):
        (EVIDENCE / path.name).write_text(path.read_text())
        path.unlink()
