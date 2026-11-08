"""Stage 9 routing test: Windows 0.4.2's output handling and EasyEffects on Linux.

  dbus-run-session -- python3 -I stage9_routing_test.py <build-bin-dir> <engine-dir> <icon-dir> <evidence-dir>

Uses this session's PipeWire with virtual outputs: test_speakers and test_headphones
(plain virtual sinks), test_bt (with a device.id and device.api=bluez5, like Bluetooth
headphones), test_hdmi (a device.id and an HDMI name), test_later (a virtual sink that
appears after Sukut starts, like EasyEffects' sink) and test_fx, a pw-loopback
whose sink plays on to another output (what EasyEffects does). Real outputs on
the machine stay as they are; every scenario names its output explicitly.

The engine (sukut_pw_filter):
  F. A newly connected device is used at once; removed, Sukut goes back to the
     previous choice; reconnected, Sukut uses it again; Off gives it the default.
  G. HDMI and virtual sinks that appear, and a new output of a device that was
     already there (a profile change), are not followed.
  H. A chosen output that is missing: an error without --fallback; with it, Sukut
     plays on the default and goes to the output when it appears; Off restores it.
  I. A feedback loop (the loopback's playback moved to Sukut's sink, as EasyEffects
     does when it follows the default output) is detected and broken.
  J. An app pinned to another output (EasyEffects' "Process all output streams")
     goes through Sukut; its pin is put back on Off and after a crash (--restore);
     pinned again by someone else, it is left alone and reported.
  K. Picking the output Sukut already plays on in the sound settings keeps Sukut in front.
The host (sukut-host on a private runtime directory and session bus):
  L. A system output chosen while Off is followed at Start; an output chosen in
     Sukut is kept; a new device is used and notified; Off gives it the default;
     a missing output starts anyway; settings from before follow the system once;
     an audio loop becomes a notice with a notification.
"""
import json
import math
import os
import pathlib
import re
import signal
import socket
import subprocess
import sys
import threading
import time
import wave

BIN, ENGINE, ICONS, EVIDENCE = (pathlib.Path(a) for a in sys.argv[1:5])
EVIDENCE.mkdir(parents=True, exist_ok=True)
HERE = pathlib.Path(__file__).resolve().parent
WORK = pathlib.Path(os.environ.get("HOME", "/tmp")) / "sukut-stage9-routing"
WORK.mkdir(exist_ok=True)
FILTER_STATE = WORK / "filter-state"
JOURNAL = FILTER_STATE / "sukut/routing.json"
FILTER_ENV = dict(os.environ, XDG_STATE_HOME=str(FILTER_STATE))
# The host gets its own runtime directory (socket, lock), so a Sukut already
# running for the user is not disturbed; PipeWire stays this session's.
REAL_RUNTIME = os.environ["XDG_RUNTIME_DIR"]
HOST_RUNTIME = WORK / "runtime"
CONFIG, STATE = WORK / "config", WORK / "state"
SETTINGS = CONFIG / "sukut/settings.json"
SOCKET = HOST_RUNTIME / "sukut/host.socket"
HOST_ENV = dict(os.environ, XDG_CONFIG_HOME=str(CONFIG), XDG_STATE_HOME=str(STATE), XDG_RUNTIME_DIR=str(HOST_RUNTIME),
                PIPEWIRE_RUNTIME_DIR=os.environ.get("PIPEWIRE_RUNTIME_DIR", REAL_RUNTIME))
HOST_ENV.pop("XDG_ACTIVATION_TOKEN", None)
TEST_SINKS = ("test_speakers", "test_headphones", "test_bt", "test_hdmi", "test_virtual", "test_profile", "test_later")
results, log_lines = [], []


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


# ----- PipeWire ----------------------------------------------------------------
def dump():
    return json.loads(run("pw-dump").stdout or "[]")


