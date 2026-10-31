"""Stage 4 test: routing ownership with the engine running (Linux/PLAN.md 5.2, 5.5, 12).

  python3 -I stage4_routing_test.py <build-bin-dir> <engine-dir> <evidence-dir>

Uses two virtual outputs (test_speakers, test_headphones) and two stand-in apps
(pw-play playing long tones). Checks the actual PipeWire links, not only Sukut's output:
  A. One app: only that app goes through Sukut; the default output is untouched;
     closing the app ends the session (Windows rule) and releases everything.
  B. One app, Sukut killed: the app falls back to a real output; --restore clears its redirect.
  C. All system audio: the user picks another output while Sukut runs; Sukut plays there,
     stays in front of it, and that output is restored on Off.
  D. The output is removed; Sukut moves to the next device, waits when none is left,
     and uses a device that appears again.
  E. All system audio, Sukut killed: its sink disappears; --restore puts the default back.
"""
import json
import math
import os
import pathlib
import re
import signal
import subprocess
import sys
import threading
import time
import wave

BIN, ENGINE, EVIDENCE = (pathlib.Path(a) for a in sys.argv[1:4])
EVIDENCE.mkdir(parents=True, exist_ok=True)
WORK = pathlib.Path(os.environ.get("HOME", "/tmp")) / "sukut-stage4"
WORK.mkdir(exist_ok=True)
JOURNAL = pathlib.Path(os.environ.get("XDG_STATE_HOME", pathlib.Path.home() / ".local/state")) / "sukut/routing.json"
RATE = 48000
results, log_lines = [], []


def log(text):
    print(text, flush=True)
    log_lines.append(text)


def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": detail})
    log(f"{name}={'PASS' if ok else 'FAIL'}{' ' + detail if detail else ''}")


def run(*args, timeout=60):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=timeout)


def dump():
    return json.loads(run("pw-dump").stdout or "[]")


def nodes():
    found = {}
    for obj in dump():
        if obj.get("type") == "PipeWire:Interface:Node":
            props = obj.get("info", {}).get("props", {})
            found[obj["id"]] = props
    return found


def node_id(name):
    return next((i for i, p in nodes().items() if p.get("node.name") == name), None)


def app_node(pid):
    """The app's playback stream: PipeWire keeps the process id on the stream's client."""
    objects = dump()
    clients = {o["id"] for o in objects if o.get("type") == "PipeWire:Interface:Client"
               and str(o.get("info", {}).get("props", {}).get("pipewire.sec.pid")) == str(pid)}
    return next((o["id"] for o in objects if o.get("type") == "PipeWire:Interface:Node"
                 and o.get("info", {}).get("props", {}).get("media.class") == "Stream/Output/Audio"
                 and o.get("info", {}).get("props", {}).get("client.id") in clients), None)


def linked_to(source_id):
    """Names of the nodes a node's output is linked to."""
    objects = dump()
    names = {o["id"]: o.get("info", {}).get("props", {}).get("node.name", "") for o in objects
             if o.get("type") == "PipeWire:Interface:Node"}
    return sorted({names.get(o["info"]["input-node-id"], "?") for o in objects
                   if o.get("type") == "PipeWire:Interface:Link" and o.get("info", {}).get("output-node-id") == source_id})


def wait_until(predicate, seconds=5.0):
    end = time.time() + seconds
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.2)
    return predicate()


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
    out = run("pw-metadata", "-n", "default", str(node), "target.object").stdout
    match = re.search(r"value:'(.*?)'", out)
    return match.group(1) if match else ""


def make_output(name, priority):
    if node_id(name) is None:
        run("pw-cli", "create-node", "adapter",
            "{ factory.name=support.null-audio-sink node.name=%s node.description=%s media.class=Audio/Sink "
            "audio.position=[ FL FR ] audio.rate=48000 priority.session=%d object.linger=true }" % (name, name, priority))
        wait_until(lambda: node_id(name) is not None)
    return node_id(name)


def remove_output(name):
    if (node := node_id(name)) is not None:
        run("pw-cli", "destroy", node)
        wait_until(lambda: node_id(name) is None)


