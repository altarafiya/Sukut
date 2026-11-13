"""Sandbox smoke test: the installed Flatpak (or, with SUKUT_TEST_TARGET=snap, the snap)
against this session's PipeWire. SUKUT_TEST_TARGET=native tests an installed Debian build.

  python3 -I flatpak_smoke_test.py <evidence-dir>

Virtual outputs only; test_speakers ranks above real devices so audio never
falls back to real speakers. The original default output is put back at the end.
"""
import json, os, pathlib, re, socket, subprocess, sys, time

APP = "io.github.altarafiya.Sukut"
EVIDENCE = pathlib.Path(sys.argv[1]); EVIDENCE.mkdir(parents=True, exist_ok=True)
SNAP = os.environ.get("SUKUT_TEST_TARGET") == "snap"
NATIVE = os.environ.get("SUKUT_TEST_TARGET") == "native"
SOCKET = pathlib.Path(os.environ["XDG_RUNTIME_DIR"]) / ("snap.sukut/sukut/host.socket" if SNAP else "sukut/host.socket" if NATIVE else "app/" + APP + "/sukut/host.socket")
CONFIG = pathlib.Path.home() / ("snap/sukut/current/.config" if SNAP else ".config" if NATIVE else ".var/app/" + APP + "/config") / "sukut/settings.json"
LOG = EVIDENCE / "host.log"
results, lines = [], []

def log(t): print(t, flush=True); lines.append(t)
def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": str(detail)}); log(f"{name}={'PASS' if ok else 'FAIL'} {detail}")
def run(*a, timeout=60): return subprocess.run([str(x) for x in a], capture_output=True, text=True, timeout=timeout)
def wait(p, s=8.0):
    end = time.time() + s
    while time.time() < end:
        v = p()
        if v: return v
        time.sleep(0.2)
    return p()
def dump(): return json.loads(run("pw-dump").stdout or "[]")
def nid(name): return next((o["id"] for o in dump() if o["type"].endswith("Node") and o["info"]["props"].get("node.name") == name), None)
def links(src):
    o = dump(); n = {x["id"]: x["info"]["props"].get("node.name", "") for x in o if x["type"].endswith("Node")}
    return sorted({n.get(x["info"]["input-node-id"], "?") for x in o if x["type"].endswith("Link") and x["info"]["output-node-id"] == src})