def node_props(name):
    return next((o.get("info", {}).get("props", {}) for o in dump() if o.get("type") == "PipeWire:Interface:Node"
                 and o.get("info", {}).get("props", {}).get("node.name") == name), None)


def node_id(name):
    return next((o["id"] for o in dump() if o.get("type") == "PipeWire:Interface:Node"
                 and o.get("info", {}).get("props", {}).get("node.name") == name), None)


def serial_of(name):
    props = node_props(name)
    return str(props.get("object.serial")) if props else ""


def app_node(pid):
    objects = dump()
    clients = {o["id"] for o in objects if o.get("type") == "PipeWire:Interface:Client"
               and str(o.get("info", {}).get("props", {}).get("pipewire.sec.pid")) == str(pid)}
    return next((o["id"] for o in objects if o.get("type") == "PipeWire:Interface:Node"
                 and o.get("info", {}).get("props", {}).get("media.class") == "Stream/Output/Audio"
                 and o.get("info", {}).get("props", {}).get("client.id") in clients), None)


def linked_to(source_id):
    if source_id is None:
        return []
    objects = dump()
    names = {o["id"]: o.get("info", {}).get("props", {}).get("node.name", "") for o in objects
             if o.get("type") == "PipeWire:Interface:Node"}
    return sorted({names.get(o["info"]["input-node-id"], "?") for o in objects
                   if o.get("type") == "PipeWire:Interface:Link" and o.get("info", {}).get("output-node-id") == source_id})


def playback_to():
    return linked_to(node_id("sukut_playback"))


def default_sink(key="default.configured.audio.sink"):
    match = re.search(r"value:'(.*?)' type", run("pw-metadata", "-n", "default", "0", key).stdout)
    try:
        return json.loads(match.group(1)).get("name", "") if match else ""
    except json.JSONDecodeError:
        return ""


def set_default(name):
    run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink", json.dumps({"name": name}), "Spa:String:JSON")
    time.sleep(0.6)


def target_of(node):
    match = re.search(r"value:'(.*?)'", run("pw-metadata", "-n", "default", str(node), "target.object").stdout)
    return match.group(1) if match else ""


def pin(node, target):
    """Pin a stream to an output, as EasyEffects or the sound settings do."""
    run("pw-metadata", "-n", "default", str(node), "target.object", serial_of(target), "Spa:Id")


def unpin(node):
    run("pw-metadata", "-n", "default", "-d", str(node), "target.object")


def make_sink(name, priority=1000, api=None, device=None):
    """A virtual output; with api, it looks like a device's output (PipeWire's
    registry shows device.id, which Sukut uses to tell devices from virtual sinks).
    Without a device object behind that id, Sukut treats it as newly connected;
    with device=<id of an existing device>, as a profile change of that device."""
    if node_id(name) is None:
        extra = f" device.api={api} device.id={device or 90000 + sum(map(ord, name))}" if api else ""
        run("pw-cli", "create-node", "adapter",
            "{ factory.name=support.null-audio-sink node.name=%s node.description=%s media.class=Audio/Sink "
            "audio.position=[ FL FR ] audio.rate=48000 priority.session=%d object.linger=true%s }" % (name, name, priority, extra))
        wait_until(lambda: node_id(name) is not None)


def remove_sink(name):
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


