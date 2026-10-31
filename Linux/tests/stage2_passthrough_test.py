"""Stage 2 test: Sukut's PipeWire pass-through without the model (Linux/PLAN.md 5 and 12).

Runs against the user's PipeWire and WirePlumber. It creates two virtual outputs
(test_speakers, test_headphones) so no real device is needed, and removes them at the end.

  python3 -I stage2_passthrough_test.py <sukut_pw_passthrough> <evidence-dir>

Checks:
  A. Routing: the default output becomes Sukut's sink, and audio played to the default
     arrives at the real output sample for sample.
  B. Volume and mute set on Sukut's sink are applied after capture (the model sees full level).
  C. Off restores the previous default; Sukut's sink and journal disappear.
  D. A killed Sukut: its sink disappears by itself, and --restore puts the default back.
  E. A newer user choice made while Sukut runs is kept on Off.
"""
import json
import math
import os
import pathlib
import random
import re
import signal
import subprocess
import sys
import threading
import time
import wave

TOOL, EVIDENCE = sys.argv[1], pathlib.Path(sys.argv[2])
EVIDENCE.mkdir(parents=True, exist_ok=True)
WORK = pathlib.Path("/tmp/sukut-stage2")
WORK.mkdir(exist_ok=True)
JOURNAL = pathlib.Path(os.environ.get("XDG_STATE_HOME", pathlib.Path.home() / ".local/state")) / "sukut/routing.json"
RATE, SECONDS = 48000, 3
results, log_lines = [], []


def log(text):
    print(text, flush=True)
    log_lines.append(text)


def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": detail})
    log(f"{name}={'PASS' if ok else 'FAIL'}{' ' + detail if detail else ''}")


def run(*args, **kwargs):
    return subprocess.run(args, capture_output=True, text=True, timeout=30, **kwargs)


def dump():
    return json.loads(run("pw-dump").stdout or "[]")


def node_id(name):
    for obj in dump():
        if obj.get("type") == "PipeWire:Interface:Node" and obj.get("info", {}).get("props", {}).get("node.name") == name:
            return obj["id"]
    return None


def default_sink(key="default.configured.audio.sink"):
    out = run("pw-metadata", "-n", "default", "0", key).stdout
    match = re.search(r"value:'(.*?)' type", out)
    if not match:
        return ""
    try:
        return json.loads(match.group(1)).get("name", "")
    except json.JSONDecodeError:
        return ""


def set_default(name):
    run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink", json.dumps({"name": name}), "Spa:String:JSON")
    time.sleep(0.5)


def make_output(name):
    if node_id(name) is None:
        run("pw-cli", "create-node", "adapter",
            "{ factory.name=support.null-audio-sink node.name=%s node.description=%s media.class=Audio/Sink "
            "audio.position=[ FL FR ] audio.rate=48000 object.linger=true }" % (name, name))
        time.sleep(0.5)
    return node_id(name)


