"""Stage 3 test: Sukut's engine live on PipeWire (Linux/PLAN.md 5 and 12).

  python3 -I stage3_engine_test.py <build-bin-dir> <engine-dir> <speech.wav> <evidence-dir>

For each mode (Music Filter, Optimized, Strict) it plays test content to the
default output while Sukut runs, records a virtual output, then checks:
  - routing switched and restored, no gaps (underruns, overruns, segment resets, fallbacks);
  - parity: the engine's live output equals an offline replay of the exact input it received;
  - end to end: the virtual output received exactly what the engine produced.
Music Filter also checks Standby after 5 s of silence and instant wake.
Music and speech levels per section are reported for information; synthetic
music is not real music, so listening on real content remains a separate check.
"""
import json
import math
import pathlib
import random
import re
import signal
import subprocess
import sys
import threading
import time
import wave

BIN, ENGINE, SPEECH, EVIDENCE = (pathlib.Path(a) for a in sys.argv[1:5])
EVIDENCE.mkdir(parents=True, exist_ok=True)
WORK = pathlib.Path("/tmp/sukut-stage3")
WORK.mkdir(exist_ok=True)
RATE = 48000
results, log_lines, info = [], [], {}


def log(text):
    print(text, flush=True)
    log_lines.append(text)


def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": detail})
    log(f"{name}={'PASS' if ok else 'FAIL'}{' ' + detail if detail else ''}")


def run(*args, timeout=60):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=timeout)


def node_id(name):
    for obj in json.loads(run("pw-dump").stdout or "[]"):
        if obj.get("type") == "PipeWire:Interface:Node" and obj.get("info", {}).get("props", {}).get("node.name") == name:
            return obj["id"]
    return None


