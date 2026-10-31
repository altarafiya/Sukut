"""Stage 5 test: the host (Linux/PLAN.md section 6, 12).

  dbus-run-session -- python3 -I stage5_host_test.py <build-bin-dir> <engine-dir> <icon-dir> <evidence-dir>

Runs sukut-host on a private session bus with stand-in desktop services
(fake_desktop.py: a StatusNotifierWatcher and a notification server) and this
session's PipeWire with one virtual output. A fake window command records each
"open". It drives the host the way a panel does (D-Bus: SNI properties,
DBusMenu GetLayout/AboutToShow/Event) and the way the window will (the socket).
  A. No tray: --tray opens the window after 5 s; closing the window exits Sukut.
  B. Tray: registration, menu, On/modes/buffering/source app from the menu,
     Standby, the closed-app rule, Arabic, window open/close (notification),
     second launch, autostart, tray restart, RealtimeKit, Exit from the menu.
  C. SIGTERM restores audio.
  D. A killed host: the next start restores audio; exit from the window.
"""
import json
import math
import os
import pathlib
import re
import signal
import socket
import stat
import subprocess
import sys
import time
import wave

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

BIN, ENGINE, ICONS, EVIDENCE = (pathlib.Path(a) for a in sys.argv[1:5])
EVIDENCE.mkdir(parents=True, exist_ok=True)
HERE = pathlib.Path(__file__).resolve().parent
WORK = pathlib.Path(os.environ.get("HOME", "/tmp")) / "sukut-stage5"
WORK.mkdir(exist_ok=True)
CONFIG, STATE = WORK / "config", WORK / "state"
RUNTIME = pathlib.Path(os.environ["XDG_RUNTIME_DIR"]) / "sukut"
SOCKET = RUNTIME / "host.socket"
SETTINGS = CONFIG / "sukut/settings.json"
JOURNAL = STATE / "sukut/routing.json"
AUTOSTART = CONFIG / "autostart/io.github.altarafiya.Sukut.desktop"
OPENS = WORK / "window-opens.txt"
APP_ID = "io.github.altarafiya.Sukut"
MENU, ITEM = "com.canonical.dbusmenu", "org.kde.StatusNotifierItem"
BUS = Gio.bus_get_sync(Gio.BusType.SESSION, None)
ENV = dict(os.environ, XDG_CONFIG_HOME=str(CONFIG), XDG_STATE_HOME=str(STATE))
ENV.pop("XDG_ACTIVATION_TOKEN", None)
results, log_lines, measurements = [], [], {}


def log(text):
    print(text, flush=True)
    log_lines.append(text)


def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": str(detail)})
    log(f"{name}={'PASS' if ok else 'FAIL'}{' ' + str(detail) if detail != '' else ''}")


def run(*args, timeout=60, env=None):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=timeout, env=env)


def wait_until(predicate, seconds=5.0):
    end = time.time() + seconds
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.2)
    return predicate()


# ----- PipeWire --------------------------------------------------------------
def dump():
    return json.loads(run("pw-dump").stdout or "[]")


def node_id(name):
    return next((o["id"] for o in dump() if o.get("type") == "PipeWire:Interface:Node"
                 and o.get("info", {}).get("props", {}).get("node.name") == name), None)