def app(path):
    proc = subprocess.Popen(["pw-play", "--target", "test_speakers", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait_until(lambda: app_node(proc.pid) is not None)
    return proc


class Loopback:
    """A virtual sink whose sound plays on to another output, like EasyEffects."""
    def __init__(self, name, target):
        self.proc = subprocess.Popen(["pw-loopback", "-n", name,
                                      "--capture-props", f"media.class=Audio/Sink node.name={name} node.description={name}",
                                      "--playback-props", f"node.name={name}_out target.object={target}"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_until(lambda: node_id(name) is not None and node_id(f"{name}_out") is not None)

    def stop(self):
        self.proc.terminate(); self.proc.wait(timeout=10)


class Filter:
    def __init__(self, *args):
        self.lines = []
        self.proc = subprocess.Popen([str(BIN / "sukut_pw_filter"), "--engine", str(ENGINE), "--mode", "1", *map(str, args)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=FILTER_ENV)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            if not line.startswith("STATS"):
                self.lines.append(line.strip())

    def mark(self):
        return len(self.lines)

    def wait_line(self, text, seconds=6, after=0):
        return wait_until(lambda: next((l for l in self.lines[after:] if text in l), None), seconds)

    def stop(self, sig=signal.SIGTERM):
        if self.proc.poll() is None:
            self.proc.send_signal(sig)
        self.proc.wait(timeout=30)
        time.sleep(0.4)
        return self.proc.returncode


# ----- the engine ---------------------------------------------------------------
def scenario_f():
    set_default("test_speakers")
    f = Filter("--output", "test_speakers", "--fallback")
    started = f.wait_line("STARTED", 30)
    check("F_started", started and "output=test_speakers" in started, started or f.lines[-3:])
    mark = f.mark()
    make_sink("test_bt", 950, api="bluez5")  # Bluetooth headphones connect
    line = f.wait_line("OUTPUT_CHANGED", 6, mark)
    check("F_new_device_used", line and "to=test_bt reason=connected" in line, line)
    check("F_plays_to_new_device", wait_until(lambda: playback_to() == ["test_bt"], 4), playback_to())
    mark = f.mark()
    remove_sink("test_bt")  # and disconnect
    line = f.wait_line("OUTPUT_CHANGED", 6, mark)
    check("F_back_to_previous_choice", line and "to=test_speakers reason=removed" in line, line)
    mark = f.mark()
    make_sink("test_bt", 950, api="bluez5")  # and reconnect
    line = f.wait_line("OUTPUT_CHANGED", 6, mark)
    check("F_reconnected_device_used", line and "to=test_bt" in line and ("reason=returned" in line or "reason=connected" in line), line)
    f.stop()
    check("F_off_gives_default_to_new_device", default_sink() == "test_bt" and node_id("sukut_sink") is None, default_sink())
    remove_sink("test_bt")


def scenario_g():
    set_default("test_speakers")
    f = Filter("--output", "test_speakers")
    check("G_started", f.wait_line("STARTED", 30) is not None)
    make_sink("test_hdmi", 2000, api="alsa")  # a screen with speakers wakes up
    make_sink("test_virtual", 2000)           # an app creates a virtual output
    # A new output of a device that was already there: a profile change in the sound settings.
    existing = next((p.get("device.id") for o in dump() if o.get("type") == "PipeWire:Interface:Node"
                     for p in [o.get("info", {}).get("props", {})]
                     if p.get("media.class") == "Audio/Sink" and p.get("device.id") and not str(p.get("node.name")).startswith("test_")), None)
    if existing:
        make_sink("test_profile", 2000, api="alsa", device=existing)
    time.sleep(2.5)
    changed = [l for l in f.lines if l.startswith("OUTPUT_CHANGED")]
    check("G_hdmi_virtual_and_profile_change_not_followed", not changed and playback_to() == ["test_speakers"],
          f"{changed or playback_to()} profile_device={existing}")
    f.stop()
    remove_sink("test_hdmi"); remove_sink("test_virtual"); remove_sink("test_profile")


def scenario_h():
    set_default("test_speakers")
    remove_sink("test_later")
    r = run(BIN / "sukut_pw_filter", "--engine", ENGINE, "--output", "test_later", "--seconds", "2", env=FILTER_ENV)
    check("H_missing_without_fallback_fails", "START_FAILED" in r.stdout and "test_later" in r.stdout, r.stdout.strip()[:120])
    f = Filter("--output", "test_later", "--fallback")
    started = f.wait_line("STARTED", 30)
    check("H_missing_plays_on_default", started and "output=test_later" not in started, started)
    check("H_missing_reported", f.wait_line("reason=missing", 4) is not None)
    mark = f.mark()
    make_sink("test_later", 0)  # EasyEffects starts after Sukut
    line = f.wait_line("OUTPUT_CHANGED", 6, mark)
    check("H_goes_to_output_when_it_appears", line and "to=test_later reason=returned" in line, line)
    check("H_plays_there", wait_until(lambda: playback_to() == ["test_later"], 4), playback_to())
    f.stop()
    check("H_off_gives_default_to_chosen_output", default_sink() == "test_later", default_sink())
    remove_sink("test_later")


def scenario_i():
    set_default("test_speakers")
    fx = Loopback("test_fx", "test_speakers")
    props = node_props("test_fx") or {}
    log(f"test_fx link-group={props.get('node.link-group')} media.class={props.get('media.class')}")
    f = Filter("--output", "test_fx")
    started = f.wait_line("STARTED", 30)
    check("I_started_on_effects_sink", started and "output=test_fx" in started and wait_until(lambda: playback_to() == ["test_fx"], 4),
          started)
    check("I_no_loop_while_effects_play_to_speakers", f.wait_line("FEEDBACK_LOOP", 1.5) is None)
    mark = f.mark()
    pin(node_id("test_fx_out"), "sukut_sink")  # the effects app follows the default output, which is Sukut
    line = f.wait_line("FEEDBACK_LOOP", 6, mark)
    check("I_loop_detected", line and "through=test_fx" in line, line)
    moved = f.wait_line("reason=loop", 4, mark)
    check("I_loop_broken", moved and "to=test_fx " not in moved and wait_until(lambda: "test_fx" not in playback_to(), 4),
          f"{moved} now -> {playback_to()}")
    f.stop()
    fx.stop()


def scenario_j():
    set_default("test_speakers")
    app1 = app(WORK / "app1.wav")
    a1 = app_node(app1.pid)
    original = serial_of("test_headphones")
    pin(a1, "test_headphones")  # EasyEffects' "Process all output streams" pinned it
    check("J_app_pinned_elsewhere", wait_until(lambda: linked_to(a1) == ["test_headphones"], 4), linked_to(a1))

    f = Filter("--output", "test_speakers")
    check("J_pinned_app_redirected", f.wait_line("STREAM_REDIRECTED", 30) is not None
          and wait_until(lambda: linked_to(a1) == ["sukut_sink"], 4), linked_to(a1))
    f.stop()
    check("J_pin_put_back_on_off", target_of(a1) == original and wait_until(lambda: linked_to(a1) == ["test_headphones"], 4),
          f"target={target_of(a1)!r} expected={original!r} -> {linked_to(a1)}")

    f = Filter("--output", "test_speakers")
    f.wait_line("STREAM_REDIRECTED", 30)
    wait_until(lambda: linked_to(a1) == ["sukut_sink"], 4)
    f.stop(signal.SIGKILL)
    journal = JOURNAL.read_text() if JOURNAL.exists() else ""
    check("J_journal_keeps_pin", '"pins":[{' in journal and f'"value":"{original}"' in journal, journal.strip()[:200])
    restore = run(BIN / "sukut_pw_filter", "--restore", env=FILTER_ENV).stdout.strip()
    check("J_restore_puts_pin_back", restore.startswith("RESTORED") and "pins put back: 1" in restore and target_of(a1) == original,
          f"{restore}; target={target_of(a1)!r}")

    f = Filter("--output", "test_speakers")
    f.wait_line("STREAM_REDIRECTED", 30)
    wait_until(lambda: linked_to(a1) == ["sukut_sink"], 4)
    mark = f.mark()
    pin(a1, "test_speakers")  # someone pins it again while Sukut runs
    line = f.wait_line("STREAM_PINNED", 6, mark)
    time.sleep(1.5)
    check("J_repinned_app_left_alone", line and "target=test_speakers" in line and target_of(a1) == serial_of("test_speakers"),
          f"{line}; target={target_of(a1)!r}")
    f.stop()
    check("J_new_pin_kept_after_off", target_of(a1) == serial_of("test_speakers"), target_of(a1))
    unpin(a1)
    app1.terminate(); app1.wait(timeout=10)


def scenario_k():
    set_default("test_speakers")
    f = Filter("--output", "test_speakers")
    check("K_started", f.wait_line("STARTED", 30) is not None and wait_until(lambda: default_sink() == "sukut_sink", 4))
    set_default("test_speakers")  # the user picks the output Sukut already plays on
    check("K_sukut_stays_in_front", wait_until(lambda: default_sink() == "sukut_sink", 3), default_sink())
    f.stop()
    check("K_off_restores", default_sink() == "test_speakers", default_sink())


# ----- the host -----------------------------------------------------------------
class Notifications:
    def __init__(self):
        self.file = WORK / "fake-notifications.jsonl"
        self.proc = subprocess.Popen([sys.executable, "-I", str(HERE / "fake_desktop.py"), "notifications"],
                                     stdout=open(self.file, "w"), stderr=subprocess.DEVNULL)
        wait_until(lambda: any("ready" in e for e in self.events()), 5)

    def events(self):
        try:
            return [json.loads(line) for line in self.file.read_text().splitlines() if line.strip()]
        except (OSError, json.JSONDecodeError):
            return []

    def notices(self, title):
        return [e for e in self.events() if e.get("call") == "Notify" and e.get("title") == title]

    def stop(self):
        self.proc.terminate(); self.proc.wait(timeout=5)


class Host:
    def __init__(self):
        self.log = WORK / f"host-{len(list(WORK.glob('host-*.log')))}.log"
        self.proc = subprocess.Popen([str(BIN / "sukut-host"), "--tray", "--engine", str(ENGINE), "--icon-theme-path", str(ICONS),
                                      "--window-command", "true"],
                                     stdout=subprocess.DEVNULL, stderr=open(self.log, "w"), env=HOST_ENV)
        self.ready = wait_until(lambda: "sukut-host: ready" in self.text(), 15)

    def text(self):
        try:
            return self.log.read_text()
        except OSError:
            return ""

    def exit(self):
        if self.proc.poll() is None:
            request("exit")
            try:
                self.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.proc.kill(); self.proc.wait(timeout=5)


def request(*fields, timeout=30):
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


def settings():
    try:
        return json.loads(SETTINGS.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def write_settings(**values):
    base = {"Language": "en", "Theme": "system", "Output": "", "SourcePid": 0, "PreferredMode": 1, "HostSettingsVersion": 1,
            "Buffering": 1, "AmplificationDb": 0, "OptimizedBackgroundDb": 0, "SetupCompleted": True, "CloseToTray": True,
            "StartWithSystem": False}
    base.update(values)
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(base, indent=2) + "\n")


def start(output):
    reply = request("select", 1, output, 0, 1, 0)
    return reply, wait_state(lambda s: s["mode"] == 1 and not s["busy"])


def stop(output=""):
    request("select", 0, output, 0, 1, 0)
    return wait_state(lambda s: s["mode"] == 0 and not s["busy"])


def scenario_l():
    for folder in (CONFIG, STATE, HOST_RUNTIME):
        subprocess.run(["rm", "-rf", str(folder)])
    HOST_RUNTIME.mkdir(mode=0o700, parents=True)
    notifications = Notifications()
    try:
        write_settings(Output="test_speakers", OutputDefault="test_speakers")
        set_default("test_headphones")  # headphones picked in the sound settings while Sukut was Off
        host = Host()
        check("L_host_ready", host.ready, host.text()[-300:])
        reply, s = start("test_speakers")
        check("L_follows_system_choice_made_while_off", reply.startswith("1") and s and s["output"] == "test_headphones"
              and settings().get("Output") == "test_headphones", f"{reply} {s and s['output']} {settings().get('Output')}")

        make_sink("test_bt", 950, api="bluez5")
        s = wait_state(lambda s: s["output"] == "test_bt", 6)
        notes = wait_until(lambda: notifications.notices("Audio output switched"), 5)
        check("L_new_device_used_and_notified", s and notes and notes[0]["body"] == "Sukut now uses test_bt"
              and wait_until(lambda: settings().get("Output") == "test_bt", 3), notes)
        stop("test_bt")
        check("L_off_gives_default_to_new_device", wait_until(lambda: default_sink() == "test_bt", 3)
              and wait_until(lambda: settings().get("OutputDefault") == "test_bt", 4), f"{default_sink()} {settings().get('OutputDefault')}")

        request("settings", "Output=test_speakers")  # chosen in Sukut while Off
        reply, s = start("test_speakers")
        check("L_choice_in_sukut_kept", s and s["output"] == "test_speakers", s and s["output"])
        stop("test_speakers")
        remove_sink("test_bt")

        request("settings", "Output=test_gone")
        reply, s = start("test_gone")
        check("L_missing_output_starts_anyway", reply.startswith("1") and s and s["output"] not in ("", "test_gone")
              and settings().get("Output") == "test_gone", f"{reply} {s and s['output']} {settings().get('Output')}")
        stop("test_gone")

        fx = Loopback("test_fx", "test_speakers")
        request("settings", "Output=test_fx")
        reply, s = start("test_fx")
        before = len(notifications.notices("Sukut stopped an audio loop"))
        pin(node_id("test_fx_out"), "sukut_sink")
        s = wait_state(lambda s: s.get("notice", "").startswith("feedback_loop:"), 6)
        notes = wait_until(lambda: notifications.notices("Sukut stopped an audio loop")[before:], 5)
        check("L_loop_notice_and_notification", s and s["notice"] == "feedback_loop:test_fx" and s["output"] != "test_fx" and notes
              and settings().get("Output") == "test_fx", f"{s and s.get('notice')} {s and s['output']} {notes}")
        stop("test_fx")
        fx.stop()
        host.exit()

        write_settings(Output="test_speakers")  # settings from before 0.4.0~beta.7: no OutputDefault
        set_default("test_headphones")
        host = Host()
        reply, s = start("test_speakers")
        check("L_old_settings_follow_system_once", s and s["output"] == "test_headphones" and settings().get("OutputDefault") == "test_headphones",
              f"{s and s['output']} {settings().get('OutputDefault')}")
        stop("test_headphones")
        host.exit()
    finally:
        notifications.stop()
        for path in sorted(WORK.glob("host-*.log")):
            (EVIDENCE / path.name).write_text(path.read_text())
            path.unlink()


def main():
    if run("pw-cli", "info", "0").returncode != 0:
        check("pipewire_running", False)
        return
    tone(WORK / "app1.wav", 440, 60)
    # Above any real device, so a fallback never plays the user's audio on real speakers.
    make_sink("test_speakers", 3000)
    make_sink("test_headphones", 2900)
    subprocess.run(["rm", "-rf", str(FILTER_STATE)])
    for scenario in (scenario_f, scenario_g, scenario_h, scenario_i, scenario_j, scenario_k, scenario_l):
        try:
            scenario()
        except Exception as error:  # one broken scenario must not hide the others
            check(f"{scenario.__name__}_ran", False, repr(error))


original_default = default_sink()
try:
    main()
finally:
    for name in TEST_SINKS:
        remove_sink(name)
    if original_default:
        set_default(original_default)
    passed = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage9-routing-results.json").write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n")
    (EVIDENCE / "stage9-routing-log.txt").write_text("\n".join(log_lines) + "\n")