def default_sink():
    match = re.search(r"value:'(.*?)' type", run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink").stdout)
    try:
        return json.loads(match.group(1)).get("name", "") if match else ""
    except json.JSONDecodeError:
        return ""


def set_default(name):
    run("pw-metadata", "-n", "default", "0", "default.configured.audio.sink", json.dumps({"name": name}), "Spa:String:JSON")
    time.sleep(0.5)


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        assert w.getframerate() == RATE and w.getnchannels() == 2 and w.getsampwidth() == 2, path
        data = w.readframes(w.getnframes())
    return [int.from_bytes(data[i:i + 2], "little", signed=True) for i in range(0, len(data) - 1, 2)]


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(b"".join(int(max(-32768, min(32767, s))).to_bytes(2, "little", signed=True) for s in samples))


def read_s16(path):
    data = pathlib.Path(path).read_bytes()
    return [int.from_bytes(data[i:i + 2], "little", signed=True) for i in range(0, len(data) - 1, 2)]


def synth_music(seconds):
    """Piano-like chords, a bass line and a light beat: tonal, rhythmic, clearly not speech."""
    rng = random.Random(7)
    chords = [(261.63, 329.63, 392.00), (220.00, 261.63, 329.63), (174.61, 220.00, 261.63), (196.00, 246.94, 293.66)]
    bass = [65.41, 55.00, 87.31, 98.00]
    frames = int(seconds * RATE)
    out = [0.0] * (frames * 2)
    beat = RATE // 2
    for start in range(0, frames, beat):
        idx = (start // (beat * 4)) % 4
        for n in range(min(beat, frames - start)):
            t = n / RATE
            env = math.exp(-3.0 * t)
            tone = sum(math.sin(2 * math.pi * f * h * t) / h for f in chords[idx] for h in (1, 2, 3)) * env * 0.09
            tone += math.sin(2 * math.pi * bass[idx] * t) * 0.18 * math.exp(-1.5 * t)
            hat = (rng.random() * 2 - 1) * 0.05 * math.exp(-60 * t) if (start // beat) % 2 else 0.0
            kick = math.sin(2 * math.pi * (60 + 80 * math.exp(-20 * t)) * t) * 0.3 * math.exp(-12 * t) if (start // beat) % 2 == 0 else 0.0
            i = (start + n) * 2
            out[i] += (tone * 1.0 + bass[idx] * 0 + hat + kick) * 32767 * 0.6
            out[i + 1] += (tone * 0.9 + hat * 1.1 + kick) * 32767 * 0.6
    return [int(round(v)) for v in out]


def make_content():
    speech = read_wav(SPEECH)
    second = RATE * 2
    music = synth_music(10)
    silence = [0] * second
    music_only = music[:5 * second]
    speech_only = speech[:5 * second]
    mixed = [a + b for a, b in zip(speech[5 * second:10 * second], music[5 * second:10 * second])]
    content = silence + music_only + speech_only + mixed + silence
    write_wav(WORK / "content.wav", content)
    write_wav(WORK / "beep.wav", [int(8000 * math.sin(2 * math.pi * 880 * (i // 2) / RATE)) for i in range(int(0.3 * second))])
    return content


def locate(haystack, needle_start, probe=64):
    for start in range(0, len(haystack) - probe, 2):
        if haystack[start] == needle_start[0] and haystack[start:start + probe] == needle_start[:probe]:
            return start
    return -1


def db(values):
    energy = sum(v * v for v in values) / max(1, len(values))
    return 10 * math.log10(energy) if energy > 0 else -200.0


class Filter:
    def __init__(self, *args):
        self.lines = []
        self.proc = subprocess.Popen([str(BIN / "sukut_pw_filter"), *map(str, args)], stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.proc.stdout:
            self.lines.append(line.strip())

    def wait_for(self, prefix, seconds=30):
        end = time.time() + seconds
        while time.time() < end:
            hit = next((l for l in self.lines if l.startswith(prefix)), None)
            if hit:
                return hit
            if self.proc.poll() is not None and not any(l.startswith(prefix) for l in self.lines):
                return None
            time.sleep(0.05)
        return None

    def last_stats(self):
        stats = [l for l in self.lines if l.startswith("STATS")]
        return dict(kv.split("=", 1) for kv in stats[-1].split()[1:]) if stats else {}

    def stop(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        self.proc.wait(timeout=30)
        time.sleep(0.2)


def session(name, mode, background, content, standby_check):
    tap = WORK / name
    tap.mkdir(exist_ok=True)
    f = Filter("--engine", ENGINE, "--mode", mode, "--buffering", 1, "--background", background,
               "--diagnostics", "--tap", tap)
    started = f.wait_for("STARTED")
    check(f"{name}_started", started is not None, started or "; ".join(f.lines[-3:]))
    if not started:
        f.stop()
        return
    time.sleep(0.6)
    check(f"{name}_default_is_sukut", default_sink() == "sukut_sink", default_sink())
    recorder = subprocess.Popen(["pw-record", "--target", "test_speakers", "-P", "{ stream.capture.sink=true }",
                                 "--rate", "48000", "--channels", "2", "--format", "s16", str(tap / "speakers.wav")])
    time.sleep(0.8)
    run("pw-play", WORK / "content.wav", timeout=90)
    if standby_check:
        time.sleep(6.5)
        stats = f.last_stats()
        check(f"{name}_standby_after_5s_silence", stats.get("standby") == "1" and int(stats.get("standby_entries", 0)) >= 1,
              f"standby={stats.get('standby')} entries={stats.get('standby_entries')}")
        run("pw-play", WORK / "beep.wav")
        time.sleep(1.2)
        stats = f.last_stats()
        check(f"{name}_wakes_on_sound", stats.get("standby") == "0", f"standby={stats.get('standby')}")
    time.sleep(0.8)
    recorder.send_signal(signal.SIGINT)
    recorder.wait(timeout=10)
    f.stop()
    final = f.last_stats()
    timing = next((l for l in f.lines if l.startswith("TIMING")), "")
    log(f"{name}: {timing}")
    info[name] = {"timing": timing, "stats": final}
    check(f"{name}_stopped_and_restored", "STOPPED" in f.lines and default_sink() == "test_speakers" and node_id("sukut_sink") is None,
          default_sink())
    gaps = {k: int(final.get(k, -1)) for k in ("underruns", "overruns", "segments_reset", "fallbacks")}
    check(f"{name}_no_gaps", all(v == 0 for v in gaps.values()), json.dumps(gaps))

    # Parity: replay the exact engine input offline.
    replay = run(BIN / "sukut_offline_replay", ENGINE, tap / "engine-input.s16", tap / "replay-output.s16",
                 mode, background, 1, timeout=300)
    live, offline = read_s16(tap / "engine-output.s16"), read_s16(tap / "replay-output.s16")
    common = min(len(live), len(offline))
    same = common > 0 and live[:common] == offline[:common] and common >= len(live) - 2 * 960 * 2
    check(f"{name}_parity_with_offline_engine", same,
          f"live={len(live) // 2} offline={len(offline) // 2} frames compared={common // 2} ({replay.stdout.strip()})")

    # End to end: the speakers received exactly what the engine produced.
    speakers = read_wav(tap / "speakers.wav")
    first_sound = next((i for i, v in enumerate(live) if v != 0), -1)
    first_sound -= first_sound % 2
    at = locate(speakers, live[first_sound:first_sound + 64]) if first_sound >= 0 else -1
    span = min(len(live) - first_sound, len(speakers) - at) if at >= 0 else 0
    exact = at >= 0 and span > RATE * 2 * 10 and speakers[at:at + span] == live[first_sound:first_sound + span]
    check(f"{name}_speakers_receive_engine_output", exact, f"compared_frames={span // 2}")

    # Levels per section, from the engine's own input and output (information).
    engine_in = read_s16(tap / "engine-input.s16")
    start = next((i for i, v in enumerate(engine_in) if v != 0), 0)
    start -= start % 2
    second = RATE * 2
    edge = second // 10
    sections = {"music_only": (0, 5), "speech_only": (5, 10), "speech_plus_music": (10, 15)}
    levels = {}
    for label, (a, b) in sections.items():
        lo, hi = start + a * second + edge, start + b * second - edge
        levels[label] = {"input_db": round(db(engine_in[lo:hi]), 1), "output_db": round(db(live[lo:hi]), 1)}
        levels[label]["change_db"] = round(levels[label]["output_db"] - levels[label]["input_db"], 1)
    info[name]["levels"] = levels
    log(f"{name}_levels={json.dumps(levels)}")


def main():
    if run("pw-cli", "info", "0").returncode != 0:
        check("pipewire_running", False)
        return
    content = make_content()
    if node_id("test_speakers") is None:
        run("pw-cli", "create-node", "adapter", "{ factory.name=support.null-audio-sink node.name=test_speakers "
            "node.description=test_speakers media.class=Audio/Sink audio.position=[ FL FR ] audio.rate=48000 object.linger=true }")
        time.sleep(0.5)
    set_default("test_speakers")
    log(f"content_seconds={len(content) / 2 / RATE:.1f}")
    session("music_filter", 1, 0, content, standby_check=True)
    session("optimized", 2, -6, content, standby_check=False)
    session("strict", 3, 0, content, standby_check=False)


try:
    main()
finally:
    if (node := node_id("test_speakers")) is not None:
        run("pw-cli", "destroy", node)
    passed = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage3-results.json").write_text(json.dumps({"checks": results, "info": info}, indent=2) + "\n")
    (EVIDENCE / "stage3-log.txt").write_text("\n".join(log_lines) + "\n")