def default_sink():
    match = re.search(r"value:'(.*?)' type", run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink").stdout)
    try:
        return json.loads(match.group(1)).get("name", "") if match else ""
    except json.JSONDecodeError:
        return ""


def set_default(name):
    run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink", json.dumps({"name": name}), "Spa:String:JSON")
    time.sleep(0.6)


def make_output(name, priority):
    if node_id(name) is None:
        run("pw-cli", "create-node", "adapter",
            "{ factory.name=support.null-audio-sink node.name=%s node.description=%s media.class=Audio/Sink "
            "audio.position=[ FL FR ] audio.rate=48000 priority.session=%d object.linger=true }" % (name, name, priority))
        wait_until(lambda: node_id(name) is not None)


def remove_output(name):
    if (node := node_id(name)) is not None:
        run("pw-cli", "destroy", node)
        wait_until(lambda: node_id(name) is None)


def tone(path, freq, seconds):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
        frames = bytearray()
        for i in range(int(seconds * 48000)):
            v = int(6000 * math.sin(2 * math.pi * freq * i / 48000)).to_bytes(2, "little", signed=True)
            frames += v + v
        w.writeframes(bytes(frames))


# ----- desktop stand-ins -------------------------------------------------------
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
        self.proc.terminate(); self.proc.wait(timeout=5)


def call(dest, path, interface, method, params=None, timeout=8000):
    reply = BUS.call_sync(dest, path, interface, method, params, None, Gio.DBusCallFlags.NONE, timeout, None)
    return reply.unpack() if reply is not None else None


def prop(dest, path, interface, name):
    return call(dest, path, "org.freedesktop.DBus.Properties", "Get", GLib.Variant("(ss)", (interface, name)))[0]


def menu(dest):
    _, root = call(dest, "/MenuBar", MENU, "GetLayout", GLib.Variant("(iias)", (0, -1, [])))
    items = {}

    def walk(node):
        ident, props, children = node
        items[ident] = dict(props, children=[c[0] for c in children])
        for child in children:
            walk(child)
    walk(root)
    return items


def click(dest, ident):
    call(dest, "/MenuBar", MENU, "Event", GLib.Variant("(isvu)", (ident, "clicked", GLib.Variant("i", 0), 0)))


def about_to_show(dest, ident):
    return call(dest, "/MenuBar", MENU, "AboutToShow", GLib.Variant("(i)", (ident,)))[0]


# ----- the host --------------------------------------------------------------
def request(*fields, timeout=20):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(timeout)
        s.connect(str(SOCKET))
        s.send("\x1f".join(str(f) for f in fields).encode())
        return s.recv(1 << 20).decode()


def read():
    reply = request("read")
    return json.loads(reply[1:]) if reply.startswith("1") else {}


def wait_state(predicate, seconds=20):
    return wait_until(lambda: (lambda s: s if s and predicate(s) else None)(read()), seconds)


class Window:
    """What the GTK window will do on the socket: hello, keep the connection, close."""
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.s.settimeout(20)
        self.s.connect(str(SOCKET))
        self.s.send(b"hello")
        self.reply = self.s.recv(4096).decode()

    def send(self, *fields):
        self.s.send("\x1f".join(str(f) for f in fields).encode())
        return self.s.recv(1 << 20).decode()

    def close(self):
        self.s.close()


class Host:
    def __init__(self, *args, env=None):
        self.log = WORK / f"host-{len(list(WORK.glob('host-*.log')))}.log"
        self.proc = subprocess.Popen([str(BIN / "sukut-host"), "--engine", str(ENGINE), "--icon-theme-path", str(ICONS),
                                      "--window-command", str(WORK / "fake-window.sh"), *args],
                                     stdout=subprocess.DEVNULL, stderr=open(self.log, "w"), env=env or ENV)
        self.ready = wait_until(lambda: "sukut-host: ready" in self.text(), 15)

    def text(self):
        try:
            return self.log.read_text()
        except OSError:
            return ""

    def exited(self, seconds=15):
        try:
            return self.proc.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            return None

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill(); self.proc.wait(timeout=10)


def opens():
    return OPENS.read_text().splitlines() if OPENS.exists() else []


def memory(pid):
    fields = {}
    for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, value = line.partition(":")
        if key in ("VmRSS", "RssAnon"):
            fields[key] = value.strip()
    return fields


def settings():
    return json.loads(SETTINGS.read_text()) if SETTINGS.exists() else {}


# ----- scenarios -------------------------------------------------------------
def scenario_a(notifications):
    host = Host("--tray")
    check("A_host_ready", host.ready, host.text().strip()[-200:])
    mode_dir, mode_socket = stat.S_IMODE(RUNTIME.stat().st_mode), stat.S_IMODE(SOCKET.stat().st_mode)
    check("A_socket_private", mode_dir == 0o700 and mode_socket == 0o600, f"dir={oct(mode_dir)} socket={oct(mode_socket)}")
    s = read()
    check("A_starts_off_without_tray", s.get("mode") == 0 and s.get("trayAvailable") is False, s)
    opened = wait_until(lambda: len(opens()) == 1, 8)
    check("A_no_tray_opens_window", opened and opens() == ["open token=none"], opens())
    window = Window()
    check("A_window_hello", window.reply.startswith("1"), window.reply)
    window.close()
    code = host.exited(15)
    check("A_window_close_exits_without_tray", code == 0 and not SOCKET.exists(), f"exit={code}")
    check("A_no_notification_without_tray", notifications.calls("Notify") == [], notifications.calls("Notify"))


def scenario_b(notifications):
    watcher = Fake("watcher")
    OPENS.unlink(missing_ok=True)
    host = Host("--tray")
    check("B_host_ready", host.ready)
    registered = wait_until(lambda: watcher.calls("RegisterStatusNotifierItem"), 5)
    sni = registered[0]["service"] if registered else ""
    check("B_tray_registered", sni == f"org.kde.StatusNotifierItem-{host.proc.pid}-1", sni)
    check("B_read_tray_available", wait_state(lambda s: s.get("trayAvailable") is True, 5) is not None)
    props = {name: prop(sni, "/StatusNotifierItem", ITEM, name) for name in
             ("Id", "Category", "Title", "Status", "IconName", "IconThemePath", "ItemIsMenu", "Menu", "ToolTip")}
    check("B_item_properties", props["Id"] == APP_ID and props["Category"] == "ApplicationStatus" and props["Title"] == "Sukut"
          and props["Status"] == "Active" and props["IconName"] == f"{APP_ID}-tray-idle-symbolic" and props["Menu"] == "/MenuBar"
          and props["ItemIsMenu"] is False and props["IconThemePath"] == str(ICONS), {k: props[k] for k in props if k != "ToolTip"})
    check("B_tooltip", props["ToolTip"][2] == "سُكوت Sukut" and props["ToolTip"][3] == "Off", props["ToolTip"])
    check("B_menu_version", prop(sni, "/MenuBar", MENU, "Version") == 3 and prop(sni, "/MenuBar", MENU, "TextDirection") == "ltr")

    m = menu(sni)
    labels = [m[i].get("label", "-") for i in m[0]["children"]]
    check("B_menu_items", labels == ["Off", "-", "Sukut on", "-", "Music Filter", "Optimized", "Strict", "-",
                                     "Audio buffering", "Audio output", "Source app", "-", "Open Sukut", "Exit"], labels)
    check("B_status_disabled", m[1].get("enabled") is False)
    check("B_power_unchecked", m[3].get("toggle-type") == "checkmark" and m[3].get("toggle-state") == 0)
    check("B_preferred_mode_marked", [m[i].get("toggle-state") for i in (5, 6, 7)] == [1, 0, 0])
    buffering = [(m[i]["label"], m[i]["toggle-state"]) for i in m[9]["children"]]
    check("B_buffering_submenu", buffering == [("Lowest", 0), ("Balanced", 1), ("Maximum", 0)], buffering)
    about_to_show(sni, 10)
    m = menu(sni)
    outputs = {m[i]["label"]: m[i]["toggle-state"] for i in m[10]["children"]}
    check("B_output_submenu", outputs.get("test_speakers") == 1 and "Sukut" not in outputs, outputs)
    check("B_source_submenu", [(m[i]["label"], m[i]["toggle-state"]) for i in m[11]["children"]][:1] == [("All system audio", 1)])

    # On from the menu: the preferred mode, all system audio.
    click(sni, 3)
    s = wait_state(lambda s: s["mode"] == 1 and s["state"] == 1 and not s["busy"])
    check("B_menu_power_on", s is not None and default_sink() == "sukut_sink", f"{s and s['status']} default={default_sink()}")
    check("B_icon_on", wait_until(lambda: prop(sni, "/StatusNotifierItem", ITEM, "IconName") == f"{APP_ID}-tray-on", 3))
    m = menu(sni)
    check("B_menu_shows_on", m[3]["toggle-state"] == 1 and m[5]["toggle-state"] == 1, (m[3]["toggle-state"], m[5]["toggle-state"]))
    s = wait_state(lambda s: s["standby"], 10)
    m = wait_until(lambda: (lambda x: x if x[1]["label"] == "Standby · nothing playing" else None)(menu(sni)), 3)
    check("B_standby_shown", s is not None and m is not None and m[1].get("icon-name") == f"{APP_ID}-standby-symbolic", m and m[1])
    # RealtimeKit grants only to processes in an active desktop session; either
    # way the engine must keep running (normal priority is the fallback).
    rtkit = next((line for line in host.text().splitlines() if "RealtimeKit" in line), "")
    check("B_realtimekit_asked", rtkit != "" and s is not None and s["state"] == 1, rtkit or "not asked")
    measurements["priority_set"] = bool(s and s["prioritySet"])
    measurements["host_on_tray_standby"] = memory(host.proc.pid)

    click(sni, 7)  # Strict
    s = wait_state(lambda s: s["mode"] == 3 and not s["busy"])
    check("B_menu_mode_switch", s is not None and settings().get("PreferredMode") == 3, s and s["status"])
    version = settings().get("HostSettingsVersion", 0)
    click(sni, 102)  # Audio buffering > Maximum
    s = wait_state(lambda s: s["mode"] == 3 and s["state"] == 1 and not s["busy"] and s["activeBuffering"] == 2)
    check("B_menu_buffering", s is not None and settings().get("Buffering") == 2 and settings().get("HostSettingsVersion", 0) > version,
          settings())

    # Source app from the menu, then the app closes (the Windows rule).
    tone(WORK / "app.wav", 440, 60)
    app = subprocess.Popen(["pw-play", str(WORK / "app.wav")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    about_to_show(sni, 11)
    m = menu(sni)
    apps = {m[i]["label"]: i for i in m[11]["children"][1:]}
    check("B_source_app_listed", len(apps) == 1, apps)
    if apps:
        click(sni, next(iter(apps.values())))
    s = wait_state(lambda s: s["activeSourcePid"] == app.pid and s["state"] == 1 and not s["busy"])
    check("B_menu_source_app", s is not None and default_sink() == "test_speakers", f"default={default_sink()}")
    app.terminate(); app.wait(timeout=10)
    s = wait_state(lambda s: s["state"] == 2, 8)
    attention = wait_until(lambda: prop(sni, "/StatusNotifierItem", ITEM, "Status") == "NeedsAttention", 3)
    check("B_source_closed_attention", s is not None and s["attention"] == "source_closed" and s["mode"] == 0 and attention,
          s and (s["status"], s["attention"]))
    m = wait_until(lambda: (lambda x: x if settings().get("SourcePid") == 0 and x[300]["toggle-state"] == 1 else None)(menu(sni)), 3)
    check("B_source_reset_to_all", m is not None and m[1]["label"] == "Attention · open Sukut", m and m[1]["label"])
    click(sni, 3)
    s = wait_state(lambda s: s["mode"] == 3 and s["state"] == 1 and not s["busy"] and s["activeSourcePid"] == 0)
    active = wait_until(lambda: prop(sni, "/StatusNotifierItem", ITEM, "Status") == "Active", 3)
    check("B_on_again_all_audio", s is not None and default_sink() == "sukut_sink" and active, f"default={default_sink()}")

    # Arabic, then a window opens and closes.
    check("B_settings_verb", request("settings", "Language=ar").startswith("1"))
    m = wait_until(lambda: (lambda x: x if x[3]["label"] == "تشغيل سُكوت" else None)(menu(sni)), 3)
    check("B_menu_arabic", m is not None and prop(sni, "/MenuBar", MENU, "TextDirection") == "rtl"
          and [m[i]["label"] for i in (5, 6, 7, 13, 14)] == ["مرشّح الموسيقى", "محسّن", "صارم", "فتح سُكوت", "خروج"])
    window = Window()
    m = wait_until(lambda: (lambda x: x if x[0]["children"] == [13, 14] else None)(menu(sni)), 3)
    check("B_window_open_menu_shrinks", m is not None, m and [m[i]["label"] for i in m[0]["children"]])
    call(sni, "/StatusNotifierItem", ITEM, "ProvideXdgActivationToken", GLib.Variant("(s)", ("tray-token",)))
    call(sni, "/StatusNotifierItem", ITEM, "Activate", GLib.Variant("(ii)", (0, 0)))
    check("B_click_opens_with_token", wait_until(lambda: opens() == ["open token=tray-token"], 3), opens())
    window.close()
    notice = wait_until(lambda: notifications.calls("Notify"), 5)
    n = notice[0] if notice else {}
    check("B_close_to_tray_notification", n.get("app") == "Sukut" and n.get("icon") == APP_ID
          and n.get("hints", {}).get("desktop-entry") == APP_ID and n.get("title") == "سُكوت في منطقة الإشعارات"
          and n.get("body") == "يستمر الترشيح. أُغلقت النافذة لتوفير الذاكرة.", n)
    m = wait_until(lambda: (lambda x: x if len(x[0]["children"]) == 14 else None)(menu(sni)), 3)
    check("B_host_stays_in_tray", host.proc.poll() is None and m is not None)

    # A second launch opens the window through the running host.
    second = run(BIN / "sukut-host", "--window-command", WORK / "fake-window.sh", timeout=15, env=dict(ENV, XDG_ACTIVATION_TOKEN="second-token"))
    check("B_second_launch_opens", second.returncode == 0 and wait_until(lambda: opens()[-1:] == ["open token=second-token"], 3), opens())
    second = run(BIN / "sukut-host", "--tray", timeout=15, env=ENV)
    time.sleep(0.5)
    check("B_second_tray_launch_quiet", second.returncode == 0 and len(opens()) == 2 and host.proc.poll() is None, opens())

    check("B_autostart_on", request("settings", "StartWithSystem=1").startswith("1") and AUTOSTART.exists()
          and "Exec=sukut --tray" in AUTOSTART.read_text() and settings().get("StartWithSystem") is True)
    check("B_autostart_off", request("settings", "StartWithSystem=0").startswith("1") and not AUTOSTART.exists())

    # The panel restarts: the host registers again.
    watcher.stop()
    check("B_tray_gone_detected", wait_state(lambda s: s.get("trayAvailable") is False, 5) is not None)
    watcher = Fake("watcher")
    again = wait_until(lambda: watcher.calls("RegisterStatusNotifierItem"), 5)
    check("B_tray_reregistered", again and again[0]["service"] == sni and wait_state(lambda s: s.get("trayAvailable"), 5) is not None)

    click(sni, 3)  # Off
    s = wait_state(lambda s: s["mode"] == 0 and not s["busy"])
    check("B_menu_power_off", s is not None and default_sink() == "test_speakers" and node_id("sukut_sink") is None, f"default={default_sink()}")
    about_to_show(sni, 11)  # menu use while Off trims memory
    time.sleep(0.5)
    measurements["host_off_tray"] = memory(host.proc.pid)
    click(sni, 14)  # Exit
    code = host.exited(15)
    check("B_menu_exit", code == 0 and not SOCKET.exists() and node_id("sukut_sink") is None and not JOURNAL.exists(), f"exit={code}")
    s = settings()
    check("B_settings_saved", s.get("Language") == "ar" and s.get("PreferredMode") == 3 and s.get("Buffering") == 2
          and s.get("SourcePid") == 0 and s.get("CloseToTray") is True and s.get("Output", None) is not None, s)
    watcher.stop()


def scenario_c():
    host = Host("--tray")
    # A chosen app that is gone when a mode is selected: the closed-app rule.
    reply = request("select", 1, "", 999999, 1, 0)
    s = wait_state(lambda s: s["state"] == 2)
    check("C_unavailable_app_rule", reply == "0source app closed" and s is not None and s["attention"] == "source_closed"
          and wait_until(lambda: settings().get("SourcePid") == 0, 3) and default_sink() == "test_speakers", reply)
    reply = request("select", 1, "", 0, 1, 0)
    check("C_select_verb", reply.startswith("1") and default_sink() == "sukut_sink", reply)
    measurements["host_on_no_tray"] = memory(host.proc.pid)
    host.proc.send_signal(signal.SIGTERM)
    code = host.exited(15)
    check("C_sigterm_restores", code == 0 and default_sink() == "test_speakers" and node_id("sukut_sink") is None
          and not JOURNAL.exists(), f"exit={code} default={default_sink()}")


def scenario_d(notifications):
    watcher = Fake("watcher")  # with a tray, only an explicit exit keeps the notice away
    host = Host("--tray")
    check("D_select_verb", request("select", 2, "", 0, 1, -6).startswith("1") and default_sink() == "sukut_sink")
    host.kill()
    check("D_killed_leaves_journal", wait_until(lambda: node_id("sukut_sink") is None, 4) and JOURNAL.exists()
          and default_sink() == "sukut_sink", default_sink())
    host = Host("--tray")
    check("D_next_start_restores", "restored audio left by an earlier run" in host.text() and default_sink() == "test_speakers"
          and not JOURNAL.exists(), host.text().strip().splitlines()[:2])
    before = len(notifications.calls("Notify"))
    window = Window()
    check("D_exit_verb", window.send("exit").startswith("1"))
    window.close()
    code = host.exited(15)
    check("D_window_exit_quits", code == 0 and len(notifications.calls("Notify")) == before, f"exit={code}")
    watcher.stop()


def main():
    if run("pw-cli", "info", "0").returncode != 0:
        check("pipewire_running", False)
        return
    if SOCKET.exists():
        try:
            request("read", timeout=2)
            check("no_other_host_running", False, "a Sukut host is already running")
            return
        except OSError:
            pass
    for folder in (CONFIG, STATE):
        subprocess.run(["rm", "-rf", str(folder)])
    OPENS.unlink(missing_ok=True)
    (WORK / "fake-window.sh").write_text(f'#!/bin/sh\necho "open token=${{XDG_ACTIVATION_TOKEN:-none}}" >> "{OPENS}"\n')
    (WORK / "fake-window.sh").chmod(0o755)
    make_output("test_speakers", 1000)
    set_default("test_speakers")
    notifications = Fake("notifications")
    try:
        scenario_a(notifications)
        scenario_b(notifications)
        scenario_c()
        scenario_d(notifications)
    finally:
        notifications.stop()


original_default = default_sink()
try:
    main()
finally:
    remove_output("test_speakers")
    if original_default:
        set_default(original_default)
    passed = sum(r["pass"] for r in results)
    log(f"memory={json.dumps(measurements)}")
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage5-results.json").write_text(json.dumps({"checks": results, "memory": measurements}, indent=2, ensure_ascii=False) + "\n")
    (EVIDENCE / "stage5-log.txt").write_text("\n".join(log_lines) + "\n")
    for path in sorted(WORK.glob("host-*.log")):
        (EVIDENCE / path.name).write_text(path.read_text())
        path.unlink()