def tone(path, freq, seconds):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        frames = bytearray()
        for i in range(int(seconds * RATE)):
            v = int(6000 * math.sin(2 * math.pi * freq * i / RATE)).to_bytes(2, "little", signed=True)
            frames += v + v
        w.writeframes(bytes(frames))


def app(path):
    proc = subprocess.Popen(["pw-play", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait_until(lambda: app_node(proc.pid) is not None)
    return proc


def peak_of(path):
    with wave.open(str(path), "rb") as w:
        data = w.readframes(w.getnframes())
    return max((abs(int.from_bytes(data[i:i + 2], "little", signed=True)) for i in range(0, len(data) - 1, 2)), default=0)


class Filter:
    def __init__(self, *args):
        self.lines = []
        self.proc = subprocess.Popen([str(BIN / "sukut_pw_filter"), "--engine", str(ENGINE), "--mode", "1", *map(str, args)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            self.lines.append(line.strip())

    def wait_for(self, prefix, seconds=30):
        return wait_until(lambda: next((l for l in self.lines if l.startswith(prefix)), None), seconds)

    def stop(self, sig=signal.SIGTERM):
        if self.proc.poll() is None:
            self.proc.send_signal(sig)
        self.proc.wait(timeout=30)
        time.sleep(0.3)
        return self.proc.returncode


def scenario_a():
    app1, app2 = app(WORK / "app1.wav"), app(WORK / "app2.wav")
    f = Filter("--app", app1.pid)
    started = f.wait_for("STARTED")
    check("A_started_for_one_app", started is not None, started or "; ".join(f.lines[-3:]))
    targeted = f.wait_for("SOURCE_TARGETED", 8)
    check("A_app_stream_targeted", targeted is not None, targeted or "")
    a1, a2 = app_node(app1.pid), app_node(app2.pid)
    check("A_app_goes_through_sukut", wait_until(lambda: linked_to(a1) == ["sukut_sink"]) is not False and linked_to(a1) == ["sukut_sink"],
          f"app1 -> {linked_to(a1)}")
    check("A_other_app_untouched", linked_to(a2) == ["test_speakers"], f"app2 -> {linked_to(a2)}")
    check("A_default_unchanged", default_sink() == "test_speakers", default_sink())
    play = node_id("sukut_playback")
    check("A_sukut_plays_to_output", play is not None and linked_to(play) == ["test_speakers"], f"sukut_playback -> {linked_to(play) if play else None}")
    app1.terminate(); app1.wait(timeout=10)
    closed = f.wait_for("SOURCE_CLOSED", 8)
    code = f.proc.wait(timeout=15)
    check("A_app_closed_ends_session", closed is not None and code == 3, f"exit={code}")
    time.sleep(0.5)
    check("A_released_after_close", node_id("sukut_sink") is None and not JOURNAL.exists() and linked_to(a2) == ["test_speakers"],
          f"journal={JOURNAL.exists()}")
    app2.terminate(); app2.wait(timeout=10)


def scenario_b():
    app1 = app(WORK / "app1.wav")
    f = Filter("--app", app1.pid)
    check("B_started", f.wait_for("STARTED") is not None and f.wait_for("SOURCE_TARGETED", 8) is not None)
    a1 = app_node(app1.pid)
    wait_until(lambda: linked_to(a1) == ["sukut_sink"])
    f.stop(signal.SIGKILL)
    fallback = wait_until(lambda: linked_to(a1) if linked_to(a1) and "sukut_sink" not in linked_to(a1) else None, 6)
    check("B_app_falls_back_after_kill", fallback == ["test_speakers"], f"app1 -> {linked_to(a1)}")
    check("B_journal_left_behind", JOURNAL.exists())
    before = target_of(a1)
    restore = run(BIN / "sukut_pw_filter", "--restore").stdout.strip()
    after = target_of(a1)
    check("B_restore_releases_app", restore.startswith("RESTORED") and before != "" and after == "" and not JOURNAL.exists(),
          f"{restore}; target before={before!r} after={after!r}")
    app1.terminate(); app1.wait(timeout=10)


def scenario_c():
    set_default("test_speakers")
    f = Filter()
    check("C_started", f.wait_for("STARTED") is not None)
    time.sleep(0.6)
    set_default("test_headphones")  # the user picks headphones in Sound settings
    changed = f.wait_for("OUTPUT_CHANGED", 6)
    check("C_follows_user_choice", changed is not None and "to=test_headphones reason=default" in changed, changed or "")
    check("C_stays_default", wait_until(lambda: default_sink() == "sukut_sink", 4), default_sink())
    play = node_id("sukut_playback")
    check("C_plays_to_new_output", play is not None and wait_until(lambda: linked_to(play) == ["test_headphones"], 4),
          f"sukut_playback -> {linked_to(play) if play else None}")
    recorders = {name: subprocess.Popen(["pw-record", "--target", name, "-P", "{ stream.capture.sink=true }", "--rate", "48000",
                                         "--channels", "2", "--format", "s16", str(WORK / f"c-{name}.wav")]) for name in ("test_headphones", "test_speakers")}
    time.sleep(0.8)
    run("pw-play", WORK / "short.wav")
    time.sleep(0.8)
    for proc in recorders.values():
        proc.send_signal(signal.SIGINT); proc.wait(timeout=10)
    heard, silent = peak_of(WORK / "c-test_headphones.wav"), peak_of(WORK / "c-test_speakers.wav")
    check("C_audio_reaches_new_output_only", heard > 1000 and silent == 0, f"headphones_peak={heard} speakers_peak={silent}")
    f.stop()
    check("C_off_restores_new_choice", default_sink() == "test_headphones" and node_id("sukut_sink") is None, default_sink())


def scenario_d():
    set_default("test_speakers")
    f = Filter()
    check("D_started", f.wait_for("STARTED") is not None)
    time.sleep(0.6)
    remove_output("test_speakers")  # e.g. headphones unplugged
    moved = f.wait_for("OUTPUT_CHANGED", 6)
    check("D_moves_to_next_output", moved is not None and "to=test_headphones reason=removed" in moved, moved or "")
    play = node_id("sukut_playback")
    check("D_plays_to_next_output", play is not None and wait_until(lambda: linked_to(play) == ["test_headphones"], 4),
          f"sukut_playback -> {linked_to(play) if play else None}")
    remove_output("test_headphones")
    check("D_waits_when_no_output", f.wait_for("OUTPUT_LOST", 6) is not None)
    make_output("test_speakers", 1000)
    appeared = wait_until(lambda: next((l for l in f.lines if "reason=appeared" in l), None), 6)
    check("D_uses_output_that_appears", appeared is not None and "to=test_speakers" in appeared, appeared or "")
    f.stop()
    effective = wait_until(lambda: default_sink("default.audio.sink") if default_sink("default.audio.sink") == "test_speakers" else None, 4)
    check("D_off_leaves_a_real_output", node_id("sukut_sink") is None and effective == "test_speakers",
          f"configured={default_sink()} effective={default_sink('default.audio.sink')}")
    make_output("test_headphones", 900)


def scenario_e():
    set_default("test_speakers")
    f = Filter()
    check("E_started", f.wait_for("STARTED") is not None)
    time.sleep(0.6)
    check("E_default_is_sukut", default_sink() == "sukut_sink", default_sink())
    f.stop(signal.SIGKILL)
    check("E_sink_gone_after_kill", wait_until(lambda: node_id("sukut_sink") is None, 4))
    restore = run(BIN / "sukut_pw_filter", "--restore").stdout.strip()
    check("E_restore_puts_default_back", restore.startswith("RESTORED") and default_sink() == "test_speakers" and not JOURNAL.exists(), restore)


def main():
    if run("pw-cli", "info", "0").returncode != 0:
        check("pipewire_running", False)
        return
    tone(WORK / "app1.wav", 440, 40)
    tone(WORK / "app2.wav", 660, 40)
    tone(WORK / "short.wav", 880, 2)
    make_output("test_speakers", 1000)
    make_output("test_headphones", 900)
    set_default("test_speakers")
    if JOURNAL.exists():
        JOURNAL.unlink()
    for scenario in (scenario_a, scenario_b, scenario_c, scenario_d, scenario_e):
        scenario()


try:
    main()
finally:
    for name in ("test_speakers", "test_headphones"):
        remove_output(name)
    passed = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage4-results.json").write_text(json.dumps(results, indent=2) + "\n")
    (EVIDENCE / "stage4-log.txt").write_text("\n".join(log_lines) + "\n")