def write_signal(path):
    # Deterministic noise, different on each side, never all-zero frames.
    rng = random.Random(20261029)
    frames = bytearray()
    for _ in range(RATE * SECONDS):
        for _channel in range(2):
            value = rng.randint(-12000, 12000) or 1
            frames += value.to_bytes(2, "little", signed=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(bytes(frames))


def read_samples(path):
    with wave.open(str(path), "rb") as w:
        data = w.readframes(w.getnframes())
    return [int.from_bytes(data[i:i + 2], "little", signed=True) for i in range(0, len(data) - 1, 2)]


def locate(recorded, reference, probe=64):
    """Sample index in `recorded` where `reference` starts, or -1."""
    key = reference[:probe]
    for start in range(0, len(recorded) - probe, 2):
        if recorded[start] == key[0] and recorded[start:start + probe] == key:
            return start
    return -1


def rms(values):
    return math.sqrt(sum(v * v for v in values) / max(1, len(values)))


class Passthrough:
    def __init__(self, *args):
        self.lines = []
        self.proc = subprocess.Popen([TOOL, *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.proc.stdout:
            self.lines.append(line.strip())

    def wait_for(self, prefix, seconds=10):
        end = time.time() + seconds
        while time.time() < end:
            for line in self.lines:
                if line.startswith(prefix):
                    return line
            time.sleep(0.05)
        return None

    def stop(self, sig=signal.SIGTERM):
        if self.proc.poll() is None:
            self.proc.send_signal(sig)
        self.proc.wait(timeout=15)
        self.reader.join(timeout=2)


def record_through(output, name):
    """Play the test signal to the default output while recording `output`'s monitor."""
    target = WORK / name
    recorder = subprocess.Popen(["pw-record", "--target", output, "-P", "{ stream.capture.sink=true }",
                                 "--rate", "48000", "--channels", "2", "--format", "s16", str(target)])
    time.sleep(0.8)
    run("pw-play", str(WORK / "signal.wav"))
    time.sleep(0.8)
    recorder.send_signal(signal.SIGINT)
    recorder.wait(timeout=10)
    return read_samples(target)


def main():
    if run("pw-cli", "info", "0").returncode != 0:
        check("pipewire_running", False, "pw-cli cannot reach PipeWire")
        return
    write_signal(WORK / "signal.wav")
    reference = read_samples(WORK / "signal.wav")
    speakers, headphones = make_output("test_speakers"), make_output("test_headphones")
    check("test_outputs_created", speakers is not None and headphones is not None)
    set_default("test_speakers")
    if JOURNAL.exists():
        JOURNAL.unlink()

    # A-C: one normal session.
    session = Passthrough("--seconds", "30")
    started = session.wait_for("STARTED")
    check("A_started", started is not None, started or "; ".join(session.lines[-3:]))
    time.sleep(0.5)
    check("A_default_is_sukut", default_sink() == "sukut_sink", default_sink())
    check("A_journal_written", JOURNAL.exists())
    check("A_output_is_previous_default", started is not None and "output=test_speakers" in started)

    recorded = record_through("test_speakers", "full.wav")
    at = locate(recorded, reference)
    exact = at >= 0 and recorded[at:at + len(reference)] == reference
    check("A_audio_sample_exact", exact, f"offset={at} recorded={len(recorded)} reference={len(reference)}")

    sink_id = node_id("sukut_sink")
    run("wpctl", "set-volume", str(sink_id), "0.5")
    time.sleep(0.6)
    stats_line = [l for l in session.lines if l.startswith("STATS")][-1]
    volume = float(re.search(r"volume=([0-9.e-]+)", stats_line).group(1))
    half = record_through("test_speakers", "half.wav")
    # The recording holds one copy of the signal plus silence, so the energy
    # ratio is the applied gain. Pre-volume capture scales it once; post-volume
    # capture would scale it twice (volume squared).
    ratio = math.sqrt(sum(v * v for v in half) / sum(v * v for v in reference))
    check("B_volume_follows_sink", abs(ratio - volume) < 0.005 and volume < 0.99,
          f"sink_volume={volume:.4f} output_gain={ratio:.4f} (twice would be {volume * volume:.4f})")
    run("wpctl", "set-volume", str(sink_id), "1.0")
    run("wpctl", "set-mute", str(sink_id), "1")
    time.sleep(0.6)
    muted = record_through("test_speakers", "muted.wav")
    check("B_mute_follows_sink", max((abs(v) for v in muted), default=0) == 0, f"peak={max((abs(v) for v in muted), default=0)}")
    run("wpctl", "set-mute", str(sink_id), "0")

    session.stop()
    stats = [l for l in session.lines if l.startswith("STATS")]
    final = stats[-1] if stats else ""
    underruns = int(re.search(r"underruns=(\d+)", final).group(1)) if final else -1
    overruns = int(re.search(r"overruns=(\d+)", final).group(1)) if final else -1
    fills = [int(re.search(r"fill=(\d+)", l).group(1)) for l in stats[:-1]]
    check("C_stopped", "STOPPED" in session.lines)
    check("C_default_restored", default_sink() == "test_speakers", default_sink())
    check("C_sink_removed", node_id("sukut_sink") is None)
    check("C_journal_removed", not JOURNAL.exists())
    check("C_no_underruns_or_overruns", underruns == 0 and overruns == 0, final)
    buffered_ms = (sum(fills) / len(fills) / 48.0) if fills else 0.0
    log(f"buffered_between_capture_and_playback_ms={buffered_ms:.1f} (prebuffer 1920 frames = 40 ms)")

    # D: killed while running.
    crashed = Passthrough()
    check("D_started", crashed.wait_for("STARTED") is not None)
    time.sleep(0.5)
    crashed.stop(signal.SIGKILL)
    time.sleep(1.0)
    check("D_sink_gone_after_kill", node_id("sukut_sink") is None)
    fallback = default_sink("default.audio.sink")
    check("D_apps_fall_back_to_real_output", fallback in ("test_speakers", "test_headphones"), fallback)
    check("D_journal_left_behind", JOURNAL.exists())
    restore = run(TOOL, "--restore").stdout.strip()
    check("D_restore_command", restore.startswith("RESTORED") and default_sink() == "test_speakers", restore)
    check("D_journal_cleared", not JOURNAL.exists())

    # E: the user picks another output while Sukut runs; Off must keep it.
    newer = Passthrough()
    check("E_started", newer.wait_for("STARTED") is not None)
    time.sleep(0.5)
    set_default("test_headphones")
    newer.stop()
    check("E_newer_choice_kept", default_sink() == "test_headphones", default_sink())
    set_default("test_speakers")


try:
    main()
finally:
    for name in ("test_speakers", "test_headphones"):
        if (node := node_id(name)) is not None:
            run("pw-cli", "destroy", str(node))
    passed = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage2-results.json").write_text(json.dumps(results, indent=2) + "\n")
    (EVIDENCE / "stage2-log.txt").write_text("\n".join(log_lines) + "\n")