def default():
    m = re.search(r"value:'(.*?)' type", run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink").stdout)
    return json.loads(m.group(1)).get("name", "") if m else ""
def set_default(n): run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink", json.dumps({"name": n}), "Spa:String:JSON"); time.sleep(.6)
def sink(name, prio, extra=""):
    run("pw-cli", "create-node", "adapter", "{ factory.name=support.null-audio-sink node.name=%s node.description=%s media.class=Audio/Sink audio.position=[ FL FR ] audio.rate=48000 priority.session=%d object.linger=true%s }" % (name, name, prio, extra))
    wait(lambda: nid(name))
def drop(name):
    if (i := nid(name)) is not None: run("pw-cli", "destroy", i); wait(lambda: nid(name) is None)
def ask(*f, timeout=40):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(timeout); s.connect(str(SOCKET)); s.send("\x1f".join(map(str, f)).encode()); return s.recv(1 << 20).decode()
def state():
    r = ask("read"); return json.loads(r[1:]) if r.startswith("1") else {}
def wait_state(p, s=25): return wait(lambda: (lambda x: x if x and p(x) else None)(state()), s)
HOST = ["snap", "run", "--shell", "sukut", "-c", 'exec "$SNAP/usr/bin/sukut-launch" "$SNAP/usr/libexec/sukut/sukut-host" "$@"', "sh"] if SNAP else ["/usr/libexec/sukut/sukut-host"] if NATIVE else ["flatpak", "run", "--command=/app/libexec/sukut/sukut-host", APP]

def app_node(pid):
    o = dump(); c = {x["id"] for x in o if x["type"].endswith("Client") and str(x["info"]["props"].get("pipewire.sec.pid")) == str(pid)}
    return next((x["id"] for x in o if x["type"].endswith("Node") and x["info"]["props"].get("media.class") == "Stream/Output/Audio" and x["info"]["props"].get("client.id") in c), None)

def main():
    v = run("snap", "run", "--shell", "sukut", "-c", '"$SNAP/usr/bin/sukut-launch" adb version') if SNAP else run("adb", "version") if NATIVE else run("flatpak", "run", "--command=/app/bin/sukut-adb", APP, "version", timeout=180)
    check("adb_runs_in_sandbox", v.returncode == 0 and "Android Debug Bridge" in v.stdout, [l for l in v.stdout.splitlines() if "adb" in l.lower() or "Bridge" in l][:1] or v.stderr[-160:])
    sink("test_speakers", 3000); sink("test_headphones", 2900); set_default("test_speakers")
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps({"Language": "en", "Theme": "system", "Output": "test_speakers", "OutputDefault": "test_speakers", "SourcePid": 0, "PreferredMode": 1, "HostSettingsVersion": 1, "Buffering": 1, "AmplificationDb": 0, "OptimizedBackgroundDb": 0, "SetupCompleted": True, "CloseToTray": True, "StartWithSystem": False}))
    host = subprocess.Popen(HOST + ["--tray", "--window-command", "true"], stdout=subprocess.DEVNULL, stderr=open(LOG, "w"))
    check("host_ready", wait(lambda: SOCKET.exists() and "ready" in LOG.read_text(), 20), LOG.read_text()[-200:])
    second = subprocess.run(HOST + ["--tray"], capture_output=True, text=True, timeout=30)
    check("second_launch_uses_running_host", second.returncode == 0 and host.poll() is None, second.stderr[-120:])
    wait(lambda: "tray:" in LOG.read_text(), 6)
    if not SNAP and not NATIVE:  # the test VM has no desktop panel
      check("tray_registered", "tray: registered" in LOG.read_text(), [l for l in LOG.read_text().splitlines() if "tray" in l][:3])
    r = ask("select", 1, "test_speakers", 0, 1, 0); s = wait_state(lambda x: x["mode"] == 1 and not x["busy"])
    check("start_all_system_audio", r.startswith("1") and s, r[:160])
    check("default_is_sukut", wait(lambda: default() == "sukut_sink", 4), default())
    check("plays_to_output", wait(lambda: links(nid("sukut_playback")) == ["test_speakers"], 4), links(nid("sukut_playback")))
    wait(lambda: "Realtime portal" in LOG.read_text(), 6)
    # Not a check: a portal that refuses leaves processing at normal priority, which still runs.
    log("NOTE priority: " + "; ".join(l[:150] for l in LOG.read_text().splitlines() if "ealtime" in l))
    set_default("test_headphones")
    s = wait_state(lambda x: x["output"] == "test_headphones", 6)
    check("follows_sound_settings", s and default() == "sukut_sink", default())
    sink("test_bt", 950, " device.api=bluez5 device.id=90123")
    s = wait_state(lambda x: x["output"] == "test_bt", 6)
    check("new_device_used", s, state().get("output"))
    drop("test_bt"); wait_state(lambda x: x["output"] != "test_bt", 6)
    r = ask("select", 0, "test_headphones", 0, 1, 0); wait_state(lambda x: x["mode"] == 0 and not x["busy"])
    check("off_restores_default", wait(lambda: default() != "sukut_sink", 4) and nid("sukut_sink") is None, default())
    set_default("test_speakers")
    tone = pathlib.Path("/tmp/sukut-smoke-tone.wav")
    run("sh", "-c", f"python3 -I -c \"import wave,math;w=wave.open('{tone}','wb');w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(b''.join(int(4000*math.sin(i/20)).to_bytes(2,'little',signed=True)*2 for i in range(48000*40)));w.close()\"")
    play = subprocess.Popen(["pw-play", "--target", "test_speakers", str(tone)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    a = wait(lambda: app_node(play.pid))
    apps = json.loads(ask("apps")[1:])["apps"]
    check("host_lists_apps_with_host_pid", any(x["pid"] == play.pid for x in apps), apps[:4])
    check("own_stream_not_listed", not any(x["name"] == "Sukut" for x in apps), [x["name"] for x in apps])
    r = ask("select", 1, "test_speakers", play.pid, 1, 0); s = wait_state(lambda x: x["mode"] == 1 and not x["busy"])
    check("start_single_app", r.startswith("1") and s, r[:200])
    check("app_goes_through_sukut", wait(lambda: links(a) == ["sukut_sink"], 6), links(a))
    check("default_untouched_single_app", default() == "test_speakers", default())
    play.terminate(); play.wait(timeout=10)
    s = wait_state(lambda x: x["attention"] == "source_closed", 10)
    check("app_closed_ends_session", s and nid("sukut_sink") is None, s and s["attention"])
    ask("exit"); host.wait(timeout=20)
    check("exit_clean", host.returncode == 0 and nid("sukut_sink") is None, host.returncode)

original = default()
try:
    try: main()
    except Exception as e: check("ran", False, repr(e))
finally:
    if SOCKET.exists():
        try: ask("exit", timeout=5)
        except OSError: pass
    for n in ("test_speakers", "test_headphones", "test_bt"): drop(n)
    if original: set_default(original)
    p = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and p == len(results) else 'FAIL'} {p}/{len(results)}")
    (EVIDENCE / "results.json").write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n")
    (EVIDENCE / "log.txt").write_text("\n".join(lines) + "\n")
